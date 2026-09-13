"""Extra device payloads: 24h session-% history and per-model token totals.

Each is sent as its own small BLE write ({"h": ...} / {"m": ...}) because a
write-without-response on macOS is capped near the link MTU (~180 bytes).
"""

import datetime
import json
import re
from pathlib import Path

HISTORY_FILE = Path.home() / ".config" / "claude-usage-monitor" / "history.json"
HISTORY_BIN_S = 15 * 60
HISTORY_BINS = 96
MODELS_MAX = 8   # the device scrolls when they don't fit
SESSION_WINDOW_S = 5 * 3600
STACK_HOURS = 24          # history screen: one stacked bar per hour

_B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
_TOKEN_FIELDS = (
    "input_tokens",
    "output_tokens",
    "cache_creation_input_tokens",
    "cache_read_input_tokens",
)


def record_history(session_pct: int, now: float, path: Path = HISTORY_FILE) -> list:
    """Append a sample, drop anything older than 24h, persist, return samples."""
    try:
        samples = json.loads(path.read_text())
    except (OSError, ValueError):
        samples = []
    horizon = now - HISTORY_BINS * HISTORY_BIN_S
    samples = [s for s in samples if isinstance(s, list) and len(s) == 2 and s[0] > horizon]
    samples.append([now, int(session_pct)])
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(samples))
    tmp.replace(path)
    return samples


def encode_history(samples: list, now: float) -> str:
    """96 chars, oldest first: max % per 15-min bin as a base64 digit, '-' if empty."""
    start = now - HISTORY_BINS * HISTORY_BIN_S
    bins: list[int | None] = [None] * HISTORY_BINS
    for ts, pct in samples:
        i = int((ts - start) // HISTORY_BIN_S)
        if 0 <= i < HISTORY_BINS:
            bins[i] = pct if bins[i] is None else max(bins[i], pct)
    return "".join(
        "-" if b is None else _B64[round(min(100, max(0, b)) * 63 / 100)] for b in bins
    )


def session_window_start(payload: dict, now: float) -> float:
    """Start of the current 5h window, derived from the reset countdown."""
    reset_mins = payload.get("sr")
    if isinstance(reset_mins, int) and 0 < reset_mins <= SESSION_WINDOW_S // 60:
        return now + reset_mins * 60 - SESSION_WINDOW_S
    return now - SESSION_WINDOW_S


def short_model_name(model: str) -> str:
    """claude-opus-5 -> "Opus 5", claude-haiku-4-5-20251001 -> "Haiku 4.5"."""
    m = re.sub(r"^claude-", "", model.split("[")[0])
    m = re.sub(r"-\d{8}$", "", m)
    parts = m.split("-")
    words = " ".join(p.capitalize() for p in parts if not p.isdigit())
    version = ".".join(p for p in parts if p.isdigit())
    return f"{words} {version}".strip()[:15]


def _parse_ts(value: str) -> float | None:
    try:
        return datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    except (AttributeError, ValueError):
        return None


class ModelTokenTally:
    """Tails Claude Code transcripts (<config>/projects/**/*.jsonl) incrementally.

    Only this machine's Claude Code usage is visible — claude.ai web/app and
    other computers never write here.
    """

    def __init__(self) -> None:
        self.offsets: dict[Path, int] = {}
        # (message id, request id) -> (timestamp, model, tokens). Claude Code
        # writes one line per content block, all repeating the same usage.
        self.records: dict[tuple, tuple[float, str, int]] = {}

    def _ingest(self, path: Path, size: int) -> None:
        offset = self.offsets.get(path, 0)
        if size < offset:
            offset = 0
        if size == offset:
            return
        with path.open("rb") as f:
            f.seek(offset)
            chunk = f.read(size - offset)
        end = chunk.rfind(b"\n")
        if end < 0:
            return
        self.offsets[path] = offset + end + 1
        for line in chunk[:end].splitlines():
            if b'"usage"' not in line:
                continue
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            msg = entry.get("message")
            if not isinstance(msg, dict):
                continue
            usage, model = msg.get("usage"), msg.get("model")
            ts = _parse_ts(entry.get("timestamp"))
            if not isinstance(usage, dict) or not model or model.startswith("<") or ts is None:
                continue
            tokens = sum(int(usage.get(k) or 0) for k in _TOKEN_FIELDS)
            self.records[(msg.get("id"), entry.get("requestId"))] = (ts, model, tokens)

    def _scan(self, config_dirs: list[Path], since: float) -> None:
        for d in config_dirs:
            for path in (d / "projects").rglob("*.jsonl"):
                try:
                    st = path.stat()
                except OSError:
                    continue
                if st.st_mtime >= since:
                    self._ingest(path, st.st_size)

    def hourly(self, config_dirs: list[Path], now: float) -> list[int]:
        """Tokens per hour over the last STACK_HOURS hours, oldest first."""
        start = now - STACK_HOURS * 3600
        self._scan(config_dirs, start)
        self.records = {k: r for k, r in self.records.items() if r[0] >= start - 3600}
        bins = [0] * STACK_HOURS
        for ts, _, tokens in self.records.values():
            i = int((ts - start) // 3600)
            if 0 <= i < STACK_HOURS:
                bins[i] += tokens
        return bins

    def top(self, config_dirs: list[Path], since: float) -> list[list]:
        """[[short name, tokens], ...] for the biggest models since `since`."""
        self._scan(config_dirs, since)
        totals: dict[str, int] = {}
        for ts, model, tokens in self.records.values():
            if ts >= since:
                name = short_model_name(model)
                totals[name] = totals.get(name, 0) + tokens
        ranked = sorted(totals.items(), key=lambda kv: kv[1], reverse=True)
        return [[name, tokens] for name, tokens in ranked[:MODELS_MAX] if tokens > 0]


def encode_stacked(*series: list[int]) -> list[str]:
    """Base64-digit strings (one per series) for the stacked hourly bars.

    Tokens and requests don't share a unit, so each series is scaled to its
    own busiest hour and the stack is scaled so the tallest bar fills the chart.
    """
    scaled = []
    for values in series:
        peak = max(values, default=0)
        scaled.append([v / peak if peak else 0.0 for v in values])
    tallest = max((sum(col) for col in zip(*scaled)), default=0.0)
    if not tallest:
        return [_B64[0] * len(values) for values in series]
    return ["".join(_B64[round(v / tallest * 63)] for v in values) for values in scaled]
