set(src_dirs
    factory_test_mode
    remote_cmd)

set(include_dirs
    factory_test_mode
    remote_cmd)
 
set(requires
    freertos esp_timer)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
