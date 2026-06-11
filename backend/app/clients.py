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

def on_mqtt_heartbeat(mac: str) -> None:
    now = datetime.now(timezone.utc)
    _clients[mac] = now
    from app.db import save_client
    save_client(mac, now)


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
