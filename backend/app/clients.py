import json
from datetime import datetime, timezone, timedelta

from fastapi import APIRouter, HTTPException

router = APIRouter(prefix="/clients")

OFFLINE_AFTER = timedelta(seconds=120)

_clients: dict[str, datetime] = {}   # mac -> last_seen (UTC)


# ── MQTT callbacks (called from mqtt.py dispatcher) ───────────────────────

def on_mqtt_heartbeat(mac: str) -> None:
    _clients[mac] = datetime.now(timezone.utc)


async def on_mqtt_status(mac: str, data: str) -> None:
    try:
        payload = json.loads(data)
    except Exception:
        return
    if not payload.get("connected"):
        return
    is_new = mac not in _clients
    _clients[mac] = datetime.now(timezone.utc)
    if is_new:
        from app.colors import get_last_color
        from app.mqtt import publish
        last = get_last_color()
        if last is not None:
            await publish(f"devices/{mac}/color", last, retain=True)


# ── HTTP endpoints (frontend-facing) ─────────────────────────────────────

@router.post("/sleep/{mac}", status_code=204)
async def request_sleep(mac: str) -> None:
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    from app.mqtt import publish
    await publish(f"devices/{mac}/cmd", {"sleep": True})


@router.get("/")
def list_clients():
    now = datetime.now(timezone.utc)
    return [
        {
            "mac":       mac,
            "last_seen": ts.isoformat(),
            "connected": (now - ts) < OFFLINE_AFTER,
        }
        for mac, ts in sorted(_clients.items())
    ]
