from peripheral_battery import build_payload, hold_levels, levels_by_kind, parse_power_sources


def test_payload_orders_mouse_then_keyboard():
    assert build_payload({"keyboard": 80, "mouse": 69}) == {"pb": [69, 80]}


def test_payload_marks_unknown_devices():
    assert build_payload({"mouse": 5}) == {"pb": [5, -1]}
    assert build_payload({}) == {"pb": [-1, -1]}


PMSET = """Now drawing from 'AC Power'
 -InternalBattery-0 (id=36503651)\t100%; charged; 0:00 remaining present: true
 -Clawdmeter (id=48043354)\t100%; 
 -AK820 5.1-1 (id=48043359)\t57%; 
 -CORSAIR HARPOON (id=48043360)\t58%; 
"""


def test_parse_power_sources_skips_internal_battery():
    assert parse_power_sources(PMSET) == {"Clawdmeter": 100, "AK820 5.1-1": 57, "CORSAIR HARPOON": 58}


def test_levels_by_kind_skips_own_device():
    kinds = {"Clawdmeter": "keyboard", "AK820 5.1-1": "keyboard", "CORSAIR HARPOON": "mouse"}
    assert levels_by_kind(parse_power_sources(PMSET), kinds, "Clawdmeter") == {"keyboard": 57, "mouse": 58}


def test_hold_levels_keeps_a_device_that_blinks_out():
    seen = {}
    assert hold_levels(seen, {"mouse": 54, "keyboard": 78}, 1000) == {"mouse": 54, "keyboard": 78}
    # Cable goes in: the mouse restarts its radio and is missing for a reading.
    assert hold_levels(seen, {"keyboard": 79}, 1060) == {"mouse": 54, "keyboard": 79}
    assert hold_levels(seen, {"mouse": 56, "keyboard": 79}, 1120) == {"mouse": 56, "keyboard": 79}


def test_hold_levels_drops_a_device_gone_past_the_hold():
    seen = {}
    hold_levels(seen, {"mouse": 54}, 0, hold_s=3600)
    assert hold_levels(seen, {}, 3600, hold_s=3600) == {"mouse": 54}
    assert hold_levels(seen, {}, 3601, hold_s=3600) == {}
