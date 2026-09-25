idf_component_register(
    SRCS "ota_service.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_ota esp_app_format esp_ota_ops esp_http_client freertos
)
