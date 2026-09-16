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
    # launchd's PATH usually lacks ~/.local/bin, where the installer puts agy.
    fallback = Path.home() / ".local" / "bin" / "agy"
    return shutil.which("agy") or (str(fallback) if fallback.exists() else None)


def parse_quota(raw: str) -> tuple[float, float] | None:
    """(remaining fraction, reset unix time) of the Gemini weekly bucket, or None."""
    try:
        groups = json.loads(raw)["command"]["data"]["groups"]
    except (ValueError, KeyError, TypeError):
        return None
    for group in groups or []:
        for bucket in group.get("buckets") or []:
            if bucket.get("id") != BUCKET_ID:
                continue
            try:
                reset = datetime.datetime.fromisoformat(bucket["reset_time"].replace("Z", "+00:00"))
                return float(bucket["remaining_fraction"]), reset.timestamp()
            except (KeyError, TypeError, ValueError):
                return None
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
        proc = await asyncio.create_subprocess_exec(
            agy, "-p", "/quota", "--output-format", "json",
            stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL)
        try:
            out, _ = await asyncio.wait_for(proc.communicate(), QUOTA_TIMEOUT_S)
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()
            return None
        return parse_quota(out.decode(errors="replace"))

    async def get(self, now: float) -> dict:
        """Last known quota; refetched every few minutes, kept when a fetch fails."""
        if now - self.fetched_at >= QUOTA_REFRESH_S:
            self.fetched_at = now
            try:
                self.quota = await self.fetch() or self.quota
            except OSError:
                pass
        return quota_payload(self.quota, now)
