import json
import os
import tempfile
import unittest
from pathlib import Path

from daemon.codex_usage import CodexUsage


def event(timestamp, primary, secondary):
    return json.dumps({"timestamp": timestamp, "payload": {
        "type": "token_count", "rate_limits": {
            "primary": primary, "secondary": secondary,
        }}})


class CodexUsageTest(unittest.TestCase):
    def test_newest_event_across_recent_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            older = root / "2026/09/19/rollout-old.jsonl"
            newer = root / "2026/09/20/rollout-new.jsonl"
            older.parent.mkdir(parents=True)
            newer.parent.mkdir(parents=True)
            older.write_text(event("2026-09-20T10:00:00Z",
                {"used_percent": 12, "window_minutes": 300, "resets_at": 2_000_000_000},
                {"used_percent": 34, "window_minutes": 10080, "resets_at": 2_000_100_000}) + "\n")
            newer.write_text("{}\n" + event("2026-09-20T11:00:00Z",
                {"used_percent": 56.4, "window_minutes": 300, "resets_at": 2_000_000_060},
                {"used_percent": 78.6, "window_minutes": 10080, "resets_at": 2_000_100_060}) + "\n")
            os.utime(newer, (200, 200))
            os.utime(older, (100, 100))

            payload = CodexUsage([root], refresh_s=60).get(2_000_000_000)
            self.assertEqual(payload, {"cxd": [56, 1, 0], "cxw": [79, 1667, 0]})

    def test_expired_window_resets_usage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "rollout-expired.jsonl"
            path.write_text(event("2026-09-20T11:00:00Z",
                {"used_percent": 90, "window_minutes": 300, "resets_at": 999},
                {"used_percent": 40, "window_minutes": 10080, "resets_at": 2000}) + "\n")
            self.assertEqual(CodexUsage([root]).get(1000),
                             {"cxd": [0, 0, 0], "cxw": [40, 16, 0]})

    def test_no_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(CodexUsage([Path(tmp)]).get(1000),
                             {"cxd": [-1, -1, 0], "cxw": [-1, -1, 0]})

    def test_null_limits_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "rollout-null-tail.jsonl"
            # Event with valid rate limits followed by a later event with null rate limits (e.g. limit_id: premium)
            path.write_text("\n".join([
                event("2026-09-20T10:00:00Z",
                      {"used_percent": 45, "window_minutes": 300, "resets_at": 2_000_000_000},
                      {"used_percent": 15, "window_minutes": 10080, "resets_at": 2_000_100_000}),
                json.dumps({"timestamp": "2026-09-20T10:05:00Z", "payload": {
                    "type": "token_count", "rate_limits": {
                        "limit_id": "premium", "primary": None, "secondary": None
                    }}}),
            ]) + "\n")
            payload = CodexUsage([root]).get(2_000_000_000 - 3600)
            self.assertEqual(payload["cxd"][0], 45)
            self.assertEqual(payload["cxw"][0], 15)

    def test_tokens_per_window(self):
        def line(ts, total, primary_reset=1000 + 300 * 60, secondary_reset=1000 + 10080 * 60):
            return json.dumps({"timestamp": ts, "payload": {"type": "token_count",
                "info": {"total_token_usage": {"total_tokens": total}},
                "rate_limits": {
                    "primary": {"used_percent": 10, "window_minutes": 300, "resets_at": primary_reset},
                    "secondary": {"used_percent": 20, "window_minutes": 10080, "resets_at": secondary_reset}}}})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "rollout-a.jsonl"
            # Both windows start at t=1000 (reset minus window length).
            path.write_text("\n".join([
                line("1970-01-01T00:00:10Z", 500),                                 # before both windows
                line("1970-01-01T00:20:00Z", 800),                                 # in 5h window (t=1200)
                line("1970-01-01T00:30:00Z", 1300),                                # in 5h window (t=1800)
            ]) + "\n")
            payload = CodexUsage([Path(tmp)]).get(2000)
            self.assertEqual(payload["cxd"][2], 1300 - 500)
            self.assertEqual(payload["cxw"][2], 1300 - 500)

    def test_hourly_tokens(self):
        def line(ts, total):
            return json.dumps({"timestamp": ts, "payload": {"type": "token_count",
                "info": {"total_token_usage": {"total_tokens": total}}}})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "rollout-h.jsonl"
            path.write_text("\n".join([
                line("1970-01-01T00:10:00Z", 100),    # t=600, before the 2h window
                line("1970-01-01T02:10:00Z", 400),    # t=7800 -> hour 0 of window starting at 7200
                line("1970-01-01T03:10:00Z", 1000),   # t=11400 -> hour 1
            ]) + "\n")
            bins = CodexUsage([Path(tmp)]).hourly(14400, hours=2)
            self.assertEqual(bins, [300, 600])


if __name__ == "__main__":
    unittest.main()
