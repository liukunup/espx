# ESPX main component
#
# Note: All sources are listed explicitly. Add new modules here.

set(espx_sources
    # Application entry
    "app_main.c"

    # Core services
    "core/device_type.c"
    "core/event_bus.c"
    "core/node_config.c"
    "core/device_manager.c"

    # Peripheral drivers
    "peripherals/peripherals.c"
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

    # Wi-Fi provisioning
    "wifi_prov/wifi_prov.c"

    # Manufacturing test mode
    "test_mode/test_mode.c"

    # Manufacturing provisioning
    "mfg_provision/mfg_provision.c"
)

set(espx_include_dirs
    "."
    "core"
    "peripherals"
    "mqtt_client"
    "cert_manager"
    "web_server"
    "wifi_prov"
    "test_mode"
    "mfg_provision"
)

idf_component_register(
    SRCS ${espx_sources}
    INCLUDE_DIRS ${espx_include_dirs}
    REQUIRES mqtt network_provisioning
    PRIV_REQUIRES
        nvs_flash
        esp_netif
        esp_wifi
        esp_https_server
        esp_https_ota
        esp_event
        esp_timer
        protocomm
        qrcode
        driver
        app_update
        led_driver
        led_strip
        cjson
        mbedtls
)
