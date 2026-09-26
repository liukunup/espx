/**
 * @file net_services.c
 * @brief Start the IP-dependent services
 */

#include <stdbool.h>
#include <esp_log.h>

#include "net_services.h"
#include "time_sync.h"
#include "mdns_service.h"

static const char *TAG = "net_services";

esp_err_t net_services_start(void)
{
    esp_err_t err;

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

bool net_services_time_ready(void)
{
    return time_sync_is_synced();
}
