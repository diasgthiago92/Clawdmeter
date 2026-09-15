from peripheral_battery import build_payload


def test_payload_orders_mouse_then_keyboard():
    assert build_payload({"keyboard": 80, "mouse": 69}) == {"pb": [69, 80]}


def test_payload_marks_unknown_devices():
    assert build_payload({"mouse": 5}) == {"pb": [5, -1]}
    assert build_payload({}) == {"pb": [-1, -1]}
