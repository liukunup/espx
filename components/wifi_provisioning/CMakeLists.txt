idf_component_register(
    SRCS "wifi_provisioning.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_http_server
)
