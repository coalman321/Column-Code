idf_component_register(
    SRCS "main.c" "wifi.c" "http_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        driver
)
