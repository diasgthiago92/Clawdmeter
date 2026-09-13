import datetime
import json

from daemon.kiro_usage import build_payload, latest_credit_usage, parse_log_line

CREDIT = {
    "currentUsage": 722, "currentUsageWithPrecision": 722.89,
    "usageLimit": 2000, "usageLimitWithPrecision": 2000,
    "resourceType": "CREDIT", "nextDateReset": "2026-10-01T00:00:00.000Z",
}


def _line(ts, credit):
    body = {"commandName": "GetUsageLimitsCommand", "output": {"usageBreakdownList": [credit]}}
    return f"{ts} [info] {json.dumps(body)}\n"


def test_parse_log_line():
    assert parse_log_line(_line("2026-09-11 14:51:08.627", CREDIT)) == ("2026-09-11 14:51:08.627", CREDIT)
    assert parse_log_line("2026-09-11 14:51:08.627 [info] outra coisa\n") is None


def test_latest_credit_usage_prefers_newest(tmp_path):
    for session, ts, used in (("20260910T142131", "2026-09-10 22:44:12.538", 600),
                              ("20260911T145102", "2026-09-11 14:51:08.627", 722)):
        log = tmp_path / session / "window1" / "exthost" / "kiro.kiroAgent" / "q-client.log"
        log.parent.mkdir(parents=True)
        log.write_text(_line(ts, {**CREDIT, "currentUsageWithPrecision": used}))
    (tmp_path / "20260912T090000").mkdir()   # newer session without a reading
    assert latest_credit_usage(tmp_path)["currentUsageWithPrecision"] == 722


def test_build_payload():
    now = datetime.datetime(2026, 9, 13, 12, 0, tzinfo=datetime.timezone.utc)
    assert build_payload(CREDIT, now) == {"k": [36, 18]}
    later = datetime.datetime(2026, 10, 2, tzinfo=datetime.timezone.utc)
    assert build_payload(CREDIT, later) == {"k": 0}
    assert build_payload(None, now) == {"k": 0}


def test_session_turns_and_activity(tmp_path):
    from daemon.kiro_usage import KiroActivity, encode_activity, session_turns

    session = {"session_state": {"conversation_metadata": {"user_turn_metadatas": [
        {"end_timestamp": "2026-09-13T12:00:00Z", "total_request_count": 4},
        {"end_timestamp": "2026-09-13T12:05:00Z", "total_request_count": 2},
        {"end_timestamp": "2026-09-13T08:00:00Z", "total_request_count": 3},
        {"model": "auto"},
    ]}}}
    turns = session_turns(session)
    assert len(turns) == 3
    now = datetime.datetime(2026, 9, 13, 12, 10, tzinfo=datetime.timezone.utc).timestamp()
    encoded, peak = encode_activity(turns, now)
    assert len(encoded) == 96 and peak == 6
    assert encoded[-1] == "/" and encoded.count("A") == 94   # busiest bin = full scale

    (tmp_path / "s1.json").write_text(json.dumps(session))
    history, in_window = KiroActivity(tmp_path).payloads(now, now - 3600)
    assert history["kp"] == 6 and in_window == 6
