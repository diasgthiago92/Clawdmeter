"""Token usage of Google Antigravity (CLI), read from its local conversation stores.

Each conversation is a SQLite file in ~/.gemini/antigravity-cli/conversations.
Model responses are `steps` rows with step_type 15 whose `metadata` protobuf
carries (reverse-engineered, undocumented):

    field 1 { field 1: unix seconds }          -> when the response was produced
    field 9 { 1: model code, 2: input tokens, 3: output tokens,
              5: cached input tokens, 9: thinking tokens }

Files are copied to a temp dir before reading so Antigravity's own WAL/locks
are never touched. Only this Mac's Antigravity CLI usage is visible.
"""

import datetime
import shutil
import sqlite3
import tempfile
from pathlib import Path

CONVERSATIONS = Path.home() / ".gemini" / "antigravity-cli" / "conversations"
RESPONSE_STEP = 15
_TOKEN_FIELDS = (2, 3, 5, 9)   # input, output, cached input, thinking


def _varint(buf: bytes, i: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        byte = buf[i]
        i += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, i


def proto_fields(buf: bytes) -> dict[int, int | bytes]:
    """First occurrence of each top-level field (varints and length-delimited only)."""
    out: dict[int, int | bytes] = {}
    i = 0
    try:
        while i < len(buf):
            key, i = _varint(buf, i)
            field, wire = key >> 3, key & 7
            if wire == 0:
                value, i = _varint(buf, i)
            elif wire == 2:
                length, i = _varint(buf, i)
                value, i = buf[i:i + length], i + length
            elif wire == 5:
                value, i = int.from_bytes(buf[i:i + 4], "little"), i + 4
            elif wire == 1:
                value, i = int.from_bytes(buf[i:i + 8], "little"), i + 8
            else:
                break
            out.setdefault(field, value)
    except IndexError:
        pass
    return out


def response_usage(metadata: bytes) -> tuple[float, int] | None:
    """(timestamp, tokens) for one response step's metadata, or None."""
    top = proto_fields(metadata or b"")
    when, usage = top.get(1), top.get(9)
    if not isinstance(when, bytes) or not isinstance(usage, bytes):
        return None
    ts = proto_fields(when).get(1)
    fields = proto_fields(usage)
    if not isinstance(ts, int):
        return None
    tokens = sum(v for k in _TOKEN_FIELDS if isinstance(v := fields.get(k, 0), int))
    return float(ts), tokens


def read_conversation(db_path: Path) -> list[tuple[float, int]]:
    with tempfile.TemporaryDirectory() as tmp:
        copy = Path(tmp) / db_path.name
        for suffix in ("", "-wal"):
            src = db_path.with_name(db_path.name + suffix)
            if src.exists():
                shutil.copyfile(src, Path(tmp) / src.name)
        con = sqlite3.connect(copy)
        try:
            rows = con.execute("SELECT metadata FROM steps WHERE step_type = ?", (RESPONSE_STEP,)).fetchall()
        except sqlite3.DatabaseError:
            rows = []
        finally:
            con.close()
    return [u for (meta,) in rows if (u := response_usage(meta))]


class AntigravityUsage:
    """Caches each conversation by (mtime, wal mtime); re-reads only what changed."""

    def __init__(self, conversations: Path = CONVERSATIONS) -> None:
        self.conversations = conversations
        self.cache: dict[Path, tuple[float, list[tuple[float, int]]]] = {}

    def responses(self, since: float) -> list[tuple[float, int]]:
        out = []
        seen = set()
        for db in self.conversations.glob("*.db"):
            try:
                wal = db.with_name(db.name + "-wal")
                mtime = max(db.stat().st_mtime, wal.stat().st_mtime if wal.exists() else 0)
            except OSError:
                continue
            seen.add(db)
            if mtime < since:
                continue
            cached = self.cache.get(db)
            if not cached or cached[0] != mtime:
                try:
                    cached = (mtime, read_conversation(db))
                except (OSError, sqlite3.Error):
                    continue
                self.cache[db] = cached
            out.extend(r for r in cached[1] if r[0] >= since)
        self.cache = {p: c for p, c in self.cache.items() if p in seen}
        return out

    def hourly(self, now: float, hours: int = 24) -> list[int]:
        start = now - hours * 3600
        bins = [0] * hours
        for ts, tokens in self.responses(start):
            i = int((ts - start) // 3600)
            if 0 <= i < hours:
                bins[i] += tokens
        return bins

    def since(self, start: float) -> int:
        return sum(tokens for _, tokens in self.responses(start))

    def today(self, now: float) -> tuple[int, int, int]:
        """(tokens today, responses today, busiest day of the last 30 days in tokens)."""
        local_now = datetime.datetime.fromtimestamp(now)
        midnight = local_now.replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
        month = self.responses(now - 30 * 86400)
        per_day: dict[datetime.date, int] = {}
        for ts, tokens in month:
            day = datetime.date.fromtimestamp(ts)
            per_day[day] = per_day.get(day, 0) + tokens
        today = [t for ts, t in month if ts >= midnight]
        return sum(today), len(today), max(per_day.values(), default=0)
