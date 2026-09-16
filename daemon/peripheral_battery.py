"""Battery of the mouse and keyboard, for the fixed corner on the device.

Two sources, in order:
1. macOS power sources (`pmset -g accps`): the level the OS itself reads over
   the Bluetooth HID link. Some keyboards (e.g. AK820) answer the GATT battery
   characteristic with a frozen value, but report the real level here.
2. GATT Battery Service, for devices macOS doesn't list.

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
import re
import subprocess
import time

BATTERY_SERVICE = "180F"
HID_SERVICE = "1812"
BATTERY_LEVEL = "00002a19-0000-1000-8000-00805f9b34fb"
REFRESH_S = 60          # macOS power sources: cheap, read every minute
GATT_REFRESH_S = 5 * 60 # GATT fallback opens connections, read less often
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


_PMSET_LINE = re.compile(r"^\s*-(.+?) \(id=\d+\)\s+(\d+)%")


def parse_power_sources(text: str) -> dict[str, int]:
    """{device name: %} from `pmset -g accps` output, internal battery excluded."""
    levels = {}
    for line in text.splitlines():
        m = _PMSET_LINE.match(line)
        if m and not m.group(1).startswith("InternalBattery"):
            levels[m.group(1)] = max(0, min(100, int(m.group(2))))
    return levels


def power_source_levels() -> dict[str, int]:
    try:
        out = subprocess.run(["/usr/bin/pmset", "-g", "accps"],
                             capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return {}
    return parse_power_sources(out)


def levels_by_kind(named: dict[str, int], kinds: dict[str, str], own_name: str) -> dict[str, int]:
    """{"mouse" | "keyboard": %} from {device name: %}; first device of each kind wins."""
    levels: dict[str, int] = {}
    for name, pct in named.items():
        kind = kinds.get(name)
        if name != own_name and kind and kind not in levels:
            levels[kind] = pct
    return levels


def build_payload(levels: dict[str, int]) -> dict:
    return {"pb": [levels.get("mouse", -1), levels.get("keyboard", -1)]}


class PeripheralBattery:
    def __init__(self, own_name: str) -> None:
        self.own_name = own_name
        self.levels: dict[str, int] = {}
        self.gatt_levels: dict[str, int] = {}
        self.read_at = 0.0
        self.gatt_read_at = 0.0

    async def get(self, manager, now: float | None = None) -> dict:
        now = time.time() if now is None else now
        if now - self.read_at >= REFRESH_S:
            self.read_at = now
            kinds = await asyncio.to_thread(bluetooth_kinds)
            os_levels = levels_by_kind(await asyncio.to_thread(power_source_levels), kinds, self.own_name)
            missing = {"mouse", "keyboard"} - os_levels.keys()
            if missing and now - self.gatt_read_at >= GATT_REFRESH_S:
                self.gatt_read_at = now
                self.gatt_levels = await self._read(manager, kinds)
            self.levels = {**{k: v for k, v in self.gatt_levels.items() if k in missing}, **os_levels}
        return build_payload(self.levels)

    async def _read(self, manager, kinds: dict[str, str]) -> dict[str, int]:
        from CoreBluetooth import CBUUID
        from bleak import BleakClient
        from bleak.backends.device import BLEDevice

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
