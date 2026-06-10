#include "remote_log.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "remote_log";

static char s_log_url[192];
static char s_mac[18];   /* "AA:BB:CC:DD:EE:FF\0" */
static bool s_ready = false;

void remote_log_init(const char *server_base_url)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_mac, sizeof(s_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(s_log_url, sizeof(s_log_url), "%s/logs/", server_base_url);
    s_ready = true;

    ESP_LOGI(TAG, "remote logging ready (mac=%s, url=%s)", s_mac, s_log_url);
}

void remote_log_post(const char *level, const char *tag, const char *message)
{
    if (!s_ready) {
        return;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "mac",     s_mac);
    cJSON_AddStringToObject(body, "level",   level);
    cJSON_AddStringToObject(body, "tag",     tag);
    cJSON_AddStringToObject(body, "message", message);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (!json) {
        return;
    }

    esp_http_client_config_t cfg = {
        .url        = s_log_url,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "log post failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json);
}
