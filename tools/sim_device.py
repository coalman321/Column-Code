#!/usr/bin/env python3
"""
Simulated ESP32 firmware device for testing the Column backend and UI.

Each simulated device runs a heartbeat loop and a log-posting loop in
separate threads, mirroring what the real firmware does.

Usage:
    python tools/sim_device.py                         # one device, defaults
    python tools/sim_device.py --devices 3             # three devices
    python tools/sim_device.py --server http://host:8000
    python tools/sim_device.py --mac AA:BB:CC:DD:EE:FF # fixed MAC
    python tools/sim_device.py --heartbeat 10 --logs 3
"""

import argparse
import json
import random
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime

# ── ANSI colours ──────────────────────────────────────────────────────────────
RESET  = "\033[0m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RED    = "\033[91m"
YELLOW = "\033[93m"
GREEN  = "\033[92m"
CYAN   = "\033[96m"
BLUE   = "\033[94m"
WHITE  = "\033[97m"

LEVEL_COLOR = {
    "DEBUG": DIM + WHITE,
    "INFO":  BLUE,
    "WARN":  YELLOW,
    "ERROR": RED,
}

# ── Fake log pool ─────────────────────────────────────────────────────────────
_LOG_POOL = [
    ("INFO",  "main",      "System initialised"),
    ("INFO",  "wifi",      "Connected to AP, RSSI: -{rssi} dBm"),
    ("INFO",  "ota",       "Firmware up to date"),
    ("INFO",  "heartbeat", "Heartbeat acknowledged"),
    ("INFO",  "sensor",    "Temperature: {temp:.1f} C  Humidity: {hum:.0f}%"),
    ("DEBUG", "main",      "Free heap: {heap} bytes"),
    ("DEBUG", "wifi",      "TX power: {txpow} dBm"),
    ("WARN",  "wifi",      "RSSI below threshold: -{rssi} dBm"),
    ("WARN",  "ota",       "OTA check failed, will retry"),
    ("WARN",  "main",      "Heap fragmentation: {frag}%"),
    ("ERROR", "wifi",      "Connection lost, reconnecting…"),
    ("ERROR", "ota",       "OTA download failed: timeout"),
    ("ERROR", "main",      "Sensor read error: I2C NACK"),
]

def _color_swatch(r: int, g: int, b: int) -> str:
    """Return a small coloured block using 24-bit ANSI background colour."""
    return f"\033[48;2;{r};{g};{b}m   \033[0m"


def _render_message(template: str) -> str:
    return template.format(
        rssi=random.randint(40, 90),
        temp=random.uniform(18.0, 35.0),
        hum=random.uniform(30, 80),
        heap=random.randint(180_000, 280_000),
        txpow=random.randint(16, 20),
        frag=random.randint(5, 40),
    )


# ── Helpers ───────────────────────────────────────────────────────────────────
def random_mac() -> str:
    octets = [random.randint(0, 255) for _ in range(6)]
    octets[0] = (octets[0] & 0xFE) | 0x02   # locally administered, unicast
    return ":".join(f"{b:02X}" for b in octets)


def _now() -> str:
    return datetime.now().strftime("%H:%M:%S")


def _log(mac: str, text: str) -> None:
    print(f"  {DIM}{_now()}{RESET}  {CYAN}{mac}{RESET}  {text}")


def post_json(url: str, payload: dict, timeout: int = 5) -> int:
    data = json.dumps(payload).encode()
    req = urllib.request.Request(
        url, data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status


def get_json(url: str, timeout: int = 5) -> dict:
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


# ── Simulated device ──────────────────────────────────────────────────────────
class SimDevice:
    def __init__(
        self,
        mac: str,
        server: str,
        heartbeat_interval: float,
        log_interval: float,
        color_interval: float,
    ):
        self.mac = mac
        self.server = server.rstrip("/")
        self.heartbeat_interval = heartbeat_interval
        self.log_interval = log_interval
        self.color_interval = color_interval
        self._stop = threading.Event()
        self._threads: list[threading.Thread] = []

    def _heartbeat_loop(self) -> None:
        while not self._stop.is_set():
            try:
                status = post_json(f"{self.server}/clients/heartbeat", {"mac": self.mac})
                _log(self.mac, f"{GREEN}heartbeat{RESET} → {status}")
            except Exception as exc:
                _log(self.mac, f"{RED}heartbeat failed:{RESET} {exc}")
            self._stop.wait(self.heartbeat_interval)

    def _log_loop(self) -> None:
        while not self._stop.is_set():
            level, tag, template = random.choice(_LOG_POOL)
            message = _render_message(template)
            try:
                post_json(f"{self.server}/logs/", {
                    "mac":     self.mac,
                    "level":   level,
                    "tag":     tag,
                    "message": message,
                })
                color = LEVEL_COLOR.get(level, WHITE)
                _log(self.mac, f"{color}{level:<5}{RESET}  [{tag}] {message}")
            except Exception as exc:
                _log(self.mac, f"{RED}log post failed:{RESET} {exc}")
            self._stop.wait(self.log_interval)

    def _color_loop(self) -> None:
        while not self._stop.is_set():
            try:
                color = get_json(f"{self.server}/colors/{self.mac}")
                r, g, b, w = color["r"], color["g"], color["b"], color["w"]
                swatch = _color_swatch(r, g, b)
                _log(self.mac, f"{CYAN}color{RESET}     {swatch}  "
                               f"R={r:3d} G={g:3d} B={b:3d} W={w:3d}")
            except Exception as exc:
                _log(self.mac, f"{RED}color fetch failed:{RESET} {exc}")
            self._stop.wait(self.color_interval)

    def start(self) -> None:
        for target in (self._heartbeat_loop, self._log_loop, self._color_loop):
            t = threading.Thread(target=target, daemon=True)
            t.start()
            self._threads.append(t)

    def stop(self) -> None:
        self._stop.set()


# ── Entry point ───────────────────────────────────────────────────────────────
def main() -> None:
    parser = argparse.ArgumentParser(description="Simulate ESP32 firmware devices")
    parser.add_argument("--server",    default="http://127.0.0.1:8000",
                        help="Backend base URL (default: http://127.0.0.1:8000)")
    parser.add_argument("--devices",   type=int, default=1,
                        help="Number of devices to simulate (default: 1)")
    parser.add_argument("--mac",       default=None,
                        help="Fixed MAC for the first device (random if omitted)")
    parser.add_argument("--heartbeat", type=float, default=30.0,
                        help="Heartbeat interval in seconds (default: 30)")
    parser.add_argument("--logs",      type=float, default=5.0,
                        help="Log post interval in seconds (default: 5)")
    parser.add_argument("--color",     type=float, default=5.0,
                        help="Color poll interval in seconds (default: 5)")
    args = parser.parse_args()

    macs = []
    for i in range(args.devices):
        if i == 0 and args.mac:
            macs.append(args.mac.upper())
        else:
            macs.append(random_mac())

    print(f"\n{BOLD}Column device simulator{RESET}")
    print(f"  server    : {args.server}")
    print(f"  devices   : {args.devices}")
    print(f"  heartbeat : every {args.heartbeat}s")
    print(f"  logs      : every {args.logs}s")
    print(f"  color     : every {args.color}s")
    print(f"  MACs      : {', '.join(macs)}")
    print(f"\n{DIM}Press Ctrl-C to stop{RESET}\n")

    devices = [
        SimDevice(mac, args.server, args.heartbeat, args.logs, args.color)
        for mac in macs
    ]

    # Stagger start times slightly so output isn't perfectly synchronised.
    for i, device in enumerate(devices):
        time.sleep(i * 0.2)
        device.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"\n{DIM}Stopping…{RESET}")
        for device in devices:
            device.stop()
        sys.exit(0)


if __name__ == "__main__":
    main()
