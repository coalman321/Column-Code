#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef void (*mqtt_msg_handler_t)(const char *topic, int topic_len,
                                   const char *data,  int data_len);

/* Register a subscription handler before calling device_mqtt_start().
   The handler is invoked from the MQTT task context when a matching message
   arrives. topic_filter must be a stable (static) string. */
void device_mqtt_subscribe(const char *topic_filter, int qos,
                           mqtt_msg_handler_t handler);

/* Connect to the broker and begin processing messages.
   Automatically reconnects on disconnect (handled by esp_mqtt_client). */
esp_err_t device_mqtt_start(const char *broker_url);

/* Publish payload to topic. Returns ESP_OK if the message was queued. */
esp_err_t device_mqtt_publish(const char *topic, const char *payload,
                              int qos, int retain);

/* Returns true if the client is currently connected to the broker. */
bool device_mqtt_is_connected(void);

/* Stop the MQTT client (cancels reconnect timers) before light sleep,
   then restart it after waking. */
void device_mqtt_suspend(void);
void device_mqtt_resume(void);
