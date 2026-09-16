import json
import unittest

from daemon.antigravity_quota import parse_quota, quota_payload

RAW = json.dumps({"command": {"data": {"groups": [
    {"name": "Gemini Models", "buckets": [{"id": "gemini-weekly", "remaining_fraction": 0.62,
                                           "reset_time": "2026-09-23T17:17:08Z"}]},
    {"name": "Claude and GPT models", "buckets": [{"id": "3p-weekly", "remaining_fraction": 1,
                                                   "reset_time": "2026-09-23T17:17:08Z"}]},
]}}})


class ParseQuotaTest(unittest.TestCase):
    def test_gemini_bucket(self):
        remaining, reset = parse_quota(RAW)
        self.assertAlmostEqual(remaining, 0.62)
        self.assertEqual(reset, 1790183828.0)

    def test_garbage(self):
        self.assertIsNone(parse_quota("not json"))
        self.assertIsNone(parse_quota('{"command": {}}'))

    def test_payload(self):
        self.assertEqual(quota_payload((0.62, 1790183828.0), 1790183828.0 - 3600 * 25), {"agw": [38, 1500]})
        self.assertEqual(quota_payload(None, 0), {"agw": [-1, -1]})


if __name__ == "__main__":
    unittest.main()
