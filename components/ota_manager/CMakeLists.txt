idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_partition esp_timer esp_app_format esp_http_client
)
