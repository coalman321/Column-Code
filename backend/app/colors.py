from fastapi import APIRouter
from pydantic import BaseModel, Field

router = APIRouter(prefix="/colors")

_store: dict[str, dict] = {}

_DEFAULT = {"r": 0, "g": 0, "b": 0, "w": 0}


class RGBWColor(BaseModel):
    r: int = Field(0, ge=0, le=255)
    g: int = Field(0, ge=0, le=255)
    b: int = Field(0, ge=0, le=255)
    w: int = Field(0, ge=0, le=255)


@router.get("/{mac}")
def get_color(mac: str):
    return _store.get(mac.upper(), dict(_DEFAULT))


@router.put("/{mac}")
def set_color(mac: str, color: RGBWColor):
    _store[mac.upper()] = color.model_dump()
    return _store[mac.upper()]
