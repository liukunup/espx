/**
 * @file sys_info.c
 * @brief Node status snapshot shared by the HTTP API and the MQTT reporter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <cJSON.h>

#include "sys_info.h"
#include "sys_stats.h"
#include "time_sync.h"
#include "mdns_service.h"
#include "ws_server.h"

void sys_info_add(cJSON *json)
{
    if (json == NULL) {
        return;
    }

    cJSON_AddNumberToObject(json, "uptime",
                            (double)(esp_timer_get_time() / 1000000ULL));
    cJSON_AddNumberToObject(json, "free_heap",
                            (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "min_free_heap",
                            (double)esp_get_minimum_free_heap_size());

    /* esp_get_free_heap_size() includes PSRAM, which hides the pool that
     * actually runs out: mbedTLS handshake buffers and Wi-Fi come from internal
     * RAM. A TLS session that fails to be created is almost always this figure,
     * not the headline one. */
    cJSON_AddNumberToObject(json, "free_heap_internal",
        (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(json, "min_free_heap_internal",
        (double)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    /* Clock: null until NTP has set it, so a client can tell the difference
     * between "midnight 1970" and "not synced yet". */
    cJSON *epoch = cJSON_AddNumberToObject(json, "epoch",
                                           (double)time_sync_epoch());
    if (epoch && time_sync_epoch() == 0) {
        cJSON_DeleteItemFromObject(json, "epoch");
        cJSON_AddNullToObject(json, "epoch");
    }
    char iso[32];
    if (time_sync_iso8601(iso, sizeof(iso)) == ESP_OK) {
        cJSON_AddStringToObject(json, "time", iso);
    } else {
        cJSON_AddNullToObject(json, "time");
    }
    cJSON_AddBoolToObject(json, "time_synced", time_sync_is_synced());
    cJSON_AddStringToObject(json, "ntp_server", time_sync_server());
    cJSON_AddStringToObject(json, "timezone", time_sync_timezone());

    if (mdns_service_is_running()) {
        cJSON_AddStringToObject(json, "mdns", mdns_service_fqdn());
    }
    cJSON_AddNumberToObject(json, "ws_clients", ws_server_client_count());

    /* CPU load and RAM. Internal RAM is reported separately from the total
     * because it is the pool that actually runs out. */
    sys_stats_t st;
    sys_stats_get(&st);

    cJSON *cpu = cJSON_AddObjectToObject(json, "cpu");
    cJSON_AddNumberToObject(cpu, "freq_mhz", st.cpu_freq_mhz);
    cJSON_AddNumberToObject(cpu, "cores", 2);
    if (st.cpu_valid) {
        cJSON_AddNumberToObject(cpu, "usage", (double)st.cpu_usage);
    } else {
        cJSON_AddNullToObject(cpu, "usage");
    }
    cJSON_AddNumberToObject(cpu, "tasks", st.task_count);

    cJSON *ram = cJSON_AddObjectToObject(json, "ram");
    /* Internal RAM is the pool that runs out (Wi-Fi, TLS, PSA crypto);
     * the headline free-heap figure includes PSRAM and hides it. Report it
     * separately so exhaustion is visible before it fails an allocation. */
    cJSON_AddNumberToObject(ram, "iram_free",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(ram, "iram_min",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(ram, "iram_total",
                            heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(ram, "internal_total", (double)st.int_total);
    cJSON_AddNumberToObject(ram, "internal_free", (double)st.int_free);
    cJSON_AddNumberToObject(ram, "internal_min_free", (double)st.int_min_free);
    cJSON_AddNumberToObject(ram, "internal_largest", (double)st.int_largest);
    cJSON_AddNumberToObject(ram, "internal_used_pct",
                            (double)sys_stats_internal_used_pct(&st));
    if (st.psram_present) {
        cJSON_AddNumberToObject(ram, "psram_total", (double)st.psram_total);
        cJSON_AddNumberToObject(ram, "psram_free", (double)st.psram_free);
        cJSON_AddNumberToObject(ram, "psram_used_pct",
                                (double)sys_stats_psram_used_pct(&st));
    }

    /* Per-task stack headroom. A task below ~1 KB free is about to overflow;
     * this is how a too-small stack shows up before it crashes. */
    cJSON *tasks = cJSON_AddArrayToObject(json, "tasks");
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *status = malloc(n * sizeof(TaskStatus_t));
    if (status != NULL) {
        n = uxTaskGetSystemState(status, n, NULL);
        for (UBaseType_t i = 0; i < n; i++) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "name", status[i].pcTaskName);
            /* High-water mark is in words on ESP-IDF's portmacro. */
            cJSON_AddNumberToObject(t, "stack_free_bytes",
                                    (double)status[i].usStackHighWaterMark *
                                        sizeof(StackType_t));
            cJSON_AddNumberToObject(t, "priority", status[i].uxCurrentPriority);
            cJSON_AddItemToArray(tasks, t);
        }
        free(status);
    }

    /* Wi-Fi state + IP */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(json, "wifi_ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(json, "wifi_rssi", ap.rssi);
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(json, "ip", buf);
        }
    }
}

cJSON *sys_info_build(void)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    sys_info_add(json);
    return json;
}
