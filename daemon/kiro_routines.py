"""Today's Kiro routines, as announced by the leo-dias bot in Slack.

Every routine in ~/.kiro/rotinas posts its outcome to #leo-dias-news through
the bot token in ~/.kiro/.env. We read that channel (read-only) and turn the
day's messages into one row per routine:

    {"r": [[name, "HH:MM" of the latest run, 1 ok | 0 failed, runs today], ...],
     "o": offset, "n": total}

Newest run first, up to ROUTINES_MAX rows (the device list scrolls).
"""

import datetime
import re
from pathlib import Path

import httpx

from market_quotes import chunk_table

KIRO_ENV = Path.home() / ".kiro" / ".env"
CHANNEL_ID = "C0B2X8FQ81M"   # leo-dias-news
HISTORY_URL = "https://slack.com/api/conversations.history"
ROUTINES_MAX = 24   # the device scrolls when they don't fit
ROUTINES_REFRESH_S = 60
NAME_MAX = 22

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


def build_rows(messages: list[dict]) -> list[list]:
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
        [name, datetime.datetime.fromtimestamp(e["ts"]).strftime("%H:%M"), int(e["ok"]), e["count"]]
        for name, e in ranked
    ]


class KiroRoutines:
    def __init__(self, env_path: Path = KIRO_ENV) -> None:
        self.env_path = env_path
        self.payloads: list[dict] = []
        self.fetched_at = 0.0

    async def get(self, now: float) -> list[dict]:
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
        rows = build_rows(messages)
        self.payloads = chunk_table("r", rows) if rows else [{"r": [], "o": 0, "n": 0}]
        return self.payloads
