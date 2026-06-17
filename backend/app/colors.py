from fastapi import APIRouter
from pydantic import BaseModel, Field

router = APIRouter(prefix="/colors")

_store: dict[str, dict] = {}
_last_color: dict | None = None   # most recently set color, pushed to new devices

_DEFAULT = {"r": 0, "g": 0, "b": 0, "w": 0, "flicker": False}


def get_last_color() -> dict | None:
    return _last_color


_global_color: dict | None = None


def load_from_db() -> None:
    """Populate in-memory store from the database on startup."""
    global _last_color, _global_color
    from app.db import load_all_colors, load_global_color
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

    # Load global color from database
    _global_color = load_global_color()
    if _global_color is None:
        _global_color = dict(_DEFAULT)


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


@router.get("/global/current")
def get_global_color():
    """Get the current global color."""
    return _global_color or dict(_DEFAULT)


@router.put("/global/current")
async def set_global_color(color: RGBWColor):
    """Set the global color and persist it."""
    global _global_color
    _global_color = color.model_dump()
    from app.db import save_global_color
    save_global_color(color.r, color.g, color.b, color.w, color.flicker)
    return _global_color


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
