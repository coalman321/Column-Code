#include "heartbeat.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "device_mac.h"
#include "color.h"

static const char *TAG = "heartbeat";

#define RESP_BUF_SIZE   64
#define SERVER_TIMEOUT_US \
    ((int64_t)CONFIG_SERVER_TIMEOUT_MINUTES * 60 * 1000 * 1000)

static char s_url[192];
static char s_mac[18];

typedef struct { char buf[RESP_BUF_SIZE]; int len; } resp_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_ctx_t *ctx = evt->user_data;
    int copy = evt->data_len;
    if (ctx->len + copy >= RESP_BUF_SIZE) copy = RESP_BUF_SIZE - ctx->len - 1;
    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, copy);
        ctx->len += copy;
    }
    return ESP_OK;
}

static void configure_wakeup_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << CONFIG_WAKEUP_GPIO_PIN),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = CONFIG_WAKEUP_GPIO_ACTIVE_LOW ? GPIO_PULLUP_ENABLE
                                                       : GPIO_PULLUP_DISABLE,
        .pull_down_en  = CONFIG_WAKEUP_GPIO_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE
                                                       : GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_int_type_t level = CONFIG_WAKEUP_GPIO_ACTIVE_LOW
                            ? GPIO_INTR_LOW_LEVEL
                            : GPIO_INTR_HIGH_LEVEL;
    ESP_ERROR_CHECK(gpio_wakeup_enable(CONFIG_WAKEUP_GPIO_PIN, level));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
}

static esp_err_t heartbeat_send(bool *sleep_out)
{
    *sleep_out = false;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "mac", s_mac);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return ESP_ERR_NO_MEM;

    resp_ctx_t ctx = { .len = 0 };
    esp_http_client_config_t cfg = {
        .url           = s_url,
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 5000,
        .event_handler = on_http_event,
        .user_data     = &ctx,
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

    ctx.buf[ctx.len] = '\0';
    cJSON *root = cJSON_Parse(ctx.buf);
    if (root) {
        cJSON *js = cJSON_GetObjectItem(root, "sleep");
        if (cJSON_IsTrue(js)) {
            *sleep_out = true;
            ESP_LOGI(TAG, "server requested sleep");
        }
        cJSON_Delete(root);
    }

    return ESP_OK;
}

static void heartbeat_task(void *arg)
{
    int64_t first_fail_us = 0;
    bool    failing       = false;

    while (1) {
        bool      sleep_req = false;
        esp_err_t err       = heartbeat_send(&sleep_req);

        if (err == ESP_OK) {
            first_fail_us = 0;
            failing       = false;

            if (sleep_req) {
                ESP_LOGI(TAG, "Sleep requested by server — entering light sleep. "
                              "Wake on GPIO %d.", CONFIG_WAKEUP_GPIO_PIN);
                color_apply(0, 0, 0, 0);
                esp_light_sleep_start();
                ESP_LOGI(TAG, "Woke from light sleep (cause: %d)",
                         (int)esp_sleep_get_wakeup_cause());
            }
        } else {
            if (!failing) {
                first_fail_us = esp_timer_get_time();
                failing       = true;
            }

            int64_t elapsed = esp_timer_get_time() - first_fail_us;
            if (elapsed >= SERVER_TIMEOUT_US) {
                ESP_LOGW(TAG, "Server unreachable for %d min — entering light sleep. "
                              "Wake on GPIO %d.",
                         CONFIG_SERVER_TIMEOUT_MINUTES, CONFIG_WAKEUP_GPIO_PIN);
                color_apply(0, 0, 0, 0);
                esp_light_sleep_start();
                ESP_LOGI(TAG, "Woke from light sleep (cause: %d)",
                         (int)esp_sleep_get_wakeup_cause());
                first_fail_us = 0;
                failing       = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_HEARTBEAT_INTERVAL_SECONDS * 1000));
    }
}

void heartbeat_start(const char *server_base_url)
{
    device_mac_get_str(s_mac);
    snprintf(s_url, sizeof(s_url), "%s/clients/heartbeat", server_base_url);

    configure_wakeup_gpio();

    ESP_LOGI(TAG, "heartbeat ready (mac=%s)", s_mac);
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
}
