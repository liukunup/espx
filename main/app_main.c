/* ESPX Application Main Entry

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Features:
   - HTTPS Web Server for device management
   - MQTT Client for cloud communication
   - Delta OTA for firmware updates
   - Manufacturing Provisioning support
   - Production test mode
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <esp_system.h>

#include "app/app.h"
#include "param_store/param_store.h"
#include "cert_manager/cert_manager.h"
#include "led_driver.h"
#include "web_server/web_server.h"
#include "mqtt_client/mqtt_client.h"
#include "ota_service/ota_service.h"
#include "mfg_provision/mfg_provision.h"
#include "test_mode/test_mode.h"

static const char *TAG = "app_main";

/**
 * @brief Print startup banner
 */
static void print_banner(void)
{
    printf("\n");
    printf("================================================\n");
    printf("           ESPX IoT Device Firmware\n");
    printf("================================================\n");
    printf("  Version  : %s\n", CONFIG_FIRMWARE_VERSION);
    printf("  Device   : %s\n", CONFIG_DEVICE_NAME);
    printf("  Chip     : ESP32-S3\n");
    printf("  Build    : %s %s\n", __DATE__, __TIME__);
    printf("================================================\n");
    printf("\n");
}

void app_main(void)
{
    // Print banner
    print_banner();

    // 1. Check for test mode trigger (GPIO low on boot)
    if (test_mode_check_trigger() == ESP_OK) {
        ESP_LOGI(TAG, "Test mode triggered, entering...");
        test_mode_enter();
        // Never returns
    }

    // 2. Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    ESP_ERROR_CHECK(nvs_flash_init());

    // 3. Initialize parameter store
    ESP_LOGI(TAG, "Initializing parameter store...");
    ESP_ERROR_CHECK(param_store_init());

    // 4. Load manufacturing data if available
    if (mfg_provision_has_data()) {
        ESP_LOGI(TAG, "Manufacturing data found, loading...");
        if (mfg_provision_load() == ESP_OK) {
            ESP_LOGI(TAG, "Manufacturing data loaded successfully");
        }
    }

    // 5. Initialize LED driver
    ESP_LOGI(TAG, "Initializing LED driver...");
    ESP_ERROR_CHECK(led_driver_init());
    led_set_status("connected");  // Default status

    // 6. Initialize certificate manager
    ESP_LOGI(TAG, "Initializing certificate manager...");
    if (cert_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize certificates");
    }

    // 7. Initialize Wi-Fi provisioning
    ESP_LOGI(TAG, "Initializing Wi-Fi provisioning...");
    ESP_ERROR_CHECK(wifi_prov_init());

    // Start provisioning (checks if already provisioned)
    ESP_LOGI(TAG, "Starting Wi-Fi provisioning...");
    ESP_ERROR_CHECK(wifi_prov_start(NULL));

    // Wait for Wi-Fi connection
    wifi_prov_wait_for_connection();

    ESP_LOGI(TAG, "Wi-Fi connected!");

    // 8. Initialize MQTT client
    ESP_LOGI(TAG, "Initializing MQTT client...");
    ESP_ERROR_CHECK(mqtt_client_init());
    ESP_ERROR_CHECK(mqtt_client_start());

    // 9. Initialize and start HTTPS Web server
    ESP_LOGI(TAG, "Starting HTTPS Web server...");
    if (web_server_start() == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTPS server");
    }

    // 10. Initialize OTA service
    ESP_LOGI(TAG, "Initializing OTA service...");
    ESP_ERROR_CHECK(ota_service_init());

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESPX device started successfully!");
    ESP_LOGI(TAG, "Access the web interface at: https://<device-ip>");
    ESP_LOGI(TAG, "================================================");

    // Main application loop
    app_loop();
}
