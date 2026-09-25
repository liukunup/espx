set(src_dirs
    cli_service
    factory_test_mode)

set(include_dirs
    cli_service
    factory_test_mode)
 
set(requires
    freertos esp_timer)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
