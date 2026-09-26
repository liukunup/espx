set(src_dirs
    .
    app
    wifi_prov
    param_store
)

set(include_dirs
    .
    app
    wifi_prov
    param_store
)

set(requires
    freertos esp_timer
    nvs_flash
    esp_mac
    cjson
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)
