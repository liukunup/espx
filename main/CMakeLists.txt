idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        log_system
        nvs_storage
        event_loop
        network_manager
        mqtt_client
        config_manager
        health_monitor
        ota_manager
        remote_cmd
        provisioning
        sensor_manager
        actuator_manager
)
