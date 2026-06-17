import json
from datetime import datetime, timezone, timedelta

from fastapi import APIRouter, HTTPException

router = APIRouter(prefix="/clients")

OFFLINE_AFTER = timedelta(seconds=5)

_clients: dict[str, datetime] = {}   # mac -> last_seen (UTC)


def load_from_db() -> None:
    from app.db import load_all_clients
    for row in load_all_clients():
        _clients[row["mac"]] = datetime.fromisoformat(row["last_seen"])


# ── MQTT callbacks (called from mqtt.py dispatcher) ───────────────────────

def on_mqtt_heartbeat(mac: str, data: str = "") -> None:
    now = datetime.now(timezone.utc)
    _clients[mac] = now
    from app.db import save_client, save_battery_status, save_firmware_version
    save_client(mac, now)

    # Parse battery data and firmware version from heartbeat
    if data:
        try:
            payload = json.loads(data)
            if "battery_percent" in payload and "battery_mv" in payload:
                save_battery_status(mac, payload["battery_percent"], payload["battery_mv"])
            if "firmware_version" in payload:
                save_firmware_version(mac, payload["firmware_version"])
        except Exception:
            pass


async def on_mqtt_status(mac: str, data: str) -> None:
    try:
        payload = json.loads(data)
    except Exception:
        return
    if not payload.get("connected"):
        return
    is_new = mac not in _clients
    now = datetime.now(timezone.utc)
    _clients[mac] = now
    from app.db import save_client
    save_client(mac, now)
    if is_new:
        from app.colors import get_last_color
        from app.mqtt import publish
        last = get_last_color()
        if last is not None:
            await publish(f"devices/{mac}/color", last, retain=True)


# ── HTTP endpoints (frontend-facing) ─────────────────────────────────────

@router.get("/battery/{mac}")
def get_battery(mac: str):
    """Get battery status for a device."""
    from app.db import load_battery_status
    status = load_battery_status(mac.upper())
    if not status:
        raise HTTPException(status_code=404, detail="Battery status not found")
    return status


@router.get("/name/{mac}")
def get_display_name(mac: str):
    """Get the display name for a device."""
    from app.db import get_display_name
    mac = mac.upper()
    name = get_display_name(mac)
    return {"mac": mac, "display_name": name}


@router.put("/name/{mac}")
def set_display_name(mac: str, body: dict):
    """Set or clear the display name for a device."""
    from app.db import set_display_name
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    name = body.get("name")
    set_display_name(mac, name)
    return {"mac": mac, "display_name": name}


@router.post("/blink/{mac}", status_code=204)
async def request_blink(mac: str) -> None:
    """Send a blink request to a device (rapid green flash for 5 seconds)."""
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    from app.mqtt import publish
    await publish(f"devices/{mac}/cmd", {"blink": True})


@router.post("/sleep/{mac}", status_code=204)
async def request_sleep(mac: str) -> None:
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    from app.mqtt import publish
    await publish(f"devices/{mac}/cmd", {"sleep": True})


@router.delete("/{mac}", status_code=204)
def delete_client(mac: str) -> None:
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    del _clients[mac]
    from app.colors import remove_from_store
    from app.db import delete_color, delete_client as db_delete_client
    remove_from_store(mac)
    delete_color(mac)
    db_delete_client(mac)


@router.get("/")
@router.get("")
def list_clients():
    """List all devices (lightweight - just display name and connection status)."""
    from app.db import get_display_name
    now = datetime.now(timezone.utc)
    devices = []
    for mac, ts in sorted(_clients.items()):
        display_name = get_display_name(mac)
        devices.append({
            "name": display_name or mac,  # use display_name if set, otherwise MAC
            "connected": (now - ts) < OFFLINE_AFTER,
        })
    return devices


def _find_mac_by_name(name: str) -> str | None:
    """Find MAC by display_name or return MAC if exact match."""
    from app.db import get_display_name
    name_upper = name.upper()

    # Check if it's a direct MAC match
    if name_upper in _clients:
        return name_upper

    # Check if it's a display_name match
    for mac in _clients:
        if get_display_name(mac) and get_display_name(mac).lower() == name.lower():
            return mac

    return None


@router.get("/{name}")
def get_device_info(name: str):
    """Get full device info by display_name or MAC."""
    from app.db import get_display_name, load_battery_status, get_firmware_version
    from app.colors import get_color

    mac = _find_mac_by_name(name)
    if not mac:
        raise HTTPException(status_code=404, detail="Device not found")

    now = datetime.now(timezone.utc)
    ts = _clients[mac]
    batt = load_battery_status(mac)
    color = get_color(mac)
    fw_version = get_firmware_version(mac)

    return {
        "mac": mac,
        "display_name": get_display_name(mac),
        "connected": (now - ts) < OFFLINE_AFTER,
        "last_seen": ts.isoformat(),
        "battery_percent": batt["battery_percent"] if batt else None,
        "battery_mv": batt["battery_mv"] if batt else None,
        "color": color,
        "firmware_version": fw_version,
    }
