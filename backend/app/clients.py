from datetime import datetime, timezone, timedelta

from fastapi import APIRouter
from pydantic import BaseModel

router = APIRouter(prefix="/clients")

# A client is considered offline if no heartbeat has been received within this window.
OFFLINE_AFTER = timedelta(seconds=120)

_clients: dict[str, datetime] = {}  # mac -> last_seen (UTC)


class HeartbeatPayload(BaseModel):
    mac: str


@router.post("/heartbeat", status_code=204)
def heartbeat(payload: HeartbeatPayload) -> None:
    _clients[payload.mac] = datetime.now(timezone.utc)


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
