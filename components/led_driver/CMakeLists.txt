idf_component_register(
    SRCS "led_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_system led_strip
)
