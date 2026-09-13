import sqlite3

from daemon.antigravity_usage import AntigravityUsage, proto_fields, response_usage


def _varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _field(num, value):
    if isinstance(value, int):
        return _varint(num << 3) + _varint(value)
    return _varint(num << 3 | 2) + _varint(len(value)) + value


def _metadata(ts, inp, out, cached=0, think=0):
    usage = _field(1, 1071) + _field(2, inp) + _field(3, out) + _field(5, cached) + _field(9, think)
    return _field(1, _field(1, ts)) + _field(6, _field(1, ts)) + _field(9, usage)


def test_response_usage_sums_token_fields():
    meta = _metadata(1_789_300_000, 1000, 200, 3000, 50)
    assert proto_fields(meta)[9]
    assert response_usage(meta) == (1_789_300_000.0, 4250)
    assert response_usage(b"") is None


def test_reads_conversation_dbs(tmp_path):
    db = sqlite3.connect(tmp_path / "c1.db")
    db.execute("CREATE TABLE steps (idx integer, step_type integer, metadata blob)")
    db.executemany("INSERT INTO steps VALUES (?, ?, ?)", [
        (0, 14, _metadata(1_789_300_000, 9, 9)),           # user turn: not a response
        (1, 15, _metadata(1_789_300_000, 100, 10)),
        (2, 15, _metadata(1_789_303_700, 200, 20)),
    ])
    db.commit()
    db.close()
    usage = AntigravityUsage(tmp_path)
    now = 1_789_304_000
    assert usage.since(now - 86400) == 330
    hourly = usage.hourly(now, 24)
    assert sum(hourly) == 330 and hourly[-1] == 220 and hourly[-2] == 110
