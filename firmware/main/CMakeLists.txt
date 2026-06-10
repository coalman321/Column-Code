idf_component_register(
    SRCS "main.c" "wifi.c" "http_client.c" "ota.c" "remote_log.c" "heartbeat.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        driver
        app_update
        esp_https_ota
        json
)
