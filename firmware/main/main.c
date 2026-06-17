#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "wifi.h"
#include "ota.h"
#include "remote_log.h"
#include "heartbeat.h"
#include "color.h"
#include "battery.h"
#include "device_mqtt.h"

static const char *TAG = "main";

#define SERVER_RETRY_INTERVAL_MS   60000u
#define SERVER_WATCHDOG_CHECK_MS   30000u
#define SERVER_TIMEOUT_TICKS \
    pdMS_TO_TICKS((TickType_t)CONFIG_SERVER_TIMEOUT_MINUTES * 60u * 1000u)
#define OTA_INTERVAL_TICKS \
    pdMS_TO_TICKS((TickType_t)CONFIG_OTA_CHECK_INTERVAL_MINUTES * 60u * 1000u)

/* Watches MQTT connectivity and requests sleep if the broker has been
   unreachable for SERVER_TIMEOUT_MINUTES. */
static void server_watchdog_task(void *arg)
{
    TickType_t disconnected_since = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SERVER_WATCHDOG_CHECK_MS));

        if (device_mqtt_is_connected()) {
            disconnected_since = 0;
        } else if (disconnected_since == 0) {
            disconnected_since = xTaskGetTickCount();
        } else if ((xTaskGetTickCount() - disconnected_since) >= SERVER_TIMEOUT_TICKS) {
            ESP_LOGW(TAG, "MQTT unreachable for %d min — requesting sleep",
                     CONFIG_SERVER_TIMEOUT_MINUTES);
            heartbeat_request_sleep();
            disconnected_since = 0;  /* fresh window after device wakes */
        }
    }
}

static void ota_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    while (1) {
        esp_err_t err = ota_check_and_update(CONFIG_SERVER_BASE_URL);
        vTaskDelay(err == ESP_OK ? OTA_INTERVAL_TICKS
                                 : pdMS_TO_TICKS(SERVER_RETRY_INTERVAL_MS));
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

    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_LOGI(TAG, "WiFi ready");

    /* Register MQTT subscriptions before connecting — they are re-applied
       automatically on every (re)connect in device_mqtt. */
    remote_log_init();
    ESP_ERROR_CHECK(battery_init());
    ESP_ERROR_CHECK(color_init());
    ESP_ERROR_CHECK(ota_init_mqtt(CONFIG_SERVER_BASE_URL));
    heartbeat_start();

    ESP_ERROR_CHECK(device_mqtt_start(CONFIG_MQTT_BROKER_URL));

    xTaskCreate(server_watchdog_task, "srv_wdog", 4096, NULL, 4, NULL);
    xTaskCreate(ota_task, "ota", 8192, NULL, 4, NULL);
}
