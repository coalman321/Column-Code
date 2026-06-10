#include "http_client.h"

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http";

static esp_err_t event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGI(TAG, "response: %.*s", evt->data_len, (char *)evt->data);
            }
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_get(const char *url)
{
    esp_http_client_config_t config = {
        .url           = url,
        .event_handler = event_handler,
        .timeout_ms    = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GET %s -> status=%d, len=%lld",
                 url,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
