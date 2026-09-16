from peripheral_battery import build_payload, levels_by_kind, parse_power_sources


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
