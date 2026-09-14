from daemon.wifi_config import load_wifi, masked


def test_load_wifi(tmp_path):
    f = tmp_path / "wifi.json"
    f.write_text('{"ssid": "Casa", "password": "segredo123"}')
    assert load_wifi(f) == ("Casa", "segredo123")
    f.write_text('{"ssid": ""}')
    assert load_wifi(f) is None
    assert load_wifi(tmp_path / "missing.json") is None


def test_masked_hides_password():
    text = masked({"wifi": ["Casa", "segredo123"]})
    assert "Casa" in text and "segredo123" not in text
