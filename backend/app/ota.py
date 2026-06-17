from fastapi import APIRouter, HTTPException

router = APIRouter(prefix="/ota")


@router.post("/{mac}")
async def trigger_ota(mac: str):
    """Trigger OTA update on a device via MQTT."""
    mac = mac.upper()

    from app.mqtt import publish
    topic = f"devices/{mac}/ota"
    payload = {"trigger": True}

    try:
        await publish(topic, payload, qos=1, retain=False)
        return {"status": "OTA update triggered", "mac": mac, "topic": topic}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to publish OTA trigger: {str(e)}")
