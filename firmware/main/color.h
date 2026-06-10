#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * Initialise LEDC PWM channels for R/G/B/W and cache the server URL + MAC.
 * Must be called after WiFi is connected.
 */
esp_err_t color_init(const char *server_base_url);

/** Drive the four LEDC channels directly (values 0-255). */
esp_err_t color_apply(uint8_t r, uint8_t g, uint8_t b, uint8_t w);

/** GET /colors/{mac} from the server and apply the result via color_apply(). */
esp_err_t color_fetch_and_apply(void);
