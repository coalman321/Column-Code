#pragma once

/* Cache the device MAC and set the log MQTT topic.
   Must be called before device_mqtt_start(). */
void remote_log_init(void);

/* Publish a log entry to devices/{mac}/logs via MQTT (QoS 0, fire-and-forget). */
void remote_log_post(const char *level, const char *tag, const char *message);
