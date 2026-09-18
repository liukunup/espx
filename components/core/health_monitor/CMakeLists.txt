idf_component_register(
    SRCS "health_monitor.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_timer esp_system
)
