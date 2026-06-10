#!/usr/bin/env python3
"""
Simulated ESP32 firmware device for testing the Column backend and UI.

Each simulated device connects to the MQTT broker, mirrors the real firmware
behaviour: publishes heartbeats and logs, subscribes to color and command
topics, and sets a Last-Will so the broker marks it offline on disconnect.

Usage:
    python tools/sim_device.py                              # one device, defaults
    python tools/sim_device.py --devices 3                  # three devices
    python tools/sim_device.py --broker 192.168.1.100:1883  # explicit broker
    python tools/sim_device.py --mac AA:BB:CC:DD:EE:FF      # fixed MAC
    python tools/sim_device.py --heartbeat 10 --logs 3
"""

import argparse
import json
import random
import sys
import threading
import time
from datetime import datetime

import paho.mqtt.client as mqtt

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
    ("INFO",  "heartbeat", "Heartbeat sent"),
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


def parse_broker(broker_str: str) -> tuple[str, int]:
    """Accept 'host', 'host:port', or 'mqtt://host:port'."""
    s = broker_str.removeprefix("mqtt://")
    if ":" in s:
        host, port = s.rsplit(":", 1)
        return host, int(port)
    return s, 1883


# ── Simulated device ──────────────────────────────────────────────────────────
class SimDevice:
    def __init__(
        self,
        mac: str,
        broker_host: str,
        broker_port: int,
        heartbeat_interval: float,
        log_interval: float,
    ):
        self.mac = mac
        self.heartbeat_interval = heartbeat_interval
        self.log_interval = log_interval
        self._stop = threading.Event()
        self._threads: list[threading.Thread] = []

        self._t_heartbeat = f"devices/{mac}/heartbeat"
        self._t_logs      = f"devices/{mac}/logs"
        self._t_color     = f"devices/{mac}/color"
        self._t_cmd       = f"devices/{mac}/cmd"
        self._t_status    = f"devices/{mac}/status"

        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"sim-{mac}",
        )
        self._client.will_set(
            self._t_status,
            json.dumps({"connected": False}),
            qos=1, retain=True,
        )
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._client.connect(broker_host, broker_port, keepalive=60)
        self._client.loop_start()

    # ── MQTT callbacks ────────────────────────────────────────────────────────

    def _on_connect(self, client, userdata, connect_flags, reason_code, properties):
        if reason_code.is_failure:
            _log(self.mac, f"{RED}broker connect failed: {reason_code}{RESET}")
            return
        client.publish(self._t_status, json.dumps({"connected": True}),
                       qos=1, retain=True)
        client.subscribe(self._t_color, qos=1)
        client.subscribe(self._t_cmd,   qos=1)
        _log(self.mac, f"{GREEN}connected to broker{RESET}")

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            payload = {}

        if msg.topic == self._t_color:
            r = payload.get("r", 0)
            g = payload.get("g", 0)
            b = payload.get("b", 0)
            w = payload.get("w", 0)
            swatch = _color_swatch(r, g, b)
            _log(self.mac, f"{CYAN}color{RESET}     {swatch}  "
                           f"R={r:3d} G={g:3d} B={b:3d} W={w:3d}")
        elif msg.topic == self._t_cmd:
            if payload.get("sleep"):
                _log(self.mac, f"{YELLOW}cmd{RESET}       sleep requested")

    # ── Background threads ────────────────────────────────────────────────────

    def _heartbeat_loop(self) -> None:
        while not self._stop.is_set():
            self._client.publish(self._t_heartbeat, "{}", qos=1)
            _log(self.mac, f"{GREEN}heartbeat{RESET} →")
            self._stop.wait(self.heartbeat_interval)

    def _log_loop(self) -> None:
        while not self._stop.is_set():
            level, tag, template = random.choice(_LOG_POOL)
            message = _render_message(template)
            self._client.publish(
                self._t_logs,
                json.dumps({"level": level, "tag": tag, "message": message}),
                qos=0,
            )
            color = LEVEL_COLOR.get(level, WHITE)
            _log(self.mac, f"{color}{level:<5}{RESET}  [{tag}] {message}")
            self._stop.wait(self.log_interval)

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    def start(self) -> None:
        for target in (self._heartbeat_loop, self._log_loop):
            t = threading.Thread(target=target, daemon=True)
            t.start()
            self._threads.append(t)

    def stop(self) -> None:
        self._stop.set()
        # Publish a clean offline status before disconnecting.
        self._client.publish(self._t_status, json.dumps({"connected": False}),
                             qos=1, retain=True)
        self._client.disconnect()
        self._client.loop_stop()


# ── Entry point ───────────────────────────────────────────────────────────────
def main() -> None:
    parser = argparse.ArgumentParser(description="Simulate ESP32 firmware devices")
    parser.add_argument("--broker",    default="localhost:1883",
                        help="MQTT broker address, host:port or mqtt://host:port "
                             "(default: localhost:1883)")
    parser.add_argument("--devices",   type=int, default=1,
                        help="Number of devices to simulate (default: 1)")
    parser.add_argument("--mac",       default=None,
                        help="Fixed MAC for the first device (random if omitted)")
    parser.add_argument("--heartbeat", type=float, default=30.0,
                        help="Heartbeat publish interval in seconds (default: 30)")
    parser.add_argument("--logs",      type=float, default=5.0,
                        help="Log publish interval in seconds (default: 5)")
    args = parser.parse_args()

    broker_host, broker_port = parse_broker(args.broker)

    macs = []
    for i in range(args.devices):
        if i == 0 and args.mac:
            macs.append(args.mac.upper())
        else:
            macs.append(random_mac())

    print(f"\n{BOLD}Column device simulator{RESET}")
    print(f"  broker    : {broker_host}:{broker_port}")
    print(f"  devices   : {args.devices}")
    print(f"  heartbeat : every {args.heartbeat}s")
    print(f"  logs      : every {args.logs}s")
    print(f"  MACs      : {', '.join(macs)}")
    print(f"\n{DIM}Press Ctrl-C to stop{RESET}\n")

    devices = [
        SimDevice(mac, broker_host, broker_port, args.heartbeat, args.logs)
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
