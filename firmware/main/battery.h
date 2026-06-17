#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t voltage_mv;  /* Battery voltage in millivolts */
    uint8_t soc;          /* State of charge percentage (0-100) */
} BatteryStatus;

/* Initialize I2C and battery monitor. */
esp_err_t battery_init(void);

/* Read current battery status from MAX17048. */
esp_err_t battery_read(BatteryStatus *status);
