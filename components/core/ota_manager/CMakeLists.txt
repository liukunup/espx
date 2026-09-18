idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "."
    REQUIRES esp_ota_ops esp_partition esp_timer
)
