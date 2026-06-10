from datetime import datetime, timezone, timedelta

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

router = APIRouter(prefix="/clients")

# A client is considered offline if no heartbeat has been received within this window.
OFFLINE_AFTER = timedelta(seconds=120)

_clients: dict[str, datetime] = {}  # mac -> last_seen (UTC)
_sleep_requested: set[str] = set()  # macs with a pending sleep command


class HeartbeatPayload(BaseModel):
    mac: str


@router.post("/heartbeat")
def heartbeat(payload: HeartbeatPayload):
    mac = payload.mac.upper()
    _clients[mac] = datetime.now(timezone.utc)
    sleep = mac in _sleep_requested
    if sleep:
        _sleep_requested.discard(mac)
    return {"sleep": sleep}


@router.post("/sleep/{mac}", status_code=204)
def request_sleep(mac: str) -> None:
    mac = mac.upper()
    if mac not in _clients:
        raise HTTPException(status_code=404, detail="Unknown client")
    _sleep_requested.add(mac)


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
