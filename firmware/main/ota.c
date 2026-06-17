#include "ota.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "device_mqtt.h"
#include "device_mac.h"

static const char *TAG = "ota";
static char s_server_base_url[128] = {0};

/* Accumulate the hash endpoint response (small JSON, 256 B is plenty). */
static char  s_resp_buf[256];
static int   s_resp_len;

static esp_err_t _http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int space = (int)sizeof(s_resp_buf) - s_resp_len - 1;
        int copy  = evt->data_len < space ? evt->data_len : space;
        memcpy(s_resp_buf + s_resp_len, evt->data, copy);
        s_resp_len += copy;
        s_resp_buf[s_resp_len] = '\0';
    }
    return ESP_OK;
}

static esp_err_t fetch_server_hash(const char *url, char *out, size_t out_len)
{
    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = _http_event_cb,
        .timeout_ms    = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hash request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "hash endpoint returned HTTP %d", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error: %s", s_resp_buf);
        return ESP_FAIL;
    }

    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    if (!cJSON_IsString(sha) || !sha->valuestring) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "sha256 field missing");
        return ESP_FAIL;
    }

    strncpy(out, sha->valuestring, out_len - 1);
    out[out_len - 1] = '\0';
    cJSON_Delete(root);
    return ESP_OK;
}

static void running_hash_hex(char *out)
{
    /* app_elf_sha256 is computed from the ELF at build time and embedded
       in esp_app_desc_t; the backend extracts the same bytes from app.bin. */
    const esp_app_desc_t *desc = esp_app_get_description();
    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", desc->app_elf_sha256[i]);
    }
    out[64] = '\0';
}

esp_err_t ota_check_and_update(const char *server_base_url)
{
    char hash_url[192];
    snprintf(hash_url, sizeof(hash_url), "%s/firmware/hash", server_base_url);

    char server_hash[65]  = {0};
    char running_hash[65] = {0};

    esp_err_t err = fetch_server_hash(hash_url, server_hash, sizeof(server_hash));
    if (err != ESP_OK) {
        return err;
    }

    running_hash_hex(running_hash);
    ESP_LOGI(TAG, "running : %s", running_hash);
    ESP_LOGI(TAG, "server  : %s", server_hash);

    if (strcmp(running_hash, server_hash) == 0) {
        ESP_LOGI(TAG, "firmware up to date");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "new firmware detected, starting OTA download");

    char bin_url[192];
    snprintf(bin_url, sizeof(bin_url), "%s/firmware/bin", server_base_url);

    esp_http_client_config_t http_cfg = {
        .url               = bin_url,
        .timeout_ms        = 60000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful — restarting");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* ── MQTT OTA trigger ───────────────────────────────────────────────────── */

static void on_ota_message(const char *topic, int topic_len,
                           const char *data,  int data_len)
{
    char buf[64];
    int copy = data_len < (int)sizeof(buf) - 1 ? data_len : (int)sizeof(buf) - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "OTA request JSON parse failed");
        return;
    }

    cJSON *trigger = cJSON_GetObjectItem(root, "trigger");
    if (cJSON_IsBool(trigger) && trigger->valueint) {
        ESP_LOGI(TAG, "OTA update triggered via MQTT");
        cJSON_Delete(root);
        ota_trigger_update();
        return;
    }

    cJSON_Delete(root);
}

esp_err_t ota_init_mqtt(const char *server_base_url)
{
    if (!server_base_url || strlen(server_base_url) >= sizeof(s_server_base_url)) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_server_base_url, server_base_url, sizeof(s_server_base_url) - 1);
    s_server_base_url[sizeof(s_server_base_url) - 1] = '\0';

    static char s_ota_topic[64];
    char mac[DEVICE_MAC_STR_LEN];
    device_mac_get_str(mac);
    snprintf(s_ota_topic, sizeof(s_ota_topic), "devices/%s/ota", mac);
    device_mqtt_subscribe(s_ota_topic, 1, on_ota_message);

    ESP_LOGI(TAG, "MQTT OTA subscription registered: %s", s_ota_topic);
    return ESP_OK;
}

esp_err_t ota_trigger_update(void)
{
    if (s_server_base_url[0] == '\0') {
        ESP_LOGE(TAG, "server_base_url not configured");
        return ESP_ERR_INVALID_STATE;
    }

    return ota_check_and_update(s_server_base_url);
}
