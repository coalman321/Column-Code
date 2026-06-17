#pragma once

#include "esp_err.h"

/* Check server for new firmware and update if available. */
esp_err_t ota_check_and_update(const char *server_base_url);

/* Initialize MQTT subscription for OTA update requests.
   Must be called before device_mqtt_start(). */
esp_err_t ota_init_mqtt(const char *server_base_url);

/* Manually trigger an OTA update check and download. */
esp_err_t ota_trigger_update(void);
