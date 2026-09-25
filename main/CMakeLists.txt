idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp-mqtt
        actuator_manager
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
        ota_manager
        remote_cmd
        sensor_manager
        wifi_provisioning
)
