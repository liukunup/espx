/* ESPX Application Main Entry */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <esp_system.h>

#include "led_driver.h"
#include "event_bus.h"
#include "device_type.h"
#include "device_manager.h"
#include "node_config.h"
#include "peripherals.h"
#include "mqtt_client/espx_mqtt_client.h"
#include "mqtt_client/mqtt_commander.h"
#include "mqtt_client/mqtt_publisher.h"
#include "cert_manager/cert_manager.h"
#include "web_server/web_server.h"
#include "wifi_prov/wifi_prov.h"

static const char *TAG = "app_main";

void app_main(void)
{
    printf("\n================================================\n");
    printf("           ESPX IoT Device Firmware\n");
    printf("================================================\n");
    printf("  Version  : %s\n", CONFIG_FIRMWARE_VERSION);
    printf("  Device   : %s\n", CONFIG_DEVICE_NAME);
    printf("  Chip     : ESP32-S3\n");
    printf("  Build    : %s %s\n", __DATE__, __TIME__);
    printf("================================================\n\n");

    ESP_LOGI(TAG, "Initializing NVS...");
    ESP_ERROR_CHECK(nvs_flash_init());

    // Wi-Fi provisioning (initializes netif, event loop, Wi-Fi)
    // Must come before MQTT / web server
    ESP_LOGI(TAG, "Initializing Wi-Fi provisioning...");
    ESP_ERROR_CHECK(wifi_prov_init());
    ESP_ERROR_CHECK(wifi_prov_start(NULL));
    wifi_prov_wait_for_connection();
    ESP_LOGI(TAG, "Wi-Fi connected");

    // Initialize core services
    ESP_LOGI(TAG, "Initializing core services...");
    ESP_ERROR_CHECK(node_config_init());
    ESP_ERROR_CHECK(node_config_load());
    ESP_ERROR_CHECK(event_bus_init());
    ESP_ERROR_CHECK(device_type_registry_init());
    ESP_ERROR_CHECK(peripherals_register_all());
    ESP_ERROR_CHECK(device_manager_init());
    ESP_ERROR_CHECK(device_manager_load());

    // Initialize LED
    ESP_LOGI(TAG, "Initializing LED driver...");
    ESP_ERROR_CHECK(led_driver_init());
    led_set_status("connected");

    // Initialize certificates + HTTPS web server
    ESP_LOGI(TAG, "Initializing certificate manager...");
    ESP_ERROR_CHECK(cert_manager_init());

    ESP_LOGI(TAG, "Starting HTTPS web server...");
    if (web_server_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
    }

    // Initialize MQTT
    ESP_LOGI(TAG, "Initializing MQTT...");
    ESP_ERROR_CHECK(mqtt_client_init());
    ESP_ERROR_CHECK(mqtt_commander_init());
    ESP_ERROR_CHECK(mqtt_publisher_init());
    ESP_ERROR_CHECK(mqtt_client_start());
    ESP_ERROR_CHECK(mqtt_publisher_start());

    ESP_LOGI(TAG, "ESPX device started! Device ID: %s, devices: %d",
             node_config_get_device_id(), (int)device_get_count());

    event_bus_publish(EVENT_NODE_READY, NULL, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGD(TAG, "Main loop alive...");
    }
}
