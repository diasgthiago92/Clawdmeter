"""Weekly quota of Google Antigravity (Gemini models group), via `agy -p /quota`.

The CLI answers read-only slash commands in print mode without starting an
agent turn or spending quota. With `--output-format json` the payload is:

    {"command": {"data": {"groups": [{"name": "Gemini Models",
        "buckets": [{"id": "gemini-weekly", "window": "weekly",
                     "remaining_fraction": 1, "reset_time": "2026-09-23T17:17:08Z"}]}]}}}

Payload sent to the device: {"agw": [used %, minutes until reset]} (-1 = unknown).
"""

import asyncio
import datetime
import json
import shutil
from pathlib import Path

QUOTA_REFRESH_S = 300
QUOTA_TIMEOUT_S = 30
BUCKET_ID = "gemini-weekly"


def agy_path() -> str | None:
    # launchd's PATH usually lacks ~/.local/bin and ~/.gemini/*/bin, where installers put agy.
    candidates = [
        Path.home() / ".local" / "bin" / "agy",
        Path.home() / ".gemini" / "antigravity-cli" / "bin" / "agy",
        Path.home() / ".gemini" / "antigravity" / "bin" / "agy",
    ]
    if found := shutil.which("agy"):
        return found
    for cand in candidates:
        if cand.exists():
            return str(cand)
    return None


def parse_quota(raw: str) -> tuple[float, float] | None:
    """(remaining fraction, reset unix time) of the Gemini weekly bucket, or None."""
    # 1. Structured JSON parsing (supports new and old AGY output format)
    try:
        data = json.loads(raw)
        groups = data
        if isinstance(data, dict):
            if "command" in data and isinstance(data["command"], dict):
                data = data["command"].get("data", {})
            groups = data.get("groups", []) if isinstance(data, dict) else []

        for group in groups or []:
            gname = str(group.get("name", "")).lower()
            buckets = group.get("buckets") or []
            for bucket in buckets:
                bid = str(bucket.get("id", "")).lower()
                window = str(bucket.get("window", "")).lower()
                if bid in ("gemini-weekly", "gemini_weekly") or ("gemini" in gname and window == "weekly"):
                    try:
                        reset = datetime.datetime.fromisoformat(bucket["reset_time"].replace("Z", "+00:00"))
                        return float(bucket["remaining_fraction"]), reset.timestamp()
                    except (KeyError, TypeError, ValueError):
                        continue
    except (ValueError, TypeError, KeyError, AttributeError):
        pass

    # 2. Text regex fallback (for plain text / TSV output from `agy -p /quota`)
    import re
    for line in raw.splitlines():
        if "gemini" in line.lower() and "weekly" in line.lower():
            m = re.search(r"(\d+(?:\.\d+)?)%\s+(\d{4}-\d{2}-\d{2}T[^\s]+)", line)
            if m:
                remaining_pct = float(m.group(1))
                remaining_frac = remaining_pct / 100.0
                reset_str = m.group(2)
                try:
                    reset = datetime.datetime.fromisoformat(reset_str.replace("Z", "+00:00"))
                    return remaining_frac, reset.timestamp()
                except ValueError:
                    pass
    return None


def quota_payload(quota: tuple[float, float] | None, now: float) -> dict:
    if quota is None:
        return {"agw": [-1, -1]}
    remaining, reset_ts = quota
    used = min(100, max(0, round((1 - remaining) * 100)))
    return {"agw": [used, max(0, int((reset_ts - now) // 60))]}


class AntigravityQuota:
    def __init__(self) -> None:
        self.quota: tuple[float, float] | None = None
        self.fetched_at = 0.0

    async def fetch(self) -> tuple[float, float] | None:
        agy = agy_path()
        if not agy:
            return None
        # Try JSON format first
        proc = await asyncio.create_subprocess_exec(
            agy, "-p", "/quota", "--output-format", "json",
            stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL)
        try:
            out, _ = await asyncio.wait_for(proc.communicate(), QUOTA_TIMEOUT_S)
            parsed = parse_quota(out.decode(errors="replace"))
            if parsed is not None:
                return parsed
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()
            return None

        # Fallback to plain text output if JSON output didn't parse
        proc = await asyncio.create_subprocess_exec(
            agy, "-p", "/quota",
            stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL)
        try:
            out, _ = await asyncio.wait_for(proc.communicate(), QUOTA_TIMEOUT_S)
            return parse_quota(out.decode(errors="replace"))
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()
            return None

    async def get(self, now: float) -> dict:
        """Last known quota; refetched every few minutes, kept when a fetch fails."""
        if now - self.fetched_at >= QUOTA_REFRESH_S:
            self.fetched_at = now
            try:
                self.quota = await self.fetch() or self.quota
            except OSError:
                pass
        return quota_payload(self.quota, now)

