import asyncio
import colorsys
from datetime import datetime, timezone
from fastapi import APIRouter
from pydantic import BaseModel

router = APIRouter(prefix="/cycle")

# State management
_cycling = False
_current_hue = 0.0  # 0-360 degrees
_step_size = 1.0  # degrees per second
_brightness = 255  # 0-255
_last_update = None
_cycle_task = None


class CycleConfig(BaseModel):
    step_size: float  # degrees per second


class CycleStatus(BaseModel):
    cycling: bool
    current_hue: float
    step_size: float
    brightness: int  # 0-255


def _hue_to_rgb(hue: float, brightness: int = 255, saturation: float = 1.0, lightness: float = 0.5) -> dict:
    """Convert HSL to RGB (0-255 range) with brightness scaling."""
    # Normalize hue to 0-1 range
    h = (hue % 360) / 360.0
    # colorsys uses 0-1 for all values
    r, g, b = colorsys.hls_to_rgb(h, lightness, saturation)
    # Convert to 0-255 range and apply brightness scaling
    brightness_factor = brightness / 255.0
    return {
        "r": int(round(r * 255 * brightness_factor)),
        "g": int(round(g * 255 * brightness_factor)),
        "b": int(round(b * 255 * brightness_factor)),
        "w": 0,
        "flicker": False,
    }


async def _cycle_loop():
    """Background task that cycles the hue."""
    global _current_hue, _last_update
    from app.mqtt import publish
    from app.clients import _clients
    from app.db import get_hue_offset
    import json

    while _cycling:
        try:
            now = datetime.now(timezone.utc)
            if _last_update:
                elapsed = (now - _last_update).total_seconds()
                _current_hue = (_current_hue + _step_size * elapsed) % 360.0

            _last_update = now

            # Publish to all connected devices with per-device offset
            for mac in _clients:
                offset = get_hue_offset(mac)
                device_hue = (_current_hue + offset) % 360.0
                color = _hue_to_rgb(device_hue, _brightness)
                # Publish as JSON string
                await publish(f"devices/{mac}/color", json.dumps(color))

            # Update interval: check every 100ms for smooth updates
            await asyncio.sleep(0.1)
        except Exception as e:
            import traceback
            print(f"Error in cycle loop: {e}")
            traceback.print_exc()
            await asyncio.sleep(0.1)


@router.post("/start")
async def start_cycle(config: CycleConfig):
    """Start the color wheel cycle."""
    global _cycling, _step_size, _last_update, _cycle_task

    if _cycling:
        return CycleStatus(cycling=True, current_hue=_current_hue, step_size=_step_size, brightness=_brightness)

    _step_size = config.step_size
    _cycling = True
    _last_update = datetime.now(timezone.utc)

    # Create background task
    _cycle_task = asyncio.create_task(_cycle_loop())

    return CycleStatus(cycling=True, current_hue=_current_hue, step_size=_step_size, brightness=_brightness)


@router.post("/stop")
async def stop_cycle():
    """Stop the color wheel cycle."""
    global _cycling, _cycle_task

    if not _cycling:
        return CycleStatus(cycling=False, current_hue=_current_hue, step_size=_step_size, brightness=_brightness)

    _cycling = False

    if _cycle_task:
        _cycle_task.cancel()
        try:
            await _cycle_task
        except asyncio.CancelledError:
            pass
        _cycle_task = None

    return CycleStatus(cycling=False, current_hue=_current_hue, step_size=_step_size, brightness=_brightness)


@router.get("/status")
def get_cycle_status() -> CycleStatus:
    """Get current cycling status."""
    return CycleStatus(
        cycling=_cycling,
        current_hue=_current_hue,
        step_size=_step_size,
        brightness=_brightness,
    )


@router.put("/step-size")
def set_step_size(config: CycleConfig):
    """Update the step size."""
    global _step_size
    _step_size = config.step_size
    return {"step_size": _step_size}


class BrightnessConfig(BaseModel):
    brightness: int = 255  # 0-255


@router.put("/brightness")
def set_brightness(config: BrightnessConfig):
    """Update the cycle brightness."""
    global _brightness
    # Clamp brightness to 0-255
    _brightness = max(0, min(255, config.brightness))
    return {"brightness": _brightness}


class DeviceOffset(BaseModel):
    offset: float  # degrees (0-360)


@router.get("/{mac}/offset")
def get_device_offset(mac: str):
    """Get the hue offset for a device."""
    from app.db import get_hue_offset
    offset = get_hue_offset(mac.upper())
    return {"mac": mac.upper(), "offset": offset}


@router.put("/{mac}/offset")
def set_device_offset(mac: str, offset_config: DeviceOffset):
    """Set the hue offset for a device."""
    from app.db import set_hue_offset
    mac = mac.upper()
    # Normalize offset to 0-360 range
    normalized_offset = offset_config.offset % 360.0
    set_hue_offset(mac, normalized_offset)
    return {"mac": mac, "offset": normalized_offset}
