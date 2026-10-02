#!/usr/bin/env python3
"""Tests for carrying the last Claude usage while the token is dead (macOS daemon).

A dead token used to blank the device with {"ok": false}: Consumo Atual fell to
the idle "Escutando / Sem dados" screen and every other screen stopped updating.
Now the daemon re-sends the last good payload, aged to the current time.

Run: python -m pytest daemon/tests/test_carry_usage.py -x -q
"""
import asyncio
import json
from unittest.mock import AsyncMock, MagicMock, patch

import daemon.claude_usage_daemon as mod

PRO = {"s": 40, "sr": 120, "w": 55, "wr": 3000, "st": "allowed", "acct": "pro", "ok": True,
       "c": 1, "t": 111, "tf": 24}


def _saved(tmp_path, payload=PRO, at=1000.0):
    path = tmp_path / "last-usage.json"
    mod.save_last_usage(payload, at, path)
    return path


def _carried(path, now):
    with patch.object(mod, "add_chime_field"), patch.object(mod, "add_clock_fields"):
        return mod.carried_usage(now, path)


def test_no_saved_usage_carries_nothing(tmp_path):
    assert mod.carried_usage(1000.0, tmp_path / "missing.json") is None


def test_corrupt_file_carries_nothing(tmp_path):
    path = tmp_path / "last-usage.json"
    path.write_text("{not json")
    assert mod.carried_usage(1000.0, path) is None


def test_countdowns_age_and_percentages_hold(tmp_path):
    got = _carried(_saved(tmp_path), 1000.0 + 30 * 60)
    assert (got["s"], got["sr"], got["w"], got["wr"]) == (40, 90, 55, 2970)
    assert got["ok"] is True


def test_session_window_resets_to_zero_once_its_reset_passes(tmp_path):
    limited = dict(PRO, s=100, st="rate_limited")
    got = _carried(_saved(tmp_path, limited), 1000.0 + 121 * 60)
    assert (got["s"], got["sr"], got["st"]) == (0, 0, "allowed")
    assert (got["w"], got["wr"]) == (55, 2879)


def test_unknown_reset_leaves_the_window_alone(tmp_path):
    got = _carried(_saved(tmp_path, dict(PRO, sr=0)), 1000.0 + 600 * 60)
    assert (got["s"], got["sr"]) == (40, 0)


def test_per_send_fields_are_not_replayed(tmp_path):
    got = _carried(_saved(tmp_path), 1060.0)
    assert not {"c", "t", "tf"} & set(got)


def test_too_old_carries_nothing(tmp_path):
    assert _carried(_saved(tmp_path), 1000.0 + mod.MAX_CARRY_S + 1) is None


def test_enterprise_payload_is_carried_untouched(tmp_path):
    ent = {"s": 30, "sr": 500, "w": 0, "wr": 0, "st": "allowed", "acct": "ent", "tp": 40, "pd": 30, "ok": True}
    assert _carried(_saved(tmp_path, ent), 1000.0 + 60 * 60) == ent


def _run_one_cycle(tmp_path, poll_result):
    """One connect_and_run poll cycle; returns (usage writes, payloads handed to write_extras)."""
    client = AsyncMock()
    client.is_connected = True
    writes, extras = [], []

    async def go():
        stop_event = asyncio.Event()

        async def fake_poll_active():
            stop_event.set()              # one poll, then unwind the loop
            return poll_result

        async def cap_write(_uuid, data, response=False):
            writes.append(json.loads(data))

        async def cap_extras(_self, payload):
            extras.append(payload)

        client.write_gatt_char = AsyncMock(side_effect=cap_write)
        with patch.object(mod, "BleakClient", return_value=client), \
             patch.object(mod, "poll_active", new=fake_poll_active), \
             patch.object(mod, "drain_notices", return_value=[]), \
             patch.object(mod, "LAST_USAGE_FILE", tmp_path / "last-usage.json"), \
             patch.object(mod.Session, "write_extras", new=cap_extras), \
             patch.object(mod.Session, "write_live", new=AsyncMock()):
            await mod.connect_and_run(MagicMock(address="AA:BB"), stop_event)

    # carried_usage / save_last_usage bind LAST_USAGE_FILE as a default argument.
    with patch.object(mod.carried_usage, "__defaults__", (tmp_path / "last-usage.json",)), \
         patch.object(mod.save_last_usage, "__defaults__", (tmp_path / "last-usage.json",)):
        asyncio.run(go())
    return writes, extras


def test_dead_token_resends_last_usage_and_extras(tmp_path):
    fresh = {"s": 12, "sr": 200, "w": 3, "wr": 9000, "st": "allowed", "acct": "pro", "ok": True}
    _run_one_cycle(tmp_path, (fresh, False))          # a good cycle seeds the file
    writes, extras = _run_one_cycle(tmp_path, (None, True))
    assert {"ok": False} not in writes
    assert writes and writes[0]["ok"] is True and writes[0]["s"] == 12
    assert extras, "the other screens must keep updating while the token is dead"


def test_dead_token_with_nothing_to_carry_still_signals_no_data(tmp_path):
    writes, extras = _run_one_cycle(tmp_path, (None, True))
    assert writes == [{"ok": False}]
    assert not extras
