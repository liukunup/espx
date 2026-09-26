set(src_dirs
    .
    wifi_prov
)

set(include_dirs
    .
    wifi_prov
)

set(requires
    freertos esp_timer
)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)
