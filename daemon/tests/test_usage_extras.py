import json

from daemon.usage_extras import (
    HISTORY_BIN_S,
    HISTORY_BINS,
    ModelTokenTally,
    encode_history,
    record_history,
    session_window_start,
    short_model_name,
)


def test_short_model_name():
    assert short_model_name("claude-opus-5") == "Opus 5"
    assert short_model_name("claude-haiku-4-5-20251001") == "Haiku 4.5"
    assert short_model_name("claude-3-5-sonnet-20241022") == "Sonnet 3.5"
    assert short_model_name("claude-opus-4-8[1m]") == "Opus 4.8"


def test_encode_history_bins_and_gaps():
    now = 1_000_000.0
    samples = [
        [now - 10, 100],
        [now - 20, 40],
        [now - HISTORY_BIN_S * 95 - 1, 0],
    ]
    h = encode_history(samples, now)
    assert len(h) == HISTORY_BINS
    assert h[-1] == "/"          # max(100, 40) in the newest bin
    assert h[0] == "A"           # 0% in the oldest bin
    assert set(h[1:-1]) == {"-"}


def test_record_history_prunes_old_samples(tmp_path):
    path = tmp_path / "history.json"
    now = 2_000_000.0
    record_history(10, now - HISTORY_BINS * HISTORY_BIN_S - 5, path)
    samples = record_history(20, now, path)
    assert samples == [[now, 20]]
    assert json.loads(path.read_text()) == [[now, 20]]


def test_session_window_start_uses_reset_countdown():
    now = 10_000.0
    assert session_window_start({"sr": 60}, now) == now + 3600 - 5 * 3600
    assert session_window_start({"sr": -1}, now) == now - 5 * 3600


def _line(msg_id, model, ts, tokens):
    return json.dumps({
        "timestamp": ts,
        "requestId": f"req-{msg_id}",
        "message": {"id": msg_id, "model": model, "usage": {
            "input_tokens": tokens, "output_tokens": 0,
            "cache_creation_input_tokens": 0, "cache_read_input_tokens": 0,
        }},
    })


def test_model_tally_dedupes_and_tails_incrementally(tmp_path):
    proj = tmp_path / "projects" / "p"
    proj.mkdir(parents=True)
    log = proj / "s.jsonl"
    log.write_text(
        _line("a", "claude-opus-5", "2026-01-01T10:00:00Z", 100) + "\n"
        + _line("a", "claude-opus-5", "2026-01-01T10:00:00Z", 100) + "\n"
        + _line("b", "claude-haiku-4-5", "2026-01-01T10:01:00Z", 30) + "\n"
        + _line("old", "claude-opus-5", "2025-12-31T00:00:00Z", 999) + "\n"
    )
    since = 1767261600.0 - 60  # 2026-01-01T09:59:00Z
    tally = ModelTokenTally()
    assert tally.top([tmp_path], since) == [["Opus 5", 100], ["Haiku 4.5", 30]]

    with log.open("a") as f:
        f.write(_line("c", "claude-haiku-4-5", "2026-01-01T10:02:00Z", 500) + "\n")
        f.write('{"partial": ')
    assert tally.top([tmp_path], since) == [["Haiku 4.5", 530], ["Opus 5", 100]]


def test_encode_stacked_scales_each_series_then_the_stack():
    from daemon.usage_extras import encode_stacked

    claude = [0, 1000, 500, 0]
    kiro = [0, 10, 0, 5]
    c, k = encode_stacked(claude, kiro)
    assert len(c) == len(k) == 4
    # hour 1: both at their peak -> the stack fills the chart, split in half
    assert c[1] == k[1] == "g"            # 32 of 63
    assert c[0] == k[0] == "A"
    assert encode_stacked([0, 0], [0, 0]) == ["AA", "AA"]
