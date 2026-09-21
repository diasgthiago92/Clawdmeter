"""Codex CLI rate limits read from the tail of recent local session files.

The Codex CLI records ``token_count`` events as JSONL.  This module never
contacts the network: it samples only the newest rollout files and only a
bounded tail of each file, then caches the newest event for a minute.
"""

import datetime
import heapq
import json
import time
from pathlib import Path
from typing import Iterable

REFRESH_S = 60
MAX_FILES = 20
TAIL_BYTES = 256 * 1024
WINDOW_PAYLOADS = {"primary": "cxd", "secondary": "cxw"}


def _event_timestamp(value) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        return None
    try:
        return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


def _tail_lines(path: Path, tail_bytes: int) -> list[bytes]:
    with path.open("rb") as src:
        src.seek(0, 2)
        size = src.tell()
        start = max(0, size - tail_bytes)
        src.seek(start)
        data = src.read()
    lines = data.splitlines()
    # A bounded read can begin in the middle of a JSON object.
    return lines[1:] if start and lines else lines


def _latest_event(paths: Iterable[Path], tail_bytes: int = TAIL_BYTES) -> dict | None:
    newest: tuple[float, dict] | None = None
    for path in paths:
        try:
            lines = _tail_lines(path, tail_bytes)
        except OSError:
            continue
        for raw in reversed(lines):
            try:
                event = json.loads(raw)
            except (UnicodeDecodeError, ValueError):
                continue
            payload = event.get("payload")
            if not isinstance(payload, dict) or payload.get("type") != "token_count":
                continue
            limits = payload.get("rate_limits")
            stamp = _event_timestamp(event.get("timestamp"))
            if stamp is None or not isinstance(limits, dict):
                continue
            if newest is None or stamp > newest[0]:
                newest = stamp, limits
    return newest[1] if newest else None


def _file_totals(path: Path) -> list[tuple[float, int]]:
    """(timestamp, cumulative session tokens) of every token_count event in one rollout."""
    totals = []
    with path.open("rb") as src:
        for raw in src:
            if b'"token_count"' not in raw:
                continue
            try:
                event = json.loads(raw)
                info = event["payload"]["info"]
                stamp = _event_timestamp(event.get("timestamp"))
                total = int(info["total_token_usage"]["total_tokens"])
            except (UnicodeDecodeError, ValueError, KeyError, TypeError):
                continue
            if stamp is not None:
                totals.append((stamp, total))
    return totals


def _window_tokens(totals: list[tuple[float, int]], start: float, now: float) -> int:
    """Tokens spent inside [start, now]: the session counter's growth over that span."""
    before = 0
    last = 0
    for stamp, total in totals:
        if stamp < start:
            before = total
        elif stamp <= now:
            last = total
    return max(0, last - before) if last else 0


def usage_payload(rate_limits: dict | None, now: float, tokens: dict | None = None) -> dict:
    result = {"cxd": [-1, -1, 0], "cxw": [-1, -1, 0]}
    if not rate_limits:
        return result
    for window, key in WINDOW_PAYLOADS.items():
        limit = rate_limits.get(window)
        if not isinstance(limit, dict):
            continue
        try:
            used = min(100, max(0, round(float(limit["used_percent"]))))
            reset = float(limit["resets_at"])
        except (KeyError, TypeError, ValueError):
            continue
        spent = (tokens or {}).get(key, 0)
        if reset <= now:
            result[key] = [0, 0, 0]
        else:
            result[key] = [used, max(0, int((reset - now) // 60)), spent]
    return result


class CodexUsage:
    def __init__(self, roots: Iterable[Path] | None = None,
                 refresh_s: int = REFRESH_S, max_files: int = MAX_FILES,
                 tail_bytes: int = TAIL_BYTES) -> None:
        home = Path.home()
        self.roots = list(roots or (home / ".claude" / "codex" / "sessions",
                                    home / ".codex" / "sessions"))
        self.refresh_s = refresh_s
        self.max_files = max_files
        self.tail_bytes = tail_bytes
        self.rate_limits: dict | None = None
        self.tokens: dict[str, int] = {}
        self.fetched_at = 0.0
        self.cache: dict[Path, tuple[float, list[tuple[float, int]]]] = {}

    def _recent_files(self) -> list[Path]:
        candidates: list[tuple[float, str, Path]] = []
        for root in self.roots:
            if not root.exists():
                continue
            for path in root.rglob("rollout-*.jsonl"):
                try:
                    candidates.append((path.stat().st_mtime, str(path), path))
                except OSError:
                    continue
        return [item[2] for item in heapq.nlargest(self.max_files, candidates)]

    def _tokens(self, rate_limits: dict | None, now: float) -> dict[str, int]:
        """Tokens spent in the 5h and weekly windows, from every session that touched them."""
        starts = {}
        for window, key in WINDOW_PAYLOADS.items():
            limit = (rate_limits or {}).get(window)
            try:
                starts[key] = float(limit["resets_at"]) - float(limit["window_minutes"]) * 60
            except (KeyError, TypeError, ValueError):
                continue
        if not starts:
            return {}
        oldest = min(starts.values())
        out = dict.fromkeys(starts, 0)
        for totals in self._all_totals(oldest):
            for key, start in starts.items():
                out[key] += _window_tokens(totals, start, now)
        return out

    def _all_totals(self, since: float) -> list[list[tuple[float, int]]]:
        """Token counter series of every session file touched since `since` (cached by mtime)."""
        result = []
        seen = set()
        for root in self.roots:
            if not root.exists():
                continue
            for path in root.rglob("rollout-*.jsonl"):
                try:
                    mtime = path.stat().st_mtime
                    if mtime < since:
                        continue
                    seen.add(path)
                    cached = self.cache.get(path)
                    if not cached or cached[0] != mtime:
                        cached = (mtime, _file_totals(path))
                        self.cache[path] = cached
                except OSError:
                    continue
                result.append(cached[1])
        self.cache = {p: c for p, c in self.cache.items() if p in seen}
        return result

    def hourly(self, now: float, hours: int = 24) -> list[int]:
        """Tokens per hour over the last `hours` hours, oldest first."""
        start = now - hours * 3600
        bins = [0] * hours
        for totals in self._all_totals(start):
            previous = 0
            for stamp, total in totals:
                if stamp >= start:
                    i = int((stamp - start) // 3600)
                    if 0 <= i < hours:
                        bins[i] += max(0, total - previous)
                previous = total
        return bins

    def get(self, now: float | None = None) -> dict:
        now = time.time() if now is None else now
        if not self.fetched_at or now - self.fetched_at >= self.refresh_s:
            self.rate_limits = _latest_event(self._recent_files(), self.tail_bytes)
            self.tokens = self._tokens(self.rate_limits, now)
            self.fetched_at = now
        return usage_payload(self.rate_limits, now, self.tokens)
