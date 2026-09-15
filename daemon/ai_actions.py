"""Latest actions of each AI on this Mac, for the "Últimas Ações" screen.

- Claude: tool_use blocks in Claude Code transcripts (<config>/projects/**/*.jsonl).
- Kiro: toolUse blocks in kiro-cli session logs (~/.kiro/sessions/cli/*.jsonl).
  Assistant messages carry no timestamp, so each action takes the time of the
  prompt that started its turn.
- Gemini: tool steps of Antigravity CLI conversations; each tool's JSON input
  carries a "toolAction" sentence written by the agent itself.

Sent as {"a": [[ai, "HH:MM", text], ...], "o": offset, "n": total}, grouped by
AI (0 Claude, 1 Kiro, 2 Gemini), newest first inside each group. An AI with no
action in the window sends one row with an empty time.
"""

import datetime
import json
import re
import shutil
import sqlite3
import tempfile
from pathlib import Path

from antigravity_usage import CONVERSATIONS, proto_fields
from kiro_usage import KIRO_CLI_SESSIONS
from market_quotes import chunk_table

AI_CLAUDE, AI_KIRO, AI_GEMINI = 0, 1, 2
ACTIONS_PER_AI = 6
WINDOW_S = 24 * 3600
TEXT_MAX = 44            # characters; the firmware truncates with "..." anyway
NO_ACTIONS = "Sem ações nas últimas 24h"

Action = tuple[float, str]   # (timestamp, text)


def clip(text: str, limit: int = TEXT_MAX) -> str:
    text = " ".join(str(text).split())
    return text if len(text) <= limit else text[:limit - 3].rstrip() + "..."


def _name(path) -> str:
    return Path(str(path)).name if path else ""


def _ts(value) -> float | None:
    try:
        return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except (AttributeError, ValueError):
        return None


# ---- Claude ----

def claude_action_text(name: str, inp: dict) -> str | None:
    """Short Portuguese description of one Claude Code tool call."""
    inp = inp if isinstance(inp, dict) else {}
    if name == "Bash":
        return inp.get("description") or inp.get("command")
    if name in ("Edit", "MultiEdit", "NotebookEdit"):
        return f"Editou {_name(inp.get('file_path') or inp.get('notebook_path'))}"
    if name == "Write":
        return f"Escreveu {_name(inp.get('file_path'))}"
    if name == "Read":
        return f"Leu {_name(inp.get('file_path'))}"
    if name in ("Grep", "Glob"):
        return f"Buscou {inp.get('pattern', '')}"
    if name == "WebSearch":
        return f"Pesquisou {inp.get('query', '')}"
    if name == "WebFetch":
        return f"Abriu {inp.get('url', '')}"
    if name in ("Agent", "Task"):
        return f"Agente: {inp.get('description', '')}"
    if name == "Skill":
        return f"Skill {inp.get('skill', '')}"
    if name in ("TodoWrite", "ToolSearch", "AskUserQuestion", "ScheduleWakeup"):
        return None
    if name.startswith("mcp__"):
        return name.split("__", 2)[-1].replace("_", " ")
    return name


def claude_actions(lines, since: float) -> list[Action]:
    out = []
    for line in lines:
        if b'"tool_use"' not in line:
            continue
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        ts = _ts(entry.get("timestamp"))
        msg = entry.get("message")
        if ts is None or ts < since or not isinstance(msg, dict) or not isinstance(msg.get("content"), list):
            continue
        for block in msg["content"]:
            if isinstance(block, dict) and block.get("type") == "tool_use":
                text = claude_action_text(str(block.get("name", "")), block.get("input"))
                if text:
                    out.append((ts, clip(text)))
    return out


# ---- Kiro ----

_KIRO_PROMPT_TS = re.compile(rb'"meta"\s*:\s*\{[^{}]*"timestamp"\s*:\s*(\d+)')


def kiro_action_text(name: str, inp: dict) -> str | None:
    inp = inp if isinstance(inp, dict) else {}
    purpose = inp.get("__tool_use_purpose")
    if purpose:
        return purpose
    if name == "shell":
        return inp.get("command")
    return name or None


def kiro_actions(lines, since: float) -> list[Action]:
    out = []
    turn_ts = None
    for line in lines:
        head = line[:80]
        if b'"ToolResults"' in head:          # huge (tool output, images): never decoded
            continue
        if b'"Prompt"' in head:
            found = _KIRO_PROMPT_TS.findall(line[-400:]) or _KIRO_PROMPT_TS.findall(line)
            turn_ts = float(found[-1]) if found else turn_ts
            continue
        if b'"AssistantMessage"' not in head or b'"toolUse"' not in line or turn_ts is None or turn_ts < since:
            continue
        try:
            content = json.loads(line)["data"]["content"]
        except (ValueError, KeyError, TypeError):
            continue
        for block in content:
            if isinstance(block, dict) and block.get("kind") == "toolUse":
                data = block.get("data") or {}
                text = kiro_action_text(str(data.get("name", "")), data.get("input"))
                if text:
                    out.append((turn_ts, clip(text)))
    return out


# ---- Gemini (Antigravity CLI) ----

_AG_TOOL = re.compile(rb'([a-z][a-z0-9_]{2,40})[\x00-\x7f]{1,3}(\{"[A-Za-z]+":)')


def gemini_step_action(payload: bytes) -> str | None:
    """The "toolAction" sentence (or tool name) of one step's payload, if it is a tool call."""
    match = _AG_TOOL.search(payload or b"")
    if not match:
        return None
    text = payload[match.start(2):].decode("utf-8", errors="replace")
    try:
        inp, _ = json.JSONDecoder().raw_decode(text)
    except ValueError:
        inp = {}
    if not isinstance(inp, dict) or match.group(1) == b"ask_permission":
        return None
    return inp.get("toolAction") or inp.get("Description")   # anything else is not a tool call


def gemini_step_time(metadata: bytes) -> float | None:
    when = proto_fields(metadata or b"").get(1)
    ts = proto_fields(when).get(1) if isinstance(when, bytes) else None
    return float(ts) if isinstance(ts, int) else None


def gemini_actions(db_path: Path, since: float) -> list[Action]:
    with tempfile.TemporaryDirectory() as tmp:
        for suffix in ("", "-wal"):
            src = db_path.with_name(db_path.name + suffix)
            if src.exists():
                shutil.copyfile(src, Path(tmp) / src.name)
        con = sqlite3.connect(Path(tmp) / db_path.name)
        try:
            rows = con.execute("SELECT metadata, step_payload FROM steps ORDER BY idx").fetchall()
        except sqlite3.DatabaseError:
            rows = []
        finally:
            con.close()
    out, last_ts = [], None
    for meta, payload in rows:
        last_ts = gemini_step_time(meta) or last_ts
        text = gemini_step_action(payload)
        if text and last_ts is not None and last_ts >= since:
            out.append((last_ts, clip(text)))
    return out


# ---- Collector ----

class AiActions:
    """Re-reads a source file only when its mtime/size changed."""

    def __init__(self, sessions_dir: Path = KIRO_CLI_SESSIONS, conversations: Path = CONVERSATIONS) -> None:
        self.sessions_dir = sessions_dir
        self.conversations = conversations
        self.cache: dict[Path, tuple[tuple, list[Action]]] = {}

    def _cached(self, path: Path, since: float, read) -> list[Action]:
        try:
            st = path.stat()
            wal = path.with_name(path.name + "-wal")
            mtime = max(st.st_mtime, wal.stat().st_mtime if wal.exists() else 0)
        except OSError:
            return []
        if mtime < since:
            return []
        key = (mtime, st.st_size)
        cached = self.cache.get(path)
        if not cached or cached[0] != key:
            try:
                cached = (key, read(path))
            except (OSError, sqlite3.Error):
                return []
            self.cache[path] = cached
        self._seen.add(path)
        return [a for a in cached[1] if a[0] >= since]

    def collect(self, config_dirs: list[Path], now: float) -> list[list[Action]]:
        since = now - WINDOW_S
        self._seen: set[Path] = set()
        read_lines = lambda parse: (lambda p: parse(p.read_bytes().splitlines(), since))
        groups: list[list[Action]] = [[], [], []]
        for d in config_dirs:
            for path in (d / "projects").rglob("*.jsonl"):
                groups[AI_CLAUDE] += self._cached(path, since, read_lines(claude_actions))
        for path in self.sessions_dir.glob("*.jsonl"):
            groups[AI_KIRO] += self._cached(path, since, read_lines(kiro_actions))
        for path in self.conversations.glob("*.db"):
            groups[AI_GEMINI] += self._cached(path, since, lambda p: gemini_actions(p, since))
        self.cache = {p: c for p, c in self.cache.items() if p in self._seen}
        return groups

    def payloads(self, config_dirs: list[Path], now: float) -> list[dict]:
        return build_payloads(self.collect(config_dirs, now))


def build_rows(groups: list[list[Action]]) -> list[list]:
    rows = []
    for ai, actions in enumerate(groups):
        newest = sorted(actions, key=lambda a: a[0], reverse=True)
        picked, seen = [], set()
        for ts, text in newest:              # repeated calls in a row show once
            if (text, int(ts // 60)) in seen:
                continue
            seen.add((text, int(ts // 60)))
            picked.append([ai, datetime.datetime.fromtimestamp(ts).strftime("%H:%M"), text])
            if len(picked) == ACTIONS_PER_AI:
                break
        rows += picked or [[ai, "", NO_ACTIONS]]
    return rows


def build_payloads(groups: list[list[Action]]) -> list[dict]:
    return chunk_table("a", build_rows(groups))
