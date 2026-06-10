#pragma once

#include <stdint.h>
#include "esp_mac.h"

#define DEVICE_MAC_STR_LEN 18  /* "XX:XX:XX:XX:XX:XX\0" */

/**
 * Fill buf (must be at least DEVICE_MAC_STR_LEN bytes) with the base MAC
 * address formatted as "XX:XX:XX:XX:XX:XX" (uppercase hex, colon-separated).
 */
static inline void device_mac_get_str(char buf[DEVICE_MAC_STR_LEN])
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buf, DEVICE_MAC_STR_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
