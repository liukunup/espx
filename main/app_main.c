/**
 * @file app_main.c
 * @brief ESP32 IoT Firmware Application Entry
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "log_system.h"
#include "nvs_storage.h"
#include "event_loop.h"
#include "network_manager.h"
#include "mqtt_client.h"
#include "config_manager.h"
#include "health_monitor.h"
#include "ota_manager.h"
#include "remote_cmd.h"
#include "provisioning.h"
#include "sensor_manager.h"
#include "actuator_manager.h"

static const char *TAG = "app_main";

/**
 * @brief Application initialization
 */
static void app_init(void) {
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "ESP32 IoT Firmware v%s", "1.0.0");
    ESP_LOGI(TAG, "===========================================");

    // 1. Initialize log system
    log_system_init();
    LOGI(TAG, "Step 1: Log system initialized");

    // 2. Initialize NVS storage
    int ret = nvs_init();
    if (ret != 0) {
        LOGE(TAG, "Failed to initialize NVS: %d", ret);
    }
    LOGI(TAG, "Step 2: NVS storage initialized");

    // 3. Initialize configuration manager
    config_manager_init();
    LOGI(TAG, "Step 3: Config manager initialized");

    // 4. Initialize event loop
    event_loop_init();
    LOGI(TAG, "Step 4: Event loop initialized");

    // 5. Initialize health monitor
    health_monitor_init();
    LOGI(TAG, "Step 5: Health monitor initialized");

    // 6. Initialize network manager
    network_manager_init();
    LOGI(TAG, "Step 6: Network manager initialized");

    // 7. Initialize MQTT client
    mqtt_client_init();
    LOGI(TAG, "Step 7: MQTT client initialized");

    // 8. Initialize OTA manager
    ota_manager_init();
    LOGI(TAG, "Step 8: OTA manager initialized");

    // 9. Initialize remote command
    remote_cmd_init();
    LOGI(TAG, "Step 9: Remote command initialized");

    // 10. Initialize sensor manager
    sensor_manager_init();
    LOGI(TAG, "Step 10: Sensor manager initialized");

    // 11. Initialize actuator manager
    actuator_manager_init();
    LOGI(TAG, "Step 11: Actuator manager initialized");

    LOGI(TAG, "All components initialized successfully!");
}

/**
 * @brief Check if provisioning is needed
 */
static bool check_provisioning_needed(void) {
    if (!config_has_wifi()) {
        ESP_LOGW(TAG, "No WiFi configuration found!");
        ESP_LOGI(TAG, "Starting AP provisioning mode...");
        provisioning_start();
        return true;
    }
    return false;
}

/**
 * @brief Connect to WiFi and start MQTT
 */
static void app_connect(void) {
    // Get WiFi credentials
    const char *ssid = config_get_wifi_ssid();
    const char *password = config_get_wifi_password();

    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "No WiFi SSID configured");
        return;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    // Connect to WiFi
    int ret = network_connect(ssid, password);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "WiFi connection initiated, waiting for connection...");

    // Wait for connection (simplified - in production use events)
    int wait_count = 0;
    while (!network_is_connected() && wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_count++;
    }

    if (network_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected!");

        // Get device ID
        const char *device_id = config_get_device_id();
        if (device_id == NULL || strlen(device_id) == 0) {
            device_id = "ESP32-unknown";
        }

        // Configure and start MQTT
        char broker[128];
        snprintf(broker, sizeof(broker), "mqtt://%s:%d",
                 "192.168.1.100", 8883);  // TODO: load from config

        mqtt_client_configure(broker);
        mqtt_client_set_client_id(device_id);
        mqtt_client_start();

        ESP_LOGI(TAG, "MQTT client started");
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
    }
}

/**
 * @brief Main application task
 */
static void app_main_task(void *params) {
    ESP_LOGI(TAG, "Application task started");

    while (1) {
        // Check network connection
        if (network_is_connected() && mqtt_is_connected()) {
            // Publish telemetry periodically
            // This would be implemented with actual sensor data
            // mqtt_publish_telemetry(telemetry_json);
        }

        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds
    }
}

/**
 * @brief Application entry point
 */
void app_main(void) {
    // Initialize all components
    app_init();

    // Check if provisioning is needed
    if (check_provisioning_needed()) {
        ESP_LOGI(TAG, "Running in provisioning mode...");
        // Stay in provisioning mode
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    // Connect to network and MQTT
    app_connect();

    // Create main application task
    xTaskCreatePinnedToCore(app_main_task, "app_main", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Application started successfully!");
}
