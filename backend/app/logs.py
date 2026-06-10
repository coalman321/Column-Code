from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, Query
from pydantic import BaseModel

router = APIRouter(prefix="/logs")

_store: list[dict] = []


class LogEntry(BaseModel):
    mac: str
    level: str = "INFO"
    tag: str = ""
    message: str


@router.post("/", status_code=201)
def post_log(entry: LogEntry):
    received_at = datetime.now(timezone.utc).isoformat()
    _store.append({
        "mac":         entry.mac,
        "level":       entry.level.upper(),
        "tag":         entry.tag,
        "message":     entry.message,
        "received_at": received_at,
    })
    return {"status": "ok", "received_at": received_at}


@router.get("/")
def get_logs(
    mac: Optional[str] = Query(None, description="Filter by device MAC address"),
    limit: int = Query(100, ge=1, le=1000),
):
    results = _store if mac is None else [e for e in _store if e["mac"] == mac]
    return results[-limit:]
