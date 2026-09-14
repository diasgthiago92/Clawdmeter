"""Wi-Fi credentials for the device's standalone mode.

The user writes ~/.config/claude-usage-monitor/wifi.json:

    {"ssid": "MinhaRede", "password": "..."}

and the daemon hands it to the device once per connection (and again if the
file changes) as {"wifi": [ssid, password]} over the bonded, encrypted BLE
link. The device stores it and, when the Mac goes quiet, fetches quotes and
Vasco fixtures by itself. The password is never written to the daemon log.
"""

import json
from pathlib import Path

WIFI_FILE = Path.home() / ".config" / "claude-usage-monitor" / "wifi.json"


def load_wifi(path: Path = WIFI_FILE) -> tuple[str, str] | None:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    ssid, password = data.get("ssid"), data.get("password", "")
    if not isinstance(ssid, str) or not ssid or not isinstance(password, str):
        return None
    if len(ssid.encode()) > 32 or len(password.encode()) > 63:
        return None
    return ssid, password


def masked(payload: dict) -> str:
    """Log-safe rendering of a wifi payload."""
    ssid = payload.get("wifi", ["?"])[0]
    return json.dumps({"wifi": [ssid, "********"]}, ensure_ascii=False)
