#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi.h"
#include "ota.h"
#include "remote_log.h"
#include "heartbeat.h"
#include "color.h"
#include "device_mqtt.h"

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
    ESP_ERROR_CHECK(color_init());
    heartbeat_start();

    ESP_ERROR_CHECK(device_mqtt_start(CONFIG_MQTT_BROKER_URL));

    xTaskCreate(ota_task, "ota", 8192, NULL, 4, NULL);
}
