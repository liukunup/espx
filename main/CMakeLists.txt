set(src_dirs
    .
    app
    wifi_prov
    param_store
    cert_manager
    web_server
    mqtt_client
    ota_service
    mfg_provision
    test_mode
)

set(include_dirs
    .
    app
    wifi_prov
    param_store
    cert_manager
    web_server
    mqtt_client
    ota_service
    mfg_provision
    test_mode
)

set(requires
    freertos
    esp_timer
    nvs_flash
    esp_mac
    cjson
    mbedtls
    esp_https_server
    wifi_provisioning
    esp_mqtt
    esp_ota_ops
    led_driver
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)
