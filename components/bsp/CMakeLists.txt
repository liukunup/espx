set(src_dirs
    actuator_manager
    ads1115
    button
    buzzer
    dht11
    hc595
    ina226
    ir_rx
    ir_tx
    led_indicator
    mcp4725
    relay
    sensor_manager
    tja1050
    ws2812)

set(include_dirs
    actuator_manager
    ads1115
    button
    buzzer
    dht11
    hc595
    ina226
    ir_rx
    ir_tx
    led_indicator
    mcp4725
    relay
    sensor_manager
    tja1050
    ws2812)

set(requires
    driver
    esp_driver_gpio
    esp_driver_ledc
    esp_driver_rmt
    esp_timer
    freertos)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
