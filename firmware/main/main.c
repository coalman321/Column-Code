#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi.h"
#include "ota.h"
#include "remote_log.h"
#include "heartbeat.h"
#include "color.h"

#define SERVER_TIMEOUT_US \
    ((int64_t)CONFIG_SERVER_TIMEOUT_MINUTES * 60 * 1000 * 1000)

static const char *TAG = "main";

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

static void heartbeat_task(void *arg)
{
    int64_t first_fail_us = 0;
    bool    failing       = false;

    while (1) {
        esp_err_t err = heartbeat_send();

        if (err == ESP_OK) {
            first_fail_us = 0;
            failing       = false;
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

static void ota_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    while (1) {
        ota_check_and_update(CONFIG_SERVER_BASE_URL);
        vTaskDelay(pdMS_TO_TICKS(
            (uint32_t)CONFIG_OTA_CHECK_INTERVAL_MINUTES * 60 * 1000));
    }
}

static void color_task(void *arg)
{
    while (1) {
        color_fetch_and_apply();
        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_COLOR_POLL_INTERVAL_SECONDS * 1000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    configure_wakeup_gpio();
    ESP_ERROR_CHECK(wifi_init_sta());

    remote_log_init(CONFIG_SERVER_BASE_URL);
    heartbeat_init(CONFIG_SERVER_BASE_URL);
    color_init(CONFIG_SERVER_BASE_URL);

    ESP_LOGI(TAG, "WiFi ready");
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(ota_task,       "ota",       8192, NULL, 4, NULL);
    xTaskCreate(color_task,     "color",     4096, NULL, 3, NULL);
}
