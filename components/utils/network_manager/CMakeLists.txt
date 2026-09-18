idf_component_register(
    SRCS "network_manager.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_wifi esp_event
)
