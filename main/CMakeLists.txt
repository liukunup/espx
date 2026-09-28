# ESPX main component
#
# Note: All sources are listed explicitly. Add new modules here.

set(espx_sources
    # Application entry
    "app_main.c"

    # Shared helpers
    "utils/json_utils.c"
    "utils/str_utils.c"
    "utils/nvs_utils.c"

    # Core services
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"

    # Device model
    "device/device_type.c"
    "device/device_manager.c"
    "device/event_bus.c"

    # Configuration
    "config/node_config.c"
    "config/config_apply.c"
    "config/yaml.c"

    # IP-dependent services
    "net_services/net_services.c"
    "net_services/time_sync.c"
    "net_services/mdns_service.c"

    # Peripheral drivers
    "peripherals/peripherals.c"
    "peripherals/esp_idf_i2c.c"
    "peripherals/tja1050.c"
    "peripherals/mcp4725.c"
    "peripherals/ads1115.c"
    "peripherals/ina226.c"
    "peripherals/buzzer.c"
    "peripherals/dht11.c"
    "peripherals/button.c"
    "peripherals/relay.c"
    "peripherals/shiftreg_595.c"
    "peripherals/ws2812.c"

    # MQTT
    "mqtt_client/espx_mqtt_client.c"
    "mqtt_client/mqtt_commander.c"
    "mqtt_client/mqtt_publisher.c"

    # Web / certificates
    "cert_manager/cert_manager.c"
    "web_server/web_server.c"
    "web_server/ws_server.c"

    # Wi-Fi provisioning
    "wifi_prov/wifi_prov.c"

    # Manufacturing test mode
    "test_mode/test_mode.c"

    # Manufacturing provisioning
    "mfg_provision/mfg_provision.c"

    # Delta OTA
    "ota_service/ota_service.c"
)

set(espx_include_dirs
    "."
    "utils"
    "config"
    "device"
    "core"
    "peripherals"
    "mqtt_client"
    "cert_manager"
    "web_server"
    "wifi_prov"
    "test_mode"
    "mfg_provision"
    "ota_service"
    "net_services"
)

idf_component_register(
    SRCS ${espx_sources}
    INCLUDE_DIRS ${espx_include_dirs}
    EMBED_FILES
        "web_server/web_files/index.html"
        "cert_manager/certs/server.crt"
        "cert_manager/certs/server.key"
    REQUIRES mqtt network_provisioning esp_delta_ota mdns
    PRIV_REQUIRES
        nvs_flash
        esp_netif
        esp_wifi
        esp_https_server
        esp_http_client
        app_update
        esp_app_format
        esp_event
        esp_timer
        protocomm
        qrcode
        driver
        esp_driver_twai
        esp_driver_i2c
        led_driver
        led_strip
        cjson
        mbedtls
)
