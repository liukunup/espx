set(src_dirs
    .
    app
    wifi_prov
    param_store
    cert_manager
    web_server
)

set(include_dirs
    .
    app
    wifi_prov
    param_store
    cert_manager
    web_server
)

set(requires
    freertos
    esp_timer
    nvs_flash
    esp_mac
    cjson
    mbedtls
    esp_https_server
    wifi_proving
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)
