idf_component_register(
    SRCS "led_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver led_indicator freertos esp_system
)
