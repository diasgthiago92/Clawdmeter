"""Latest git commits made by each AI on this Mac, for the "Últimas Ações" screen.

A commit counts as an AI's when that AI ran `git commit` itself:

- Claude: Bash tool_use blocks in Claude Code transcripts (<config>/projects/**/*.jsonl),
  with the entry's timestamp and cwd.
- Kiro: shell toolUse blocks in kiro-cli session logs (~/.kiro/sessions/cli/*.jsonl).
  Assistant messages carry no timestamp, so a call takes the time of the prompt
  that started its turn; the cwd comes from the session's .json file.
- Gemini: run_command steps of Antigravity CLI conversations (CommandLine + Cwd).

Two ways a commit is found, merged by hash:
- the call's own `git commit`: the repository is worked out from the command
  (`cd X && ...`, `git -C X`) and the commit is looked up in `git log` as the
  first one created at or after the call (output can be `-q` or filtered);
- git's "[branch abc1234] subject" line in a tool's output, which also catches
  commits made by scripts the AI ran (e.g. a backup script).

Sent as {"a": [[ai, when, subject], ...], "o": offset, "n": total}, grouped by
AI (0 Claude, 1 Kiro, 2 Gemini), newest first inside each group, no time limit.
`when` is "HH:MM" for today and "dd/mm" before. An AI with no commit found
sends one row with an empty `when`.
"""

import datetime
import json
import os
import re
import shlex
import shutil
import sqlite3
import subprocess
import tempfile
import time
from pathlib import Path

from antigravity_usage import CONVERSATIONS, proto_fields
from kiro_usage import KIRO_CLI_SESSIONS
from market_quotes import chunk_table

AI_CLAUDE, AI_KIRO, AI_GEMINI = 0, 1, 2
COMMITS_PER_AI = 6
TEXT_MAX = 44
NO_COMMITS = "Nenhum commit encontrado"
EARLY_S = 5                          # clock slack before the call
LATE_S = {AI_CLAUDE: 300, AI_KIRO: 3600, AI_GEMINI: 600}   # Kiro's time is the turn start
GIT_LOG_REFRESH_S = 60

Call = tuple[float, str, str]        # (timestamp, cwd, command)
Commit = tuple[float, str, str]      # (time, short hash, subject)

# git's own commit summary inside JSON-escaped tool output: "[branch sha] subject"
# at the start of a line, straight followed by its "N files changed" line. Text
# that merely quotes such output (a log read back, grep -o) is escaped twice or
# lacks the stats line, so it doesn't match.
_COMMIT_LINE = re.compile(
    rb'(?:\\n|")\[[\w./-]+(?: \(root-commit\))? ([0-9a-f]{7,40})\] '
    rb'((?:[^"\\]|\\u[0-9a-fA-F]{4}|\\")+?)\\n \d+ files? changed')


def output_commits(line: bytes, ts: float) -> list[Commit]:
    """Commits announced in one raw JSON line of tool output."""
    out = []
    for sha, subject in _COMMIT_LINE.findall(line):
        try:
            text = json.loads(b'"' + subject + b'"')
        except ValueError:
            text = subject.decode("utf-8", errors="replace")
        out.append((ts, sha.decode()[:7], text))
    return out


def clip(text: str, limit: int = TEXT_MAX) -> str:
    text = " ".join(str(text).split())
    return text if len(text) <= limit else text[:limit - 3].rstrip() + "..."


def _ts(value) -> float | None:
    try:
        return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except (AttributeError, ValueError):
        return None


def is_commit_command(command) -> bool:
    return isinstance(command, str) and re.search(r"\bgit\b(\s+-C\s+\S+)?\s+commit(?![\w-])", command) is not None


def repo_dir(cwd: str, command: str) -> str:
    """Directory the `git commit` in `command` runs in: last `cd X` before it, or `git -C X`."""
    head = command[:re.search(r"\bgit\b(\s+-C\s+\S+)?\s+commit(?![\w-])", command).end()]
    here = cwd or str(Path.home())
    for segment in re.split(r"&&|;|\|\||\n", head):
        try:
            words = shlex.split(segment, posix=True)
        except ValueError:
            words = segment.split()
        if len(words) >= 2 and words[0] == "cd":
            here = _join(here, words[1])
        if words[:2] == ["git", "-C"] and len(words) >= 3:
            here = _join(here, words[2])
    return here


def _join(base: str, path: str) -> str:
    path = os.path.expandvars(os.path.expanduser(path))
    return os.path.normpath(os.path.join(base, path))


# ---- Claude ----

def claude_calls(lines) -> tuple[list[Call], list[Commit]]:
    out, seen_out = [], []
    for line in lines:
        if b'"tool_result"' in line and b"] " in line and _COMMIT_LINE.search(line):
            ts = _ts((re.search(rb'"timestamp":"([^"]+)"', line) or [None, b""])[1].decode())
            if ts is not None:
                seen_out += output_commits(line, ts)
            continue
        if b"git" not in line or b"commit" not in line or b'"tool_use"' not in line:
            continue
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        ts, msg = _ts(entry.get("timestamp")), entry.get("message")
        if ts is None or not isinstance(msg, dict) or not isinstance(msg.get("content"), list):
            continue
        for block in msg["content"]:
            if isinstance(block, dict) and block.get("type") == "tool_use" and block.get("name") == "Bash":
                command = (block.get("input") or {}).get("command")
                if is_commit_command(command):
                    out.append((ts, entry.get("cwd") or "", command))
    return out, seen_out


# ---- Kiro ----

_KIRO_PROMPT_TS = re.compile(rb'"meta"\s*:\s*\{[^{}]*"timestamp"\s*:\s*(\d+)')


def kiro_calls(lines, cwd: str) -> tuple[list[Call], list[Commit]]:
    out, seen_out = [], []
    turn_ts = None
    for line in lines:
        head = line[:80]
        if b'"ToolResults"' in head:          # huge (tool output, images): only pattern-matched
            if turn_ts is not None and b"] " in line:
                seen_out += output_commits(line, turn_ts)
            continue
        if b'"Prompt"' in head:
            found = _KIRO_PROMPT_TS.findall(line[-400:]) or _KIRO_PROMPT_TS.findall(line)
            turn_ts = float(found[-1]) if found else turn_ts
            continue
        if b'"AssistantMessage"' not in head or b"commit" not in line or turn_ts is None:
            continue
        try:
            content = json.loads(line)["data"]["content"]
        except (ValueError, KeyError, TypeError):
            continue
        for block in content:
            if isinstance(block, dict) and block.get("kind") == "toolUse":
                command = ((block.get("data") or {}).get("input") or {}).get("command")
                if is_commit_command(command):
                    out.append((turn_ts, cwd, command))
    return out, seen_out


def kiro_session_cwd(jsonl: Path) -> str:
    try:
        return json.loads(jsonl.with_suffix(".json").read_text()).get("cwd") or ""
    except (OSError, ValueError, AttributeError):
        return ""


# ---- Gemini (Antigravity CLI) ----

_AG_COMMAND = re.compile(rb'\{"CommandLine":')


def gemini_step_time(metadata: bytes) -> float | None:
    when = proto_fields(metadata or b"").get(1)
    ts = proto_fields(when).get(1) if isinstance(when, bytes) else None
    return float(ts) if isinstance(ts, int) else None


def gemini_calls(db_path: Path) -> tuple[list[Call], list[Commit]]:
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
    out, seen_out, last_ts, seen = [], [], None, set()
    for meta, payload in rows:
        last_ts = gemini_step_time(meta) or last_ts
        if last_ts is not None and payload and b"] " in payload:
            seen_out += output_commits(payload, last_ts)
        match = _AG_COMMAND.search(payload or b"")
        if not match or last_ts is None:
            continue
        try:
            inp, _ = json.JSONDecoder().raw_decode(payload[match.start():].decode("utf-8", errors="replace"))
        except ValueError:
            continue
        command = inp.get("CommandLine") if isinstance(inp, dict) else None
        if is_commit_command(command) and command not in seen:   # a call repeats across steps
            seen.add(command)
            out.append((last_ts, inp.get("Cwd") or "", command))
    return out, seen_out


# ---- git ----

class GitLog:
    """Recent commits per repository, re-read at most once a minute."""

    def __init__(self) -> None:
        self.cache: dict[str, tuple[float, list[tuple[float, str, str]]]] = {}

    def commits(self, repo: str, now: float) -> list[tuple[float, str, str]]:
        cached = self.cache.get(repo)
        if cached and now - cached[0] < GIT_LOG_REFRESH_S:
            return cached[1]
        try:
            out = subprocess.run(
                ["/usr/bin/git", "-C", repo, "log", "--all", "-n", "500", "--format=%H%x1f%ct%x1f%s"],
                capture_output=True, text=True, timeout=15).stdout
        except (OSError, subprocess.SubprocessError):
            out = ""
        rows = []
        for line in out.splitlines():
            parts = line.split("\x1f")
            if len(parts) == 3 and parts[1].isdigit():
                rows.append((float(parts[1]), parts[0], parts[2]))
        self.cache[repo] = (now, rows)
        return rows


def match_commits(ai: int, calls: list[Call], git: GitLog, now: float) -> list[Commit]:
    """Each commit call -> the first commit created at or after it in that repo."""
    found, used = [], set()
    for ts, cwd, command in sorted(calls):
        repo = repo_dir(cwd, command)
        best = None
        for ct, sha, subject in git.commits(repo, now):
            if ts - EARLY_S <= ct <= ts + LATE_S[ai] and sha not in used and (best is None or ct < best[0]):
                best = (ct, sha, subject)
        if best:
            used.add(best[1])
            found.append((best[0], best[1][:7], best[2]))
    return found


# ---- Collector ----

class AiActions:
    """Re-reads a source file only when its mtime/size changed."""

    def __init__(self, sessions_dir: Path = KIRO_CLI_SESSIONS, conversations: Path = CONVERSATIONS) -> None:
        self.sessions_dir = sessions_dir
        self.conversations = conversations
        self.cache: dict[Path, tuple[tuple, list[Call]]] = {}
        self.git = GitLog()

    def _cached(self, path: Path, read):
        try:
            st = path.stat()
            wal = path.with_name(path.name + "-wal")
            key = (max(st.st_mtime, wal.stat().st_mtime if wal.exists() else 0), st.st_size)
        except OSError:
            return [], []
        self._seen.add(path)
        cached = self.cache.get(path)
        if not cached or cached[0] != key:
            try:
                cached = (key, read(path))
            except (OSError, sqlite3.Error):
                return [], []
            self.cache[path] = cached
        return cached[1]

    def collect(self, config_dirs: list[Path], now: float | None = None) -> list[list[Commit]]:
        now = time.time() if now is None else now
        self._seen: set[Path] = set()
        calls: list[list[Call]] = [[], [], []]
        printed: list[list[Commit]] = [[], [], []]

        def add(ai, result):
            calls[ai] += result[0]
            printed[ai] += result[1]

        for d in config_dirs:
            for path in (d / "projects").rglob("*.jsonl"):
                add(AI_CLAUDE, self._cached(path, lambda p: claude_calls(p.read_bytes().splitlines())))
        for path in self.sessions_dir.glob("*.jsonl"):
            add(AI_KIRO, self._cached(path, lambda p: kiro_calls(p.read_bytes().splitlines(), kiro_session_cwd(p))))
        for path in self.conversations.glob("*.db"):
            add(AI_GEMINI, self._cached(path, gemini_calls))
        self.cache = {p: c for p, c in self.cache.items() if p in self._seen}
        return [merge_commits(match_commits(ai, calls[ai], self.git, now), printed[ai]) for ai in range(3)]

    def payloads(self, config_dirs: list[Path], now: float) -> list[dict]:
        return build_payloads(self.collect(config_dirs, now), now)


def merge_commits(looked_up: list[Commit], printed: list[Commit]) -> list[Commit]:
    """One entry per hash; the git-log time wins over a tool call's time."""
    by_sha: dict[str, Commit] = {}
    for commit in printed + looked_up:
        by_sha[commit[1]] = commit
    return list(by_sha.values())


def when_label(ts: float, now: float) -> str:
    moment = datetime.datetime.fromtimestamp(ts)
    today = datetime.datetime.fromtimestamp(now).date()
    return moment.strftime("%H:%M") if moment.date() == today else moment.strftime("%d/%m")


def build_rows(groups: list[list[Commit]], now: float) -> list[list]:
    rows = []
    for ai, commits in enumerate(groups):
        newest = sorted(commits, key=lambda c: c[0], reverse=True)[:COMMITS_PER_AI]
        rows += [[ai, when_label(ts, now), clip(subject)] for ts, _, subject in newest] or [[ai, "", NO_COMMITS]]
    return rows


def build_payloads(groups: list[list[Commit]], now: float) -> list[dict]:
    return chunk_table("a", build_rows(groups, now))
