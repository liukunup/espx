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
#include "test_mode/test_mode.h"
#include "mfg_provision/mfg_provision.h"
#include "ota_service/ota_service.h"

static const char *TAG = "app_main";

static void print_banner(void)
{
    printf("\n================================================\n");
    printf("           ESPX IoT Device Firmware\n");
    printf("================================================\n");
    printf("  Version  : %s\n", CONFIG_FIRMWARE_VERSION);
    printf("  Device   : %s\n", CONFIG_DEVICE_NAME);
    printf("  Chip     : ESP32-S3\n");
    printf("  Build    : %s %s\n", __DATE__, __TIME__);
    printf("================================================\n\n");
}

/**
 * @brief Report the Wi-Fi connection without blocking startup
 */
static void wifi_status_task(void *arg)
{
    wifi_prov_wait_for_connection();
    ESP_LOGI(TAG, "Wi-Fi connected");
    led_set_status("connected");
    vTaskDelete(NULL);
}

void app_main(void)
{
    print_banner();

    /* ---- 1. Storage ---------------------------------------------------- */
    /* NVS must be initialised before the test-mode check reads its request flag. */
    ESP_LOGI(TAG, "Initializing NVS...");
    ESP_ERROR_CHECK(nvs_flash_init());

    /* ---- 2. Manufacturing test mode ------------------------------------ */
    /* Entered via an NVS request flag, or TEST_MODE_GPIO when configured. */
    if (test_mode_check_trigger() == ESP_OK) {
        ESP_LOGI(TAG, "Test mode triggered, entering self-test console");
        test_mode_enter();              /* never returns */
    }

    /* Start the BOOT long-press watchdog early: Wi-Fi provisioning below blocks
     * until credentials arrive, and a factory-fresh device must still be able
     * to reach test mode. */
    test_mode_start_longpress_watchdog();

    /* ---- 3. Configuration ---------------------------------------------- */
    ESP_LOGI(TAG, "Initializing configuration...");
    ESP_ERROR_CHECK(node_config_init());
    ESP_ERROR_CHECK(node_config_load());

    /* ---- 4. Core services ---------------------------------------------- */
    ESP_LOGI(TAG, "Initializing core services...");
    ESP_ERROR_CHECK(event_bus_init());
    ESP_ERROR_CHECK(device_type_registry_init());
    ESP_ERROR_CHECK(peripherals_register_all());
    ESP_ERROR_CHECK(device_manager_init());
    ESP_ERROR_CHECK(device_manager_load());

    /* ---- 5. Factory provisioning -------------------------------------- */
    /* Applied after node_config and the device manager are up, because it
     * writes into both. Also supplies pre-provisioned Wi-Fi credentials. */
    if (mfg_provision_has_data()) {
        ESP_LOGI(TAG, "Applying factory configuration...");
        if (mfg_provision_load() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to apply factory configuration");
        }
    }

    /* ---- 6. Network ---------------------------------------------------- */
    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    ESP_ERROR_CHECK(wifi_prov_init());
    ESP_ERROR_CHECK(wifi_prov_start(NULL));

    /* ---- 7. HTTPS ------------------------------------------------------- */
    /* Started before waiting for the station connection: during provisioning
     * the device's own SoftAP is up, so an installer can reach the
     * configuration UI in a browser without a mobile app. */
    ESP_LOGI(TAG, "Initializing certificate manager...");
    ESP_ERROR_CHECK(cert_manager_init());

    ESP_LOGI(TAG, "Starting HTTPS web server...");
    esp_err_t err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(err));
    }

    /* ---- 8. Status LED -------------------------------------------------- */
    ESP_ERROR_CHECK(led_driver_init());

    /* ---- 9. MQTT -------------------------------------------------------- */
    /* Deliberately not gated on a station connection: esp-mqtt retries until
     * the network is up, so a node whose Wi-Fi is down (or one that is still
     * only serving its own SoftAP) keeps its services alive instead of
     * stalling in a blocking wait. */
    ESP_LOGI(TAG, "Initializing MQTT...");
    ESP_ERROR_CHECK(mqtt_client_init());
    ESP_ERROR_CHECK(mqtt_commander_init());
    ESP_ERROR_CHECK(mqtt_publisher_init());
    ESP_ERROR_CHECK(mqtt_client_start());
    ESP_ERROR_CHECK(mqtt_publisher_start());

    /* ---- 10. OTA -------------------------------------------------------- */
    ESP_LOGI(TAG, "Initializing OTA service...");
    ESP_ERROR_CHECK(ota_service_init());
    /* This firmware booted; cancel any pending rollback. */
    ota_service_mark_valid();

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESPX ready — device id %s, %d device(s) bound",
             node_config_get_device_id(), (int)device_get_count());
    ESP_LOGI(TAG, "Web UI: https://<device-ip>/  (self-signed certificate)");
    ESP_LOGI(TAG, "Config: POST /api/config or MQTT <prefix>/cmd/config (YAML)");
    ESP_LOGI(TAG, "================================================");

    event_bus_publish(EVENT_NODE_READY, NULL, NULL);

    /* ---- 11. Report the Wi-Fi connection when it arrives ---------------- */
    /* In its own task so it can never gate the services above. */
    xTaskCreate(wifi_status_task, "wifi_status", 3072, NULL, 3, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "idle");
    }
}
