#include "remote_log.h"

#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"
#include "device_mac.h"
#include "device_mqtt.h"

static const char *TAG = "remote_log";

static char s_log_topic[64];   /* "devices/{mac}/logs" */
static char s_mac[DEVICE_MAC_STR_LEN];
static bool s_ready = false;

void remote_log_init(void)
{
    device_mac_get_str(s_mac);
    snprintf(s_log_topic, sizeof(s_log_topic), "devices/%s/logs", s_mac);
    s_ready = true;
    ESP_LOGI(TAG, "remote logging ready (mac=%s)", s_mac);
}

void remote_log_post(const char *level, const char *tag, const char *message)
{
    if (!s_ready) return;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "level",   level);
    cJSON_AddStringToObject(body, "tag",     tag);
    cJSON_AddStringToObject(body, "message", message);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return;

    esp_err_t err = device_mqtt_publish(s_log_topic, json, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "log publish failed");
    }
    free(json);
}
