import json
from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, Query

router = APIRouter(prefix="/logs")

_store: list[dict] = []
_MAX_ENTRIES = 500


# ── MQTT callback (called from mqtt.py dispatcher) ────────────────────────

def on_mqtt_log(mac: str, data: str) -> None:
    try:
        payload = json.loads(data)
    except Exception:
        payload = {}
    received_at = datetime.now(timezone.utc).isoformat()
    _store.append({
        "mac":         mac,
        "level":       payload.get("level", "INFO").upper(),
        "tag":         payload.get("tag", ""),
        "message":     payload.get("message", data),
        "received_at": received_at,
    })
    if len(_store) > _MAX_ENTRIES:
        del _store[: len(_store) - _MAX_ENTRIES]


# ── HTTP endpoint (frontend-facing) ──────────────────────────────────────

@router.get("/")
def get_logs(
    mac: Optional[str] = Query(None, description="Filter by device MAC address"),
    limit: int = Query(100, ge=1, le=1000),
):
    results = _store if mac is None else [e for e in _store if e["mac"] == mac.upper()]
    return results[-limit:]
