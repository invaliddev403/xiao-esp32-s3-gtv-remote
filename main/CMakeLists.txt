idf_component_register(
    SRCS
        "app_main.c"
        "ble_remote.c"
        "usb_network.c"
        "web_server.c"
    INCLUDE_DIRS "."
    EMBED_FILES "web/index.html"
    REQUIRES driver esp_hid nvs_flash esp_tinyusb esp_netif esp_http_server esp_timer heap cjson lwip
)
