idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        log_system
        nvs_storage
        event_loop
        network_manager
        emqx_client
        config_manager
        health_monitor
        ota_manager
        remote_cmd
        wifi_provisioning
        sensor_manager
        actuator_manager
        esp_driver_gpio
        led_indicator
)
