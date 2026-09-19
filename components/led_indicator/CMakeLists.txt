idf_component_register(
    SRCS "led_indicator.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_rmt esp_driver_gpio esp_timer
)
