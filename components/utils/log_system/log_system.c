/**
 * @file log_system.c
 * @brief Log System implementation with ring buffer
 */

#include "log_system.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "log_system";

/** @brief Log entry with full data */
typedef struct {
    uint32_t index;
    log_level_t level;
    uint32_t timestamp;
    char tag[LOG_MAX_TAG_LEN + 1];
    char message[128];
} internal_log_entry_t;

/** @brief Ring buffer for logs */
static internal_log_entry_t g_log_buffer[LOG_BUFFER_SIZE];

/** @brief Current write index */
static uint32_t g_write_index = 0;

/** @brief Total logs ever written */
static uint32_t g_total_count = 0;

/** @brief Current log level */
static log_level_t g_log_level = LOG_LEVEL_INFO;

/** @brief Mutex for thread safety */
static SemaphoreHandle_t g_log_mutex = NULL;

/** @brief Whether system is initialized */
static bool g_initialized = false;

int log_system_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Log system already initialized");
        return 0;
    }

    // Create mutex
    g_log_mutex = xSemaphoreCreateMutex();
    if (g_log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create log mutex");
        return -1;
    }

    // Clear buffer
    memset(g_log_buffer, 0, sizeof(g_log_buffer));
    g_write_index = 0;
    g_total_count = 0;

    g_initialized = true;
    ESP_LOGI(TAG, "Log system initialized, buffer size: %d", LOG_BUFFER_SIZE);
    return 0;
}

int log_set_level(log_level_t level) {
    if (level > LOG_LEVEL_VERBOSE) {
        level = LOG_LEVEL_VERBOSE;
    }
    g_log_level = level;
    return 0;
}

log_level_t log_get_level(void) {
    return g_log_level;
}

int log_write(log_level_t level, const char *tag, const char *format, ...) {
    if (!g_initialized) {
        // Fall back to ESP_LOG
        if (level <= LOG_LEVEL_ERROR) {
            ESP_LOGE(tag, "%s", format);
        } else if (level <= LOG_LEVEL_WARN) {
            ESP_LOGW(tag, "%s", format);
        } else if (level <= LOG_LEVEL_INFO) {
            ESP_LOGI(tag, "%s", format);
        } else if (level <= LOG_LEVEL_DEBUG) {
            ESP_LOGD(tag, "%s", format);
        } else {
            ESP_LOGV(tag, "%s", format);
        }
        return -1;
    }

    // Check log level
    if (level > g_log_level) {
        return 0;
    }

    // Acquire mutex
    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -2;
    }

    // Create log entry
    internal_log_entry_t entry;
    entry.index = g_write_index;
    entry.level = level;
    entry.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    // Copy tag (truncate if needed)
    strncpy(entry.tag, tag, LOG_MAX_TAG_LEN);
    entry.tag[LOG_MAX_TAG_LEN] = '\0';

    // Format message
    va_list args;
    va_start(args, format);
    vsnprintf(entry.message, sizeof(entry.message), format, args);
    va_end(args);

    // Write to ring buffer
    g_log_buffer[g_write_index % LOG_BUFFER_SIZE] = entry;
    g_write_index++;
    g_total_count++;

    // Release mutex
    xSemaphoreGive(g_log_mutex);

    // Also output to ESP_LOG
    if (level <= LOG_LEVEL_ERROR) {
        ESP_LOGE(tag, "%s", entry.message);
    } else if (level <= LOG_LEVEL_WARN) {
        ESP_LOGW(tag, "%s", entry.message);
    } else if (level <= LOG_LEVEL_INFO) {
        ESP_LOGI(tag, "%s", entry.message);
    } else if (level <= LOG_LEVEL_DEBUG) {
        ESP_LOGD(tag, "%s", entry.message);
    } else {
        ESP_LOGV(tag, "%s", entry.message);
    }

    return (int)strlen(entry.message);
}

int log_read_entries(log_entry_t *entries, int max_count, uint32_t start_index) {
    if (!g_initialized || entries == NULL || max_count <= 0) {
        return -1;
    }

    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -2;
    }

    int read_count = 0;
    uint32_t current_index = start_index;

    // Find the starting point
    if (g_total_count == 0) {
        xSemaphoreGive(g_log_mutex);
        return 0;
    }

    // If start_index is 0, start from oldest
    if (start_index == 0) {
        if (g_total_count > LOG_BUFFER_SIZE) {
            current_index = g_write_index - LOG_BUFFER_SIZE;
        } else {
            current_index = 0;
        }
    }

    while (read_count < max_count && current_index < g_write_index) {
        if (current_index >= g_write_index - LOG_BUFFER_SIZE) {
            // Entry is in buffer
            int buf_idx = current_index % LOG_BUFFER_SIZE;
            entries[read_count].index = g_log_buffer[buf_idx].index;
            entries[read_count].level = g_log_buffer[buf_idx].level;
            entries[read_count].timestamp = g_log_buffer[buf_idx].timestamp;
            strncpy(entries[read_count].tag, g_log_buffer[buf_idx].tag, sizeof(entries[read_count].tag) - 1);
            entries[read_count].tag[sizeof(entries[read_count].tag) - 1] = '\0';
            strncpy(entries[read_count].message, g_log_buffer[buf_idx].message, sizeof(entries[read_count].message) - 1);
            entries[read_count].message[sizeof(entries[read_count].message) - 1] = '\0';
            read_count++;
        }
        current_index++;
    }

    xSemaphoreGive(g_log_mutex);
    return read_count;
}

uint32_t log_get_count(void) {
    if (!g_initialized) {
        return 0;
    }

    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    uint32_t count = g_total_count;
    xSemaphoreGive(g_log_mutex);
    return count;
}

void log_clear(void) {
    if (!g_initialized) {
        return;
    }

    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(g_log_buffer, 0, sizeof(g_log_buffer));
        g_write_index = 0;
        g_total_count = 0;
        xSemaphoreGive(g_log_mutex);
    }
}

int log_export_recent(char *buffer, size_t len, int count) {
    if (!g_initialized || buffer == NULL || len == 0) {
        return -1;
    }

    log_entry_t entries[LOG_BUFFER_SIZE];
    int actual_count = log_read_entries(entries, count > LOG_BUFFER_SIZE ? LOG_BUFFER_SIZE : count, 0);

    if (actual_count <= 0) {
        return -2;
    }

    // Build JSON array
    char *p = buffer;
    size_t remaining = len;
    int written = snprintf(p, remaining, "[");
    p += written;
    remaining -= written;

    for (int i = 0; i < actual_count; i++) {
        const char *level_str[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"};
        int level_idx = entries[i].level;

        written = snprintf(p, remaining,
            "%s{\"idx\":%u,\"level\":\"%s\",\"ts\":%u,\"tag\":\"%s\",\"msg\":\"%s\"}",
            i > 0 ? "," : "",
            entries[i].index,
            level_idx >= 0 && level_idx <= 5 ? level_str[level_idx] : "UNKNOWN",
            entries[i].timestamp,
            entries[i].tag,
            entries[i].message);
        p += written;
        remaining -= written;

        if (remaining <= 0) break;
    }

    written = snprintf(p, remaining, "]");
    return 0;
}

int log_get_entry(uint32_t index, log_entry_t *entry) {
    if (!g_initialized || entry == NULL) {
        return -1;
    }

    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -2;
    }

    // Check if index is in buffer
    if (index >= g_write_index || index < g_write_index - LOG_BUFFER_SIZE) {
        xSemaphoreGive(g_log_mutex);
        return -3;  // Not found
    }

    int buf_idx = index % LOG_BUFFER_SIZE;
    entry->index = g_log_buffer[buf_idx].index;
    entry->level = g_log_buffer[buf_idx].level;
    entry->timestamp = g_log_buffer[buf_idx].timestamp;
    strncpy(entry->tag, g_log_buffer[buf_idx].tag, sizeof(entry->tag) - 1);
    entry->tag[sizeof(entry->tag) - 1] = '\0';
    strncpy(entry->message, g_log_buffer[buf_idx].message, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';

    xSemaphoreGive(g_log_mutex);
    return 0;
}
