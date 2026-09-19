/**
 * @file app_main.c
 * @brief ESP32 IoT Firmware Application Entry
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "log_system.h"
#include "nvs_storage.h"
#include "event_loop.h"
#include "network_manager.h"
#include "emqx_client.h"
#include "config_manager.h"
#include "health_monitor.h"
#include "ota_manager.h"
#include "remote_cmd.h"
#include "wifi_provisioning.h"
#include "sensor_manager.h"
#include "actuator_manager.h"
#include "led_indicator.h"

#define LED_GPIO GPIO_NUM_21

#define FACTORY_RESET_GPIO GPIO_NUM_12

static const char *TAG = "app_main";

/**
 * @brief Check if provisioning is needed
 */
static bool check_provisioning_needed(void);

/**
 * @brief Connect to WiFi and start MQTT
 */
static void app_connect(void);

/**
 * @brief Main application task
 */
static void app_main_task(void *params);

/**
 * @brief Application initialization
 */
static void app_init(void) {
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "ESP32 IoT Firmware v%s", "1.0.0");
    ESP_LOGI(TAG, "===========================================");

    // 0. Initialize LED indicator
    led_indicator_init(LED_GPIO);
    led_indicator_set_status(LED_STATUS_BOOT);
    
    // 1. Initialize log system
    log_system_init();
    ESP_LOGI(TAG, "Step 1: Log system initialized");

    // 2. Initialize NVS storage
    int ret = storage_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %d", ret);
    }
    ESP_LOGI(TAG, "Step 2: NVS storage initialized");

    // 3. Initialize configuration manager
    config_manager_init();
    ESP_LOGI(TAG, "Step 3: Config manager initialized");

    // 4. Initialize event loop
    event_loop_init();
    ESP_LOGI(TAG, "Step 4: Event loop initialized");

    // 5. Initialize health monitor
    health_monitor_init();
    ESP_LOGI(TAG, "Step 5: Health monitor initialized");

    // 6. Initialize network manager (skip if provisioning needed)
    if (config_has_wifi()) {
        network_manager_init();
        ESP_LOGI(TAG, "Step 6: Network manager initialized");
    } else {
        ESP_LOGI(TAG, "Step 6: Skipped (provisioning mode)");
    }

    // 7. Initialize MQTT client (skip if provisioning needed)
    if (config_has_wifi()) {
        mqtt_client_init();
        ESP_LOGI(TAG, "Step 7: MQTT client initialized");
    } else {
        ESP_LOGI(TAG, "Step 7: Skipped (provisioning mode)");
    }

    // 8. Initialize OTA manager
    ota_manager_init();
    ESP_LOGI(TAG, "Step 8: OTA manager initialized");

    // 9. Initialize remote command
    remote_cmd_init();
    ESP_LOGI(TAG, "Step 9: Remote command initialized");

    // 10. Initialize sensor manager
    sensor_manager_init();
    ESP_LOGI(TAG, "Step 10: Sensor manager initialized");

    // 11. Initialize actuator manager
    actuator_manager_init();
    ESP_LOGI(TAG, "Step 11: Actuator manager initialized");

    ESP_LOGI(TAG, "All components initialized successfully!");
    ESP_LOGI(TAG, "Checking config_has_wifi...");
    bool has_wifi = config_has_wifi();
    ESP_LOGI(TAG, "config_has_wifi = %d", has_wifi);
}

/**
 * @brief Check if provisioning is needed
 */
static bool check_provisioning_needed(void) {
    if (!config_has_wifi()) {
        ESP_LOGW(TAG, "No WiFi configuration found!");
        ESP_LOGI(TAG, "Starting AP provisioning mode...");
        // Initialize provisioning
        provisioning_init();
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
        ESP_LOGW(TAG, "No WiFi SSID configured");
        return;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    // Connect to WiFi
    int ret = network_connect(ssid, password);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %d", ret);
        return;
    }

    // Wait for connection
    ret = network_wait_connected(30000);
    if (ret == 0) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
    }
}

/**
 * @brief Main application task
 */
static void app_main_task(void *params) {
    ESP_LOGI(TAG, "Application task started");
    
    // Initialize GPIO0 for factory reset detection (with pull-up, active low)
    gpio_reset_pin(FACTORY_RESET_GPIO);
    gpio_set_direction(FACTORY_RESET_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(FACTORY_RESET_GPIO);
    gpio_pulldown_dis(FACTORY_RESET_GPIO);
    
    int reset_pressed_time = 0;
    bool reset_triggered = false;

    while (1) {
        // Check GPIO0 for factory reset (short to GND for 5 seconds)
        if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
            reset_pressed_time += 100;
            
            // Update LED as warning
            if (reset_pressed_time >= 1000 && reset_pressed_time <= 5000) {
                if (reset_pressed_time % 500 < 100) {
                    // Flash red as warning
                    led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);
                }
            }
            
            if (reset_pressed_time >= 5000 && !reset_triggered) {
                ESP_LOGW(TAG, "Factory reset triggered by GPIO0!");
                led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);
                reset_triggered = true;
                
                // Flash LED and delay before reset
                for (int i = 0; i < 10; i++) {
                    led_indicator_task();
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                
                ESP_LOGW(TAG, "Erasing NVS and restarting...");
                nvs_flash_erase();
                esp_restart();
            }
        } else {
            if (reset_pressed_time > 0 && !reset_triggered) {
                ESP_LOGI(TAG, "GPIO0 released, cancel reset");
            }
            reset_pressed_time = 0;
        }
        
        // Update LED state
        led_indicator_task();
        
        // Check network connection
        if (network_is_connected() && mqtt_is_connected()) {
            // Publish telemetry periodically
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
    }
}

/**
 * @brief Check GPIO for factory reset
 * Hold BOOT button for 10 seconds during startup to reset to factory defaults
 */
static void check_factory_reset(void) {
    gpio_reset_pin(FACTORY_RESET_GPIO);
    gpio_set_direction(FACTORY_RESET_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(FACTORY_RESET_GPIO);
    gpio_pulldown_dis(FACTORY_RESET_GPIO);
    
    ESP_LOGI(TAG, "Hold BOOT (GPIO0) 10s for factory reset...");
    led_indicator_set_status(LED_STATUS_PROV_START);  // Yellow blinking
    
    int pressed_time = 0;
    while (pressed_time < 10000) {
        // Update LED state
        led_indicator_task();
        
        if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
            pressed_time += 100;
            if (pressed_time % 1000 == 0) {
                ESP_LOGI(TAG, "BOOT pressed: %ds/10s", pressed_time / 1000);
                // Flash red to indicate progress
                led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);
            }
        } else {
            if (pressed_time > 0) {
                ESP_LOGI(TAG, "BOOT released early, cancel reset");
                led_indicator_set_status(LED_STATUS_BOOT);
                return;
            }
            pressed_time = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGW(TAG, "Factory reset triggered!");
    led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);  // Red fast blink
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGW(TAG, "Erasing NVS and restarting...");
    nvs_flash_erase();
    esp_restart();
}

void app_main(void) {
    // Initialize LED first so we can see feedback during factory reset
    led_indicator_init(LED_GPIO);
    led_indicator_set_status(LED_STATUS_BOOT);
    
    // Check for factory reset
    check_factory_reset();
    
    // Initialize all components
    app_init();

    // Check if provisioning is needed FIRST
    if (!config_has_wifi()) {
        ESP_LOGW(TAG, "No WiFi configuration found!");
        ESP_LOGI(TAG, "Starting AP provisioning mode...");
        
        // Only initialize provisioning components
        provisioning_init();
        provisioning_start();
        
        ESP_LOGI(TAG, "Running in provisioning mode...");
        // Stay in provisioning mode forever
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    // Normal mode: skip MQTT for now
    ESP_LOGI(TAG, "Network manager initialized");
    
    // Start WiFi connection
    ESP_LOGI(TAG, "Starting WiFi connection...");
    app_connect();

    // Create main application task
    xTaskCreatePinnedToCore(app_main_task, "app_main", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Application started successfully!");
}
