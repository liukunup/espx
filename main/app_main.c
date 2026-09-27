/* ESPX Application Main Entry */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_netif_types.h>

#include "task_util.h"
#include "app_info.h"
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
#include "net_services/net_services.h"
#include "core/sys_stats.h"
#include "core/defaults.h"
#include "at_service/at_service.h"

static const char *TAG = "app_main";

static void print_banner(void)
{
    printf("\n================================================\n");
    printf("           ESPX IoT Device Firmware\n");
    printf("================================================\n");
    printf("  Version  : %s\n", app_version());
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

    /* NTP and mDNS need an address, so they start here rather than in the boot
     * sequence (which must not block on the network). */
    net_services_start();

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

    /* ---- 3. Serial AT interface ----------------------------------------- */
    /* Started before the network so a host MCU can talk to the node even while
     * it is still unprovisioned. */
#ifdef CONFIG_ESPX_AT_ENABLE
    if (at_service_start() != ESP_OK) {
        ESP_LOGW(TAG, "AT service failed to start");
    }
#endif

    /* ---- 4. Configuration ---------------------------------------------- */
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

    /* ---- 6. Defaults ---------------------------------------------------- */
    /* A node with nothing bound is indistinguishable from a broken one; give a
     * fresh unit its on-board LED so there is something to see and control.
     * Runs after factory provisioning, which takes precedence. */
    defaults_seed_once();

    /* ---- 7. Network ---------------------------------------------------- */
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
    /* mqtt_client_start returns ESP_ERR_INVALID_ARG when broker is not configured;
     * log but don't fail — the node can still run with web UI only. */
    err = mqtt_client_start();
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "MQTT disabled — configure broker to enable");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(mqtt_publisher_start());

    /* ---- 10. OTA -------------------------------------------------------- */
    /* Runtime statistics (CPU load, RAM) for /api/system/info and the UI. */
    sys_stats_start();

    ESP_LOGI(TAG, "Initializing OTA service...");
    ESP_ERROR_CHECK(ota_service_init());
    /* This firmware booted; cancel any pending rollback. */
    ota_service_mark_valid();

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESPX ready — device id %s, %d peripheral(s) bound",
             node_config_get_device_id(), (int)device_get_count());
    // Get Wi-Fi IP for logging
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            char ip_str[16];
            esp_ip4addr_ntoa(&ip.ip, ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "Web UI: https://%s/  (self-signed certificate)", ip_str);
        } else {
            ESP_LOGI(TAG, "Web UI: https://<device-ip>/  (self-signed certificate)");
        }
    } else {
        ESP_LOGI(TAG, "Web UI: https://<device-ip>/  (self-signed certificate)");
    }
    ESP_LOGI(TAG, "Config: POST /api/config or MQTT <prefix>/cmd/config (YAML)");
    ESP_LOGI(TAG, "================================================");

    event_bus_publish(EVENT_NODE_READY, NULL, NULL);

    /* ---- 11. Report the Wi-Fi connection when it arrives ---------------- */
    /* In its own task so it can never gate the services above. */
    /* Internal-RAM stack: it starts mDNS, which persists its hostname to NVS. */
    xTaskCreate(wifi_status_task, "wifi_status", 3072, NULL, 3, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        /* Internal RAM is the pool that runs out (Wi-Fi, TLS sessions, PSA
         * crypto); the headline free-heap figure includes PSRAM and hides it.
         * Logging the minimum makes a leak or a slow exhaustion visible. */
        ESP_LOGI(TAG, "heap: internal free %u, internal min %u, total free %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)esp_get_free_heap_size());
    }
}
