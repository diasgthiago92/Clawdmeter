"""Battery of the mouse and keyboard, for the fixed corner on the device.

macOS doesn't show the battery of most third-party Bluetooth LE devices, but
they still expose the standard Battery Service (0x180F, level 0x2A19). The
daemon already holds a CoreBluetooth central manager, so it reads that
characteristic from system-connected HID peripherals, riding the existing link
the way it does for the Clawdmeter itself.

Mouse or keyboard comes from System Information's "Minor Type" for that name.
Devices on a USB 2.4G receiver aren't reachable this way and stay unknown.

Sent as {"pb": [mouse %, keyboard %]}, -1 = unknown.
"""

import asyncio
import json
import subprocess
import time

BATTERY_SERVICE = "180F"
HID_SERVICE = "1812"
BATTERY_LEVEL = "00002a19-0000-1000-8000-00805f9b34fb"
REFRESH_S = 5 * 60
READ_TIMEOUT_S = 15


def bluetooth_kinds() -> dict[str, str]:
    """{device name: "mouse" | "keyboard"} from System Information."""
    try:
        out = subprocess.run(["/usr/sbin/system_profiler", "SPBluetoothDataType", "-json"],
                             capture_output=True, text=True, timeout=30).stdout
        data = json.loads(out)
    except (OSError, subprocess.SubprocessError, ValueError):
        return {}
    kinds = {}
    for controller in data.get("SPBluetoothDataType", []):
        for section in ("device_connected", "device_not_connected"):
            for entry in controller.get(section, []) or []:
                for name, info in (entry or {}).items():
                    kind = str((info or {}).get("device_minorType", "")).lower()
                    if kind in ("mouse", "keyboard"):
                        kinds[name] = kind
    return kinds


def build_payload(levels: dict[str, int]) -> dict:
    return {"pb": [levels.get("mouse", -1), levels.get("keyboard", -1)]}


class PeripheralBattery:
    def __init__(self, own_name: str) -> None:
        self.own_name = own_name
        self.levels: dict[str, int] = {}
        self.read_at = 0.0

    async def get(self, manager, now: float | None = None) -> dict:
        now = time.time() if now is None else now
        if now - self.read_at >= REFRESH_S:
            self.read_at = now
            self.levels = await self._read(manager)
        return build_payload(self.levels)

    async def _read(self, manager) -> dict[str, int]:
        from CoreBluetooth import CBUUID
        from bleak import BleakClient
        from bleak.backends.device import BLEDevice

        kinds = await asyncio.to_thread(bluetooth_kinds)
        services = [CBUUID.UUIDWithString_(BATTERY_SERVICE), CBUUID.UUIDWithString_(HID_SERVICE)]
        levels: dict[str, int] = {}
        for p in manager.central_manager.retrieveConnectedPeripheralsWithServices_(services) or []:
            name = p.name()
            kind = kinds.get(name)
            if not name or name == self.own_name or not kind or kind in levels:
                continue
            addr = p.identifier().UUIDString()
            try:
                async with BleakClient(BLEDevice(addr, name, (p, manager)), timeout=READ_TIMEOUT_S) as client:
                    value = await client.read_gatt_char(BATTERY_LEVEL)
                levels[kind] = max(0, min(100, int(value[0])))
            except Exception:   # device asleep, no battery service, link busy
                continue
        return levels
