#include "heartbeat.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "device_mac.h"

static const char *TAG = "heartbeat";

static char s_url[192];
static char s_mac[18];
static bool s_ready = false;

void heartbeat_init(const char *server_base_url)
{
    device_mac_get_str(s_mac);
    snprintf(s_url, sizeof(s_url), "%s/clients/heartbeat", server_base_url);
    s_ready = true;

    ESP_LOGI(TAG, "heartbeat ready (mac=%s)", s_mac);
}

esp_err_t heartbeat_send(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "mac", s_mac);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t cfg = {
        .url        = s_url,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(json);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "server returned HTTP %d", status);
        return ESP_FAIL;
    }

    return ESP_OK;
}
