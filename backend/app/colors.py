from fastapi import APIRouter
from pydantic import BaseModel, Field

router = APIRouter(prefix="/colors")

_store: dict[str, dict] = {}
_last_color: dict | None = None   # most recently set color, pushed to new devices

_DEFAULT = {"r": 0, "g": 0, "b": 0, "w": 0, "flicker": False}


def get_last_color() -> dict | None:
    return _last_color


def load_from_db() -> None:
    """Populate in-memory store from the database on startup."""
    global _last_color
    from app.db import load_all_colors
    rows = load_all_colors()
    for row in rows:
        mac = row["mac"]
        _store[mac] = {
            "r": row["r"],
            "g": row["g"],
            "b": row["b"],
            "w": row["w"],
            "flicker": bool(row["flicker"])
        }
    if rows:
        latest = max(rows, key=lambda r: r["last_updated"])
        _last_color = {
            "r": latest["r"],
            "g": latest["g"],
            "b": latest["b"],
            "w": latest["w"],
            "flicker": bool(latest["flicker"])
        }


def remove_from_store(mac: str) -> None:
    _store.pop(mac, None)


class RGBWColor(BaseModel):
    r: int = Field(0, ge=0, le=255)
    g: int = Field(0, ge=0, le=255)
    b: int = Field(0, ge=0, le=255)
    w: int = Field(0, ge=0, le=255)
    flicker: bool = Field(False)


@router.get("/{mac}")
def get_color(mac: str):
    return _store.get(mac.upper(), dict(_DEFAULT))


@router.put("/{mac}")
async def set_color(mac: str, color: RGBWColor):
    global _last_color
    mac = mac.upper()
    _store[mac] = color.model_dump()
    _last_color = _store[mac]
    from app.db import save_color
    save_color(mac, color.r, color.g, color.b, color.w, color.flicker)
    from app.mqtt import publish
    await publish(f"devices/{mac}/color", _store[mac], retain=True)
    return _store[mac]
