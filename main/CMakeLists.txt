idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp-mqtt
        actuator_manager
        button
        cli_service
        config_manager
        emqx_client
        event_loop
        factory_test
        health_monitor
        led_indicator
        log_system
        mqtt_wrapper
        network_manager
        nvs_storage
        ota_service
        sensor_manager
        wifi_service
)
