#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi.h"
#include "ota.h"
#include "remote_log.h"
#include "heartbeat.h"
#include "color.h"

static const char *TAG = "main";

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

    ESP_ERROR_CHECK(wifi_init_sta());

    remote_log_init(CONFIG_SERVER_BASE_URL);
    color_init(CONFIG_SERVER_BASE_URL);

    ESP_LOGI(TAG, "WiFi ready");
    heartbeat_start(CONFIG_SERVER_BASE_URL);
    xTaskCreate(ota_task,   "ota",   8192, NULL, 4, NULL);
    xTaskCreate(color_task, "color", 4096, NULL, 3, NULL);
}
