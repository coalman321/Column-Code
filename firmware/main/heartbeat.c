#include "heartbeat.h"

#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "device_mac.h"
#include "device_mqtt.h"
#include "color.h"
#include "battery.h"

static const char *TAG = "heartbeat";

static char s_heartbeat_topic[64];   /* "devices/{mac}/heartbeat" */
static char s_cmd_topic[64];          /* "devices/{mac}/cmd"       */
static volatile bool s_sleep_requested = false;
static volatile bool s_blink_requested = false;

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
                            ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    ESP_ERROR_CHECK(gpio_wakeup_enable(CONFIG_WAKEUP_GPIO_PIN, level));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
}

void heartbeat_request_sleep(void)
{
    s_sleep_requested = true;
}

static void on_cmd_message(const char *topic, int topic_len,
                           const char *data,  int data_len)
{
    char buf[64];
    int copy = data_len < (int)sizeof(buf) - 1 ? data_len : (int)sizeof(buf) - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    cJSON *js_sleep = cJSON_GetObjectItem(root, "sleep");
    if (cJSON_IsTrue(js_sleep)) {
        s_sleep_requested = true;
        ESP_LOGI(TAG, "server requested sleep via MQTT");
    }

    cJSON *js_blink = cJSON_GetObjectItem(root, "blink");
    if (cJSON_IsTrue(js_blink)) {
        s_blink_requested = true;
        ESP_LOGI(TAG, "server requested blink via MQTT");
    }

    cJSON_Delete(root);
}

static void heartbeat_task(void *arg)
{
    while (1) {
        /* Read battery status and include in heartbeat */
        BatteryStatus batt = {0};
        cJSON *root = cJSON_CreateObject();

        if (battery_read(&batt) == ESP_OK) {
            cJSON_AddNumberToObject(root, "battery_percent", batt.soc);
            cJSON_AddNumberToObject(root, "battery_mv", batt.voltage_mv);
        }

        char *json_str = cJSON_PrintUnformatted(root);
        device_mqtt_publish(s_heartbeat_topic, json_str, 1, 0);
        free(json_str);
        cJSON_Delete(root);

        if (s_blink_requested) {
            s_blink_requested = false;
            ESP_LOGI(TAG, "blinking green for 5 seconds");
            uint32_t blink_end = esp_timer_get_time() / 1000 + 5000;  /* 5 seconds from now */
            while (esp_timer_get_time() / 1000 < blink_end) {
                color_apply(0, 255, 0, 0, false);  /* Green ON */
                vTaskDelay(pdMS_TO_TICKS(100));
                color_apply(0, 0, 0, 0, false);    /* OFF */
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            color_apply(0, 0, 0, 0, false);  /* Ensure OFF at end */
        }

        if (s_sleep_requested) {
            s_sleep_requested = false;
            ESP_LOGI(TAG, "entering light sleep, wake on GPIO %d",
                     CONFIG_WAKEUP_GPIO_PIN);
            color_apply(0, 0, 0, 0, false);
            device_mqtt_suspend();
            esp_light_sleep_start();
            device_mqtt_resume();
            ESP_LOGI(TAG, "woke from light sleep (cause: %d)",
                     (int)esp_sleep_get_wakeup_cause());
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_HEARTBEAT_INTERVAL_SECONDS * 1000));
    }
}

void heartbeat_start(void)
{
    char mac[DEVICE_MAC_STR_LEN];
    device_mac_get_str(mac);
    snprintf(s_heartbeat_topic, sizeof(s_heartbeat_topic),
             "devices/%s/heartbeat", mac);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "devices/%s/cmd", mac);

    device_mqtt_subscribe(s_cmd_topic, 1, on_cmd_message);
    configure_wakeup_gpio();

    ESP_LOGI(TAG, "heartbeat ready (mac=%s)", mac);
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 5, NULL);
}
