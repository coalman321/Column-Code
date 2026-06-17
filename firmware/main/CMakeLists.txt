idf_component_register(
    SRCS "main.c" "wifi.c" "device_mqtt.c" "ota.c" "remote_log.c" "heartbeat.c" "color.c" "battery.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        driver
        esp_driver_rmt
        esp_driver_i2c
        app_update
        esp_https_ota
        mqtt
        json
)
