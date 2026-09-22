"""Kiro credit usage, read from the Kiro IDE's own logs.

Kiro bills in credits, not tokens. The IDE logs every GetUsageLimits response
(one JSON object per line) in q-client.log; we take the newest one. No auth or
network: the number is only as fresh as the IDE's last check.

Sent as {"k": [percent, days until reset, credits used, credit limit]}, or
{"k": 0} when there is nothing current to show (no log yet, or the logged cycle
has already reset).

Kiro records neither tokens nor a real model per request (every turn says
"auto" with zero token counts), so activity comes from kiro-cli's session
files instead: each turn's end time and request count. KiroActivity turns
that into the 24h history line ({"hk": 96 chars, "kp": peak requests per bin})
and the requests in the current 5h window for the Models screen.

Credits per period aren't recorded by current kiro-cli versions, so the 24h and
5h figures are *estimated*: requests x the average credits per request found in
kiro-cli's own history (older sessions in data.sqlite3 kept a per-request
`usage_info` credit value). The monthly total above is the real figure.
"""

import datetime
import json
import math
from pathlib import Path

from usage_extras import _B64, HISTORY_BIN_S, HISTORY_BINS

KIRO_LOGS = Path.home() / "Library" / "Application Support" / "Kiro" / "logs"
LOG_GLOB = "*/exthost/kiro.kiroAgent/q-client.log"
MARKER = "usageBreakdownList"
KIRO_REFRESH_S = 5 * 60
KIRO_CLI_SESSIONS = Path.home() / ".kiro" / "sessions" / "cli"
KIRO_CLI_STORE = Path.home() / "Library" / "Application Support" / "kiro-cli" / "data.sqlite3"
DEFAULT_CREDITS_PER_REQUEST = 0.16

# Live GetUsageLimits: the same call the Kiro IDE makes, so the monthly figure
# stays fresh even when the IDE is closed (we only use kiro-cli). Auth comes
# from the IDE's SSO token cache, which kiro-cli/Kiro refresh on their own.
KIRO_TOKEN = Path.home() / ".aws" / "sso" / "cache" / "kiro-auth-token.json"
KIRO_PROFILE_ARN = "arn:aws:codewhisperer:us-east-1:717564244052:profile/WXD3ADXVREVU"
CW_TARGET = "AmazonCodeWhispererService.GetUsageLimits"


def _read_token(path: Path = KIRO_TOKEN) -> str | None:
    """Fresh, unexpired bearer token from the IDE's SSO cache, or None."""
    try:
        tok = json.loads(path.read_text())
        access = tok.get("accessToken")
        exp = tok.get("expiresAt")
        if not access or not exp:
            return None
        expires = datetime.datetime.fromisoformat(exp.replace("Z", "+00:00"))
        if expires <= datetime.datetime.now(datetime.timezone.utc):
            return None   # expired; the caller falls back to the IDE log
        return access
    except (OSError, ValueError, KeyError, TypeError):
        return None


def _normalize_credit(credit: dict) -> dict:
    """Make a live-API CREDIT breakdown look like a logged one: build_payload
    expects nextDateReset as an ISO string, but the API returns an epoch float."""
    reset = credit.get("nextDateReset")
    if isinstance(reset, (int, float)):
        credit = dict(credit)
        credit["nextDateReset"] = (
            datetime.datetime.fromtimestamp(reset, datetime.timezone.utc).isoformat()
        )
    return credit


def live_credit_usage(token_path: Path = KIRO_TOKEN, arn: str = KIRO_PROFILE_ARN) -> dict | None:
    """Call GetUsageLimits directly and return the CREDIT breakdown, or None on
    any failure (missing/expired token, no network, unexpected shape)."""
    import urllib.error
    import urllib.request

    access = _read_token(token_path)
    if not access:
        return None
    try:
        tok = json.loads(token_path.read_text())
        region = tok.get("region", "us-east-1")
    except (OSError, ValueError):
        region = "us-east-1"
    url = f"https://codewhisperer.{region}.amazonaws.com/"
    body = json.dumps({"profileArn": arn, "origin": "AI_EDITOR",
                       "resourceType": "AGENTIC_REQUEST"}).encode()
    req = urllib.request.Request(url, data=body, method="POST", headers={
        "Content-Type": "application/x-amz-json-1.0",
        "X-Amz-Target": CW_TARGET,
        "Authorization": "Bearer " + access,
    })
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.loads(resp.read())
    except (urllib.error.URLError, ValueError, OSError):
        return None
    credit = next((b for b in data.get("usageBreakdownList", [])
                   if b.get("resourceType") == "CREDIT"), None)
    return _normalize_credit(credit) if credit else None



def parse_log_line(line: str) -> tuple[str, dict] | None:
    """(timestamp, credit breakdown) from one q-client.log line, if it holds one."""
    if MARKER not in line or "{" not in line:
        return None
    try:
        entry = json.loads(line[line.index("{"):])
        breakdowns = entry["output"]["usageBreakdownList"]
    except (ValueError, KeyError, TypeError):
        return None
    credit = next((b for b in breakdowns if b.get("resourceType") == "CREDIT"), None)
    if not credit:
        return None
    return line[:23], credit


def latest_credit_usage(logs_dir: Path = KIRO_LOGS) -> dict | None:
    """Newest logged credit breakdown. Session dirs are named by start time, so
    walk them newest first and stop at the first one that has a reading."""
    for session in sorted((p for p in logs_dir.glob("*") if p.is_dir()), reverse=True):
        best = None
        for log in session.glob(LOG_GLOB):
            try:
                with log.open(encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        found = parse_log_line(line)
                        if found and (best is None or found[0] >= best[0]):
                            best = found
            except OSError:
                continue
        if best:
            return best[1]
    return None


def build_payload(credit: dict | None, now: datetime.datetime) -> dict:
    if not credit:
        return {"k": 0}
    try:
        used = float(credit.get("currentUsageWithPrecision", credit["currentUsage"]))
        limit = float(credit.get("usageLimitWithPrecision", credit["usageLimit"]))
        reset = datetime.datetime.fromisoformat(credit["nextDateReset"].replace("Z", "+00:00"))
    except (KeyError, TypeError, ValueError):
        return {"k": 0}
    remaining_s = (reset - now).total_seconds()
    if limit <= 0 or remaining_s <= 0:   # stale reading from an already-reset cycle
        return {"k": 0}
    return {"k": [min(999, round(used / limit * 100)), math.ceil(remaining_s / 86400), round(used), round(limit)]}


class KiroUsage:
    def __init__(self, logs_dir: Path = KIRO_LOGS) -> None:
        self.logs_dir = logs_dir
        self.credit: dict | None = None
        self.read_at = 0.0

    def get(self, now: float) -> dict:
        if now - self.read_at >= KIRO_REFRESH_S:
            self.read_at = now
            # Live API first (fresh even with the IDE closed); the IDE log is the
            # fallback when the SSO token is missing/expired or the call fails.
            fresh = live_credit_usage() or latest_credit_usage(self.logs_dir)
            self.credit = fresh or self.credit
        return build_payload(self.credit, datetime.datetime.fromtimestamp(now, datetime.timezone.utc))


def session_turns(session: dict) -> list[tuple[float, int]]:
    """(end time, request count) for each turn in one kiro-cli session file."""
    state = (session or {}).get("session_state") or {}
    turns = ((state.get("conversation_metadata") or {}).get("user_turn_metadatas")) or []
    out = []
    for turn in turns:
        try:
            ts = datetime.datetime.fromisoformat(turn["end_timestamp"].replace("Z", "+00:00")).timestamp()
            out.append((ts, int(turn.get("total_request_count") or 0)))
        except (KeyError, TypeError, ValueError, AttributeError):
            continue
    return out


def encode_activity(turns: list[tuple[float, int]], now: float) -> tuple[str, int]:
    """96 chars oldest first, scaled to the busiest 15-min bin, and that bin's requests."""
    start = now - HISTORY_BINS * HISTORY_BIN_S
    bins = [0] * HISTORY_BINS
    for ts, requests in turns:
        i = int((ts - start) // HISTORY_BIN_S)
        if 0 <= i < HISTORY_BINS:
            bins[i] += requests
    peak = max(bins)
    return "".join(_B64[round(b * 63 / peak) if peak else 0] for b in bins), peak


class KiroActivity:
    """Re-reads only kiro-cli session files touched within the last 24h."""

    def __init__(self, sessions_dir: Path = KIRO_CLI_SESSIONS) -> None:
        self.sessions_dir = sessions_dir
        self.cache: dict[Path, tuple[float, list[tuple[float, int]]]] = {}

    def turns(self, now: float) -> list[tuple[float, int]]:
        horizon = now - HISTORY_BINS * HISTORY_BIN_S
        out = []
        seen = set()
        for path in self.sessions_dir.glob("*.json"):
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if mtime < horizon:
                continue
            seen.add(path)
            cached = self.cache.get(path)
            if not cached or cached[0] != mtime:
                try:
                    cached = (mtime, session_turns(json.loads(path.read_text())))
                except (OSError, ValueError):
                    continue
                self.cache[path] = cached
            out.extend(t for t in cached[1] if t[0] >= horizon)
        self.cache = {p: c for p, c in self.cache.items() if p in seen}
        return out

    def payloads(self, now: float, window_start: float) -> tuple[dict, int]:
        """({"hk": ..., "kp": ...}, requests since window_start)."""
        turns = self.turns(now)
        encoded, peak = encode_activity(turns, now)
        in_window = sum(r for ts, r in turns if ts >= window_start)
        return {"hk": encoded, "kp": peak}, in_window

    def hourly(self, now: float, hours: int = 24) -> list[int]:
        """Requests per hour over the last `hours` hours, oldest first."""
        start = now - hours * 3600
        bins = [0] * hours
        for ts, requests in self.turns(now):
            i = int((ts - start) // 3600)
            if 0 <= i < hours:
                bins[i] += requests
        return bins


def credits_per_request(store: Path = KIRO_CLI_STORE) -> float:
    """Average credits per request in kiro-cli's recorded usage_info (read-only)."""
    import re
    import sqlite3
    try:
        con = sqlite3.connect(f"file:{store}?mode=ro", uri=True)
        try:
            values = []
            for (value,) in con.execute("SELECT value FROM conversations_v2"):
                for match in re.finditer(r'"usage_info"\s*:\s*(\[[^\]]*\])', value):
                    try:
                        values += [float(e["value"]) for e in json.loads(match.group(1)) if e.get("unit") == "credit"]
                    except (ValueError, KeyError, TypeError):
                        continue
        finally:
            con.close()
    except sqlite3.Error:
        return DEFAULT_CREDITS_PER_REQUEST
    return sum(values) / len(values) if values else DEFAULT_CREDITS_PER_REQUEST
