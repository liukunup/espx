set(src_dirs
    emqx_client
    mqtt_wrapper
    network_manager
    wifi_service)

set(include_dirs
    emqx_client
    mqtt_wrapper
    network_manager
    wifi_service)

set(requires
    esp_event
    esp_http_server
    esp_mqtt
    esp_netif
    esp_wifi
    freertos
    nvs_flash
    nvs_storage)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
