idf_component_register(
    SRCS "event_loop.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_timer
)
