set(src_dirs
    config_manager
    event_loop
    health_monitor
    log_system
    nvs_storage
    ota_service)

set(include_dirs
    config_manager
    event_loop
    health_monitor
    log_system
    nvs_storage
    ota_service)

set(requires
    esp_http_client
    esp_partition
    esp_system
    esp_timer
    esp_wifi
    freertos
    nvs_flash)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
