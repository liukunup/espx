/**
 * @file health_monitor.c
 * @brief Health Monitor implementation
 */

#include "health_monitor.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"

static const char *TAG = "health_monitor";

/** @brief Maximum number of watchdogs */
#define MAX_WATCHDOGS 8

/** @brief Watchdog entry */
typedef struct {
    char task_name[16];
    uint32_t timeout_ms;
    uint32_t last_feed;
    bool active;
} watchdog_entry_t;

/** @brief Watchdog table */
static watchdog_entry_t g_watchdogs[MAX_WATCHDOGS];

/** @brief Reset counter */
static int g_reset_count = 0;

/** @brief Last reset reason */
static reset_reason_t g_last_reset_reason = RESET_REASON_POWER_ON;

/** @brief System metrics */
static struct {
    uint32_t mqtt_reconnect_count;
    uint32_t mqtt_packets_sent;
    uint32_t mqtt_packets_failed;
    uint32_t sensor_read_count;
    uint32_t sensor_error_count;
    uint32_t min_free_heap;
} g_metrics = {0};

/** @brief Boot time */
static uint64_t g_boot_time = 0;

/** @brief Whether initialized */
static bool g_initialized = false;

/**
 * @brief Get reset reason from ESP32
 */
static reset_reason_t get_esp_reset_reason(void) {
    esp_reset_reason_t reason = esp_reset_reason();

    switch (reason) {
    case ESP_RST_POWERON:
        return RESET_REASON_POWER_ON;
    case ESP_RST_SW:
        return RESET_REASON_SW;
    case ESP_RST_PANIC:
        return RESET_REASON_PANIC;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return RESET_REASON_WDT;
    case ESP_RST_BROWNOUT:
        return RESET_REASON_BROWNOUT;
    default:
        return RESET_REASON_POWER_ON;
    }
}

const char* health_get_reset_reason_string(reset_reason_t reason) {
    switch (reason) {
    case RESET_REASON_POWER_ON: return "Power On";
    case RESET_REASON_SW: return "Software Reset";
    case RESET_REASON_PANIC: return "Panic";
    case RESET_REASON_EXCEPTION: return "Exception";
    case RESET_REASON_WDT: return "Watchdog";
    case RESET_REASON_BROWNOUT: return "Brownout";
    case RESET_REASON_OTA: return "OTA Update";
    default: return "Unknown";
    }
}

int health_monitor_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Health monitor already initialized");
        return 0;
    }

    // Get reset reason
    g_last_reset_reason = get_esp_reset_reason();
    ESP_LOGI(TAG, "Last reset reason: %s", health_get_reset_reason_string(g_last_reset_reason));

    // Record boot time
    g_boot_time = esp_timer_get_time() / 1000;  // ms

    // Initialize metrics
    g_metrics.min_free_heap = esp_get_minimum_free_heap_size();

    // Clear watchdog table
    memset(g_watchdogs, 0, sizeof(g_watchdogs));

    g_initialized = true;
    g_reset_count++;

    ESP_LOGI(TAG, "Health monitor initialized");
    return 0;
}

int watchdog_register(const char *task_name, uint32_t timeout_ms) {
    if (task_name == NULL || timeout_ms == 0) {
        return -1;
    }

    // Find slot
    int slot = -1;
    for (int i = 0; i < MAX_WATCHDOGS; i++) {
        if (!g_watchdogs[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ESP_LOGE(TAG, "No free watchdog slot");
        return -2;
    }

    watchdog_entry_t *wd = &g_watchdogs[slot];
    strncpy(wd->task_name, task_name, sizeof(wd->task_name) - 1);
    wd->task_name[sizeof(wd->task_name) - 1] = '\0';
    wd->timeout_ms = timeout_ms;
    wd->last_feed = (uint32_t)(esp_timer_get_time() / 1000);
    wd->active = true;

    ESP_LOGD(TAG, "Registered watchdog for %s, timeout=%ums", task_name, timeout_ms);
    return 0;
}

int watchdog_feed(const char *task_name) {
    if (task_name == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_WATCHDOGS; i++) {
        if (g_watchdogs[i].active && strcmp(g_watchdogs[i].task_name, task_name) == 0) {
            g_watchdogs[i].last_feed = (uint32_t)(esp_timer_get_time() / 1000);
            return 0;
        }
    }

    return -2;  // Not found
}

int watchdog_unregister(const char *task_name) {
    if (task_name == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_WATCHDOGS; i++) {
        if (g_watchdogs[i].active && strcmp(g_watchdogs[i].task_name, task_name) == 0) {
            g_watchdogs[i].active = false;
            return 0;
        }
    }

    return -2;  // Not found
}

int health_report_status(void) {
    // This would typically publish to MQTT
    ESP_LOGI(TAG, "Status report requested (implement MQTT publish)");

    metrics_t metrics;
    metrics_collect(&metrics);

    ESP_LOGI(TAG, "Free heap: %u (min: %u)", metrics.free_heap_current, metrics.free_heap_min);
    ESP_LOGI(TAG, "Uptime: %lu ms", (uint32_t)(esp_timer_get_time() / 1000 - g_boot_time));

    return 0;
}

int health_report_metrics(void) {
    return health_report_status();
}

int metrics_collect(metrics_t *metrics) {
    if (metrics == NULL) {
        return -1;
    }

    metrics->timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    metrics->free_heap_current = esp_get_free_heap_size();

    uint32_t min_heap = esp_get_minimum_free_heap_size();
    if (min_heap < g_metrics.min_free_heap) {
        g_metrics.min_free_heap = min_heap;
    }
    metrics->free_heap_min = g_metrics.min_free_heap;

    metrics->mqtt_reconnect_count = g_metrics.mqtt_reconnect_count;
    metrics->mqtt_packets_sent = g_metrics.mqtt_packets_sent;
    metrics->mqtt_packets_failed = g_metrics.mqtt_packets_failed;
    metrics->sensor_read_count = g_metrics.sensor_read_count;
    metrics->sensor_error_count = g_metrics.sensor_error_count;

    if (g_metrics.sensor_read_count > 0) {
        metrics->sensor_success_rate = 100.0f *
            (g_metrics.sensor_read_count - g_metrics.sensor_error_count) /
            g_metrics.sensor_read_count;
    } else {
        metrics->sensor_success_rate = 100.0f;
    }

    return 0;
}

int metrics_export_json(char *buffer, size_t len) {
    if (buffer == NULL || len == 0) {
        return -1;
    }

    metrics_t metrics;
    metrics_collect(&metrics);

    int written = snprintf(buffer, len,
        "{"
        "\"timestamp\":%lu,"
        "\"free_heap_current\":%lu,"
        "\"free_heap_min\":%lu,"
        "\"mqtt_reconnect_count\":%d,"
        "\"mqtt_packets_sent\":%lu,"
        "\"mqtt_packets_failed\":%lu,"
        "\"sensor_read_count\":%lu,"
        "\"sensor_error_count\":%lu,"
        "\"sensor_success_rate\":%.2f,"
        "\"uptime\":%llu"
        "}",
        metrics.timestamp,
        metrics.free_heap_current,
        metrics.free_heap_min,
        metrics.mqtt_reconnect_count,
        metrics.mqtt_packets_sent,
        metrics.mqtt_packets_failed,
        metrics.sensor_read_count,
        metrics.sensor_error_count,
        metrics.sensor_success_rate,
        (unsigned long long)(esp_timer_get_time() / 1000 - g_boot_time)
    );

    return (written > 0 && written < (int)len) ? 0 : -2;
}

void register_panic_handler(void) {
    // In production, register esp_panic_handler or custom handler
    ESP_LOGI(TAG, "Panic handler registration (placeholder)");
}

int health_get_reset_count(void) {
    return g_reset_count;
}

reset_reason_t health_get_last_reset_reason(void) {
    return g_last_reset_reason;
}

uint32_t health_get_min_free_heap(void) {
    return g_metrics.min_free_heap;
}

uint32_t health_get_free_heap(void) {
    return esp_get_free_heap_size();
}

void health_inc_mqtt_reconnect(void) {
    g_metrics.mqtt_reconnect_count++;
}

void health_inc_mqtt_sent(void) {
    g_metrics.mqtt_packets_sent++;
}

void health_inc_mqtt_failed(void) {
    g_metrics.mqtt_packets_failed++;
}

void health_inc_sensor_read(void) {
    g_metrics.sensor_read_count++;
}

void health_inc_sensor_error(void) {
    g_metrics.sensor_error_count++;
}
