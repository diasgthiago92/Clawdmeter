"""Today's Kiro routines, as announced by the leo-dias bot in Slack.

Every routine in ~/.kiro/rotinas posts its outcome to #leo-dias-news through
the bot token in ~/.kiro/.env. We read that channel (read-only) and turn the
day's messages into one row per routine:

    {"r": [[name, "HH:MM" of the latest run, 1 ok | 0 failed, runs today, 1 rerunnable], ...],
     "o": offset, "n": total}

Newest run first, up to ROUTINES_MAX rows (the device list scrolls).

Rerun: the device can ask for a failed routine to run again ({"rr": name} on
the request characteristic). Only routines in RERUN_LABELS (plus the optional
~/.config/claude-usage-monitor/routines.json {name: launchd label}) can be
started, and only through `launchctl kickstart` of their existing LaunchAgent —
the device never sends a command line.
"""

import asyncio
import datetime
import json
import os
import re
import logging
from zoneinfo import ZoneInfo
from pathlib import Path

import httpx

from market_quotes import chunk_table

KIRO_ENV = Path.home() / ".kiro" / ".env"
CHANNEL_ID = "C0B2X8FQ81M"   # leo-dias-news
HISTORY_URL = "https://slack.com/api/conversations.history"
ROUTINES_MAX = 24   # the device scrolls when they don't fit
ROUTINES_REFRESH_S = 60
NAME_MAX = 22
ROUTINES_OVERRIDES = Path.home() / ".config" / "claude-usage-monitor" / "routines.json"

# Routine name as shown on the device -> LaunchAgent that runs it.
RERUN_LABELS = {
    "OLX Rejected Rescue": "com.kiro.olx-rejected-rescue",
    "Board Financiamento": "com.kiro.update-dados-financiamento",
    "Histórico Veicular": "com.kiro.hv-volume-mensal",
    "Backup local": "com.thiago.kiro-backup-local",
    "Backup Drive": "com.thiago.kiro-backup-drive",
    "Organização e-mails": "com.kiro.organize-emails",
    "Vault Graph Sync": "com.kiro.vault-graph-sync",
    "Daily Vehicle Report": "com.kiro.daily-vehicle-report",
    "Daily Support Report": "com.kiro.daily-support-report",
    "Sprint Watcher": "com.kiro.sprint-confluence-watcher",
    "Autobanking Confluence": "com.kiro.atualiza-dados-autobanking-confluence-15h",
}
_LABEL_OK = re.compile(r"^[A-Za-z0-9._-]+$")


def rerun_labels(overrides: Path = ROUTINES_OVERRIDES) -> dict[str, str]:
    labels = dict(RERUN_LABELS)
    try:
        extra = json.loads(overrides.read_text())
        labels.update({k: v for k, v in extra.items() if isinstance(v, str) and _LABEL_OK.match(v)})
    except (OSError, ValueError, AttributeError):
        pass
    return labels


async def rerun(name: str, labels: dict[str, str] | None = None) -> bool:
    """Kickstart the routine's LaunchAgent; False for unknown names or launchctl errors."""
    label = (labels if labels is not None else rerun_labels()).get(name)
    if not label or not _LABEL_OK.match(label):
        return False
    proc = await asyncio.create_subprocess_exec(
        "launchctl", "kickstart", f"gui/{os.getuid()}/{label}",
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    return await proc.wait() == 0

# Messages without a bold title: (pattern, routine name). First match wins.
_KNOWN = (
    (re.compile(r"backup local", re.I), "Backup local"),
    (re.compile(r"backup \.kiro", re.I), "Backup Drive"),
    (re.compile(r"hist[óo]rico veicular", re.I), "Histórico Veicular"),
)
_SHORT = {
    "Organização de e-mails": "Organização e-mails",
}
_FAIL = re.compile(r"^:(x|rotating_light|red_circle|no_entry|warning):|\bfalh|\berro\b", re.I)
_EMOJI = re.compile(r":[a-z0-9_+\-]+:")


def load_token(env_path: Path = KIRO_ENV) -> str | None:
    try:
        for line in env_path.read_text().splitlines():
            if line.startswith("SLACK_BOT_TOKEN="):
                return line.split("=", 1)[1].strip().strip("'\"") or None
    except OSError:
        pass
    return None


def routine_of(text: str) -> tuple[str, bool] | None:
    """(routine name, succeeded) for one bot message, or None if unrecognised."""
    first = (text or "").strip().split("\n", 1)[0]
    if not first:
        return None
    ok = not _FAIL.search(first)
    for pattern, name in _KNOWN:
        if pattern.search(first):
            return name, ok
    bold = re.search(r"\*([^*]+)\*", first)
    if bold:
        name = bold.group(1).strip()
    else:
        name = _EMOJI.sub("", first).split(" — ")[0].split(":")[0].strip()
    if not name:
        return None
    name = _SHORT.get(name, name)
    return name[:NAME_MAX], ok


def build_rows(messages: list[dict], labels: dict[str, str] | None = None) -> list[list]:
    labels = RERUN_LABELS if labels is None else labels
    runs: dict[str, dict] = {}
    for msg in messages:
        found = routine_of(msg.get("text", ""))
        try:
            ts = float(msg["ts"])
        except (KeyError, TypeError, ValueError):
            continue
        if not found:
            continue
        name, ok = found
        entry = runs.setdefault(name, {"ts": 0.0, "ok": True, "count": 0})
        entry["count"] += 1
        if ts > entry["ts"]:
            entry["ts"], entry["ok"] = ts, ok
    ranked = sorted(runs.items(), key=lambda kv: kv[1]["ts"], reverse=True)[:ROUTINES_MAX]
    return [
        [name, datetime.datetime.fromtimestamp(e["ts"]).strftime("%H:%M"), int(e["ok"]), e["count"],
         int(name in labels)]
        for name, e in ranked
    ]


PHONE_ROOT = Path.home() / "monitor-celulares"


def phone_row(now: float, root: Path = PHONE_ROOT) -> list | None:
    """Local collector status: 0 error, 1 complete, 2 running, 3 waiting."""
    if not root.is_dir():
        return None
    today = datetime.datetime.fromtimestamp(now, ZoneInfo("America/Sao_Paulo")).date().isoformat()
    try:
        state = json.loads((root / "estado.json").read_text())
        if not isinstance(state, dict):
            raise ValueError("invalid state")
    except FileNotFoundError:
        return ["Preços celulares", "11:00", 3, 0, 0, 1]
    except (OSError, ValueError):
        return ["Preços celulares", "--:--", 0, 0, 0, 1]
    if state.get("attempt_day") != today:
        return ["Preços celulares", "11:00", 3, 0, 0, 1]
    status = {"completed": 1, "running": 2, "failed": 0}.get(state.get("status"), 3)
    field = "started_at" if status == 2 else "finished_at"
    try:
        stamp = datetime.datetime.fromisoformat(state[field]).astimezone(ZoneInfo("America/Sao_Paulo"))
        clock = stamp.strftime("%H:%M")
    except (KeyError, TypeError, ValueError):
        clock = "--:--"
    runs = sum(1 for _ in (root / "logs").glob(today.replace("-", "") + "-??????.log"))
    # Automatic retries are owned by the collector; do not offer an ineffective kickstart.
    return ["Preços celulares", clock, status, runs, 0, 1]


class KiroRoutines:
    def __init__(self, env_path: Path = KIRO_ENV) -> None:
        self.env_path = env_path
        self.payloads: list[dict] = []
        self.fetched_at = 0.0

    async def get(self, now: float) -> list[dict]:
        try:
            payloads = await self._get_slack(now)
        except Exception as error:
            logging.getLogger(__name__).warning("Rotinas Slack: %s", error)
            payloads = self.payloads
        rows = [row for payload in payloads for row in payload.get("r", [])]
        local = phone_row(now)
        if local is not None:
            rows.insert(0, local)
        return chunk_table("r", rows) if rows else [{"r": [], "o": 0, "n": 0}]

    async def _get_slack(self, now: float) -> list[dict]:
        if now - self.fetched_at < ROUTINES_REFRESH_S:
            return self.payloads
        self.fetched_at = now
        token = load_token(self.env_path)
        if not token:
            return self.payloads
        midnight = datetime.datetime.fromtimestamp(now).replace(hour=0, minute=0, second=0, microsecond=0)
        messages: list[dict] = []
        cursor = None
        async with httpx.AsyncClient(timeout=10.0, headers={"Authorization": f"Bearer {token}"}) as http:
            for _ in range(5):   # 5 pages x 200 is far more than a day of routine posts
                params = {"channel": CHANNEL_ID, "oldest": str(midnight.timestamp()), "limit": 200}
                if cursor:
                    params["cursor"] = cursor
                resp = await http.get(HISTORY_URL, params=params)
                resp.raise_for_status()
                body = resp.json()
                if not body.get("ok"):
                    raise ValueError(f"Slack: {body.get('error')}")
                messages.extend(body.get("messages") or [])
                cursor = (body.get("response_metadata") or {}).get("next_cursor")
                if not cursor:
                    break
        rows = build_rows(messages, rerun_labels())
        self.payloads = chunk_table("r", rows) if rows else [{"r": [], "o": 0, "n": 0}]
        return self.payloads
