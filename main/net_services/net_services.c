/**
 * @file net_services.c
 * @brief Start the IP-dependent services
 */

#include <stdbool.h>
#include <esp_log.h>

#include "net_services.h"
#include "time_sync.h"
#include "mdns_service.h"
#include "esp_now_service.h"

static const char *TAG = "net_services";

esp_err_t net_services_start(void)
{
    esp_err_t err;

    /* Initialize ESP-NOW (before Wi-Fi is fully configured) */
    err = espx_espnow_init(false, false);  /* security=false, forward=false */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
    }

    /* NTP first: mDNS TXT records and the log benefit from a real clock, and
     * the sync itself needs no DNS. */
    err = time_sync_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTP start failed: %s", esp_err_to_name(err));
    }

    err = mdns_service_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS start failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

void net_services_start_esp_now(void)
{
    /* Start ESP-NOW after Wi-Fi is connected */
    espx_espnow_start();
}

bool net_services_time_ready(void)
{
    return time_sync_is_synced();
}
