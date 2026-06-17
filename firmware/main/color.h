#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Set up the RMT/SK6812 driver and register the MQTT color subscription.
   Must be called before device_mqtt_start(). */
esp_err_t color_init(void);

/* Drive the SK6812 LEDs with the given RGBW values (0-255 each).
   If flicker is true, the brightness will oscillate to simulate candlelight. */
esp_err_t color_apply(uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool flicker);

/* Temporarily disable flicker (useful during OTA updates). */
void color_disable_flicker(void);
