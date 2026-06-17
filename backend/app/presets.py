from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

router = APIRouter(prefix="/presets")

_store: dict[int, dict] = {}


def load_from_db() -> None:
    """Populate in-memory store from the database on startup."""
    from app.db import load_all_presets
    rows = load_all_presets()
    for row in rows:
        _store[row["id"]] = {
            "id": row["id"],
            "name": row["name"],
            "r": row["r"],
            "g": row["g"],
            "b": row["b"],
            "w": row["w"],
        }


class Preset(BaseModel):
    id: int
    name: str
    r: int = Field(0, ge=0, le=255)
    g: int = Field(0, ge=0, le=255)
    b: int = Field(0, ge=0, le=255)
    w: int = Field(0, ge=0, le=255)
    flicker: bool = Field(False)


class CreatePresetRequest(BaseModel):
    name: str
    r: int = Field(0, ge=0, le=255)
    g: int = Field(0, ge=0, le=255)
    b: int = Field(0, ge=0, le=255)
    w: int = Field(0, ge=0, le=255)
    flicker: bool = Field(False)


@router.get("")
def list_presets() -> list[Preset]:
    return [Preset(**p) for p in sorted(_store.values(), key=lambda x: x["name"])]


@router.post("")
def create_preset(req: CreatePresetRequest) -> Preset:
    from app.db import save_preset
    try:
        preset_id = save_preset(req.name, req.r, req.g, req.b, req.w, req.flicker)
    except Exception:
        raise HTTPException(status_code=400, detail="Preset name already exists")

    preset = {
        "id": preset_id,
        "name": req.name,
        "r": req.r,
        "g": req.g,
        "b": req.b,
        "w": req.w,
        "flicker": req.flicker,
    }
    _store[preset_id] = preset
    return Preset(**preset)


@router.delete("/{preset_id}", status_code=204)
def delete_preset(preset_id: int) -> None:
    if preset_id not in _store:
        raise HTTPException(status_code=404, detail="Preset not found")
    from app.db import delete_preset as db_delete_preset
    db_delete_preset(preset_id)
    del _store[preset_id]
