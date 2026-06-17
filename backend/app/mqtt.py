import asyncio
import json
import logging
import os

import aiomqtt

logger = logging.getLogger("mqtt")

BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "localhost")
BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))

# Outgoing messages are queued here so any async context can publish
# without holding a reference to the aiomqtt client.
_queue: asyncio.Queue[tuple[str, str, int, bool]] = asyncio.Queue()


async def publish(topic: str, payload: dict | str,
                  qos: int = 1, retain: bool = False) -> None:
    """Queue a message for delivery to the broker."""
    data = json.dumps(payload) if isinstance(payload, dict) else payload
    await _queue.put((topic, data, qos, retain))


async def _publish_loop(client: aiomqtt.Client) -> None:
    while True:
        topic, data, qos, retain = await _queue.get()
        try:
            await client.publish(topic, payload=data, qos=qos, retain=retain)
        except Exception as exc:
            logger.warning("publish failed for %s: %s", topic, exc)


async def _dispatch(message: aiomqtt.Message) -> None:
    topic = str(message.topic)
    parts = topic.split("/")
    if len(parts) != 3 or parts[0] != "devices":
        return
    mac = parts[1].upper()
    subtopic = parts[2]
    try:
        data = message.payload.decode()
    except Exception:
        data = ""

    if subtopic == "heartbeat":
        from app.clients import on_mqtt_heartbeat
        on_mqtt_heartbeat(mac, data)
    elif subtopic == "logs":
        from app.logs import on_mqtt_log
        on_mqtt_log(mac, data)
    elif subtopic == "status":
        from app.clients import on_mqtt_status
        await on_mqtt_status(mac, data)


async def run() -> None:
    """Connect to the broker, subscribe to device topics, and process messages.
    Reconnects automatically on connection loss."""
    reconnect_interval = 5
    while True:
        try:
            async with aiomqtt.Client(BROKER_HOST, port=BROKER_PORT) as client:
                logger.info("Connected to MQTT broker %s:%d", BROKER_HOST, BROKER_PORT)
                await client.subscribe("devices/+/heartbeat", qos=1)
                await client.subscribe("devices/+/logs",      qos=0)
                await client.subscribe("devices/+/status",    qos=1)

                pub_task = asyncio.create_task(_publish_loop(client))
                try:
                    async for message in client.messages:
                        await _dispatch(message)
                finally:
                    pub_task.cancel()

        except aiomqtt.MqttError as exc:
            logger.warning("MQTT error: %s — reconnecting in %ds",
                           exc, reconnect_interval)
            await asyncio.sleep(reconnect_interval)
        except asyncio.CancelledError:
            logger.info("MQTT manager stopped")
            raise
