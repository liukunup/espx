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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "log_system.h"
#include "nvs_storage.h"
#include "event_loop.h"
#include "network_manager.h"
#include "emqx_client.h"
#include "config_manager.h"
#include "health_monitor.h"
// Include new ESP services
#include "wifi_service.h"
#include "ota_service.h"
#include "cli_service.h"
#include "button_service.h"

#include "sensor_manager.h"
#include "actuator_manager.h"
#include "led_indicator.h"

#define LED_GPIO GPIO_NUM_21

#define FACTORY_RESET_GPIO GPIO_NUM_12

static const char *TAG = "app_main";

/** @brief Factory reset button handle */
static button_service_handle_t g_factory_reset_btn = NULL;

/**
 * @brief Main application task
 */
static void app_main_task(void *params) {
    ESP_LOGI(TAG, "Application task started");

    while (1) {
        led_indicator_task();
        led_indicator_cycle_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Factory reset button callback
 */
static void factory_reset_callback(const button_service_event_t *event, void *user_data) {
    (void)user_data;
    
    switch (event->type) {
        case BUTTON_SERVICE_EVENT_LONG_PRESSED:
            ESP_LOGI(TAG, "Factory reset: Long press detected (%ums)", event->press_duration_ms);
            break;
        case BUTTON_SERVICE_EVENT_CLICKED:
            ESP_LOGI(TAG, "Factory reset: Short press (%ums)", event->press_duration_ms);
            break;
        default:
            break;
    }
}

/**
 * @brief Check GPIO12 for factory reset
 * Hold GPIO12 for 5 seconds during startup, then release to confirm
 * Flow: Hold 5s -> blink red (waiting for release) -> Release -> Erase NVS -> Restart
 * This prevents accidental reset if user keeps holding the button
 */
static void check_factory_reset(void) {
    // Create button service for factory reset
    button_service_config_t btn_cfg = {
        .button_id = 0,
        .gpio_num = FACTORY_RESET_GPIO,
        .long_press_ms = 5000,  // 5 seconds for factory reset
        .active_low = true,      // Active low (pull-up mode)
    };
    
    g_factory_reset_btn = button_service_create(&btn_cfg);
    if (g_factory_reset_btn == NULL) {
        ESP_LOGE(TAG, "Failed to create factory reset button");
        return;
    }
    
    // Register callback
    button_service_register_callback(g_factory_reset_btn, factory_reset_callback, NULL);
    
    ESP_LOGI(TAG, "Hold GPIO12 5s for factory reset...");
    led_indicator_set_status(LED_STATUS_PROV_START);  // Yellow blinking
    
    int pressed_time = 0;
    bool waiting_for_release = false;
    bool pressed = false;
    
    while (1) {
        // Update LED state periodically
        led_indicator_task();
        
        // Poll button state
        button_service_poll(g_factory_reset_btn);
        button_service_get_state(g_factory_reset_btn, &pressed);
        
        if (!waiting_for_release) {
            // Phase 1: Wait for 5 seconds press
            if (pressed) {
                pressed_time += 100;
                if (pressed_time % 1000 == 0) {
                    ESP_LOGI(TAG, "GPIO12 pressed: %ds/5s", pressed_time / 1000);
                    // Flash red to indicate progress
                    led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);
                }
                
                if (pressed_time >= 5000) {
                    // 5 seconds reached, enter waiting state
                    waiting_for_release = true;
                    ESP_LOGI(TAG, "GPIO12 held 5s, release to confirm factory reset");
                }
            } else {
                if (pressed_time > 0) {
                    ESP_LOGI(TAG, "GPIO12 released early, cancel reset");
                    led_indicator_set_status(LED_STATUS_BOOT);
                    button_service_delete(g_factory_reset_btn);
                    g_factory_reset_btn = NULL;
                    return;
                }
            }
        } else {
            // Phase 2: Waiting for release - blink green
            if (pressed_time % 200 < 100) {
                led_indicator_set_pattern(LED_ON, LED_COLOR_GREEN);
            } else {
                led_indicator_set_pattern(LED_OFF, LED_COLOR_GREEN);
            }
            
            // Check if released
            if (!pressed) {
                ESP_LOGW(TAG, "GPIO12 released, starting factory reset!");
                break;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Factory reset sequence
    ESP_LOGW(TAG, "Factory reset triggered!");
    led_indicator_set_pattern(LED_BLINK_FAST, LED_COLOR_RED);  // Red fast blink
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Cleanup button service
    if (g_factory_reset_btn != NULL) {
        button_service_delete(g_factory_reset_btn);
        g_factory_reset_btn = NULL;
    }
    
    ESP_LOGW(TAG, "Erasing NVS and restarting...");
    nvs_flash_erase();
    esp_restart();
}

void app_main(void) {
    // 立即打印日志
    printf("=== ESP32 IoT Firmware Starting ===\n");
    printf("Hello from ESP32-S3!\n");
    
    // Initialize LED
    printf("Initializing LED...\n");
    led_indicator_init(LED_GPIO);
    // Start color cycle test: Blue LED -> Red -> Green -> Blue(WS2812)
    led_indicator_cycle_colors();
    printf("LED initialized, starting color cycle test\n");
    
    // Print startup banner
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "ESP32 IoT Firmware v1.0.0");
    ESP_LOGI(TAG, "===========================================");
    
    // Check for factory reset
    ESP_LOGI(TAG, "Checking factory reset...");
    check_factory_reset();
    ESP_LOGI(TAG, "Factory reset check complete");
    
    // Initialize log system
    ESP_LOGI(TAG, "Initializing log system...");
    log_system_init();
    ESP_LOGI(TAG, "Step 1: Log system initialized");

    // Initialize NVS storage
    ESP_LOGI(TAG, "Initializing NVS storage...");
    int ret = storage_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %d", ret);
    }
    ESP_LOGI(TAG, "Step 2: NVS storage initialized");

    // Initialize configuration manager
    ESP_LOGI(TAG, "Initializing config manager...");
    config_manager_init();
    ESP_LOGI(TAG, "Step 3: Config manager initialized");

    // Initialize event loop
    ESP_LOGI(TAG, "Initializing event loop...");
    event_loop_init();
    ESP_LOGI(TAG, "Step 4: Event loop initialized");

    // Initialize health monitor
    ESP_LOGI(TAG, "Initializing health monitor...");
    health_monitor_init();
    ESP_LOGI(TAG, "Step 5: Health monitor initialized");

    // Initialize Wi-Fi service
    ESP_LOGI(TAG, "Initializing Wi-Fi service...");
    wifi_service_init();
    ESP_LOGI(TAG, "Step 6: Wi-Fi service initialized");

    // Initialize MQTT client (skip if provisioning needed)
    if (config_has_wifi()) {
        mqtt_client_init();
        ESP_LOGI(TAG, "Step 7: MQTT client initialized");
    } else {
        ESP_LOGI(TAG, "Step 7: Skipped (provisioning mode)");
    }

    // Initialize OTA service (new)
    ESP_LOGI(TAG, "Initializing OTA service...");
    ota_service_init(NULL);
    ESP_LOGI(TAG, "Step 8: OTA service initialized");

    // Initialize CLI service (new)
    ESP_LOGI(TAG, "Initializing CLI service...");
    cli_service_init(NULL);
    ESP_LOGI(TAG, "Step 9: CLI service initialized");
    // Start CLI in background
    cli_service_start();
    ESP_LOGI(TAG, "Step 9: CLI service started");

    // Initialize sensor manager
    ESP_LOGI(TAG, "Initializing sensor manager...");
    sensor_manager_init();
    ESP_LOGI(TAG, "Step 10: Sensor manager initialized");

    // Initialize actuator manager
    ESP_LOGI(TAG, "Initializing actuator manager...");
    actuator_manager_init();
    ESP_LOGI(TAG, "Step 11: Actuator manager initialized");

    ESP_LOGI(TAG, "All components initialized successfully!");
    ESP_LOGI(TAG, "Checking config_has_wifi...");
    bool has_wifi = config_has_wifi();
    ESP_LOGI(TAG, "config_has_wifi = %d", has_wifi);

    // Check if provisioning is needed FIRST
    if (!config_has_wifi()) {
        ESP_LOGW(TAG, "No WiFi configuration found!");
        ESP_LOGI(TAG, "Starting Wi-Fi provisioning mode...");
        
        // Use new Wi-Fi service for provisioning
        wifi_service_start_provisioning(NULL);
        
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
    
    // Get WiFi credentials
    const char *ssid = config_get_wifi_ssid();
    const char *password = config_get_wifi_password();

    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "No WiFi SSID configured");
    } else {
        ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

        // Connect to WiFi
        int ret = network_connect(ssid, password);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to connect to WiFi: %d", ret);
        } else {
            // Wait for connection
            ret = network_wait_connected(30000);
            if (ret == 0) {
                ESP_LOGI(TAG, "WiFi connected!");
            } else {
                ESP_LOGW(TAG, "WiFi connection failed");
            }
        }
    }

    // Create main application task
    xTaskCreatePinnedToCore(app_main_task, "app_main", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Application started successfully!");
}
