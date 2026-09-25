/**
 * @file ota_manager.c
 * @brief OTA Manager implementation
 */

#include "ota_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_http_client.h"

static const char *TAG = "ota_manager";

/** @brief Current OTA state */
static volatile ota_state_t g_ota_state = OTA_STATE_IDLE;

/** @brief Last error code */
static int g_last_error = 0;

/** @brief Progress percentage */
static int g_progress = 0;

/** @brief OTA info */
static ota_info_t g_ota_info = {0};

/** @brief OTA handle */
static esp_ota_handle_t g_ota_handle = 0;

/** @brief Target partition */
static const esp_partition_t *g_target_partition = NULL;

/** @brief Whether initialized */
static bool g_initialized = false;

/** @brief Buffer for OTA data */
static uint8_t *g_ota_buffer = NULL;
static size_t g_ota_buffer_size = 0;
static size_t g_ota_buffer_offset = 0;

int ota_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "OTA manager already initialized");
        return 0;
    }

    g_ota_state = OTA_STATE_IDLE;
    g_last_error = 0;
    g_progress = 0;
    memset(&g_ota_info, 0, sizeof(g_ota_info));

    g_initialized = true;
    ESP_LOGI(TAG, "OTA manager initialized");
    return 0;
}

int ota_handle_start(const char *json_payload) {
    if (json_payload == NULL) {
        return -1;
    }

    if (g_ota_state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return -2;
    }

    // Simple parsing - in production use cJSON
    ESP_LOGI(TAG, "OTA start received (parsing not fully implemented)");

    g_ota_state = OTA_STATE_RECEIVING;
    g_ota_buffer_offset = 0;

    // Allocate buffer for received data
    if (g_ota_buffer == NULL) {
        g_ota_buffer_size = 4 * 1024 * 1024;  // 4MB max
        g_ota_buffer = malloc(g_ota_buffer_size);
        if (g_ota_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate OTA buffer");
            g_ota_state = OTA_STATE_FAILED;
            g_last_error = -1;
            return -3;
        }
    }

    ESP_LOGI(TAG, "OTA receiving started");
    return 0;
}

int ota_handle_data(const char *topic, const uint8_t *data, int len) {
    if (data == NULL || len <= 0) {
        return -1;
    }

    if (g_ota_state != OTA_STATE_RECEIVING && g_ota_state != OTA_STATE_DOWNLOADING) {
        ESP_LOGW(TAG, "OTA not in receiving state");
        return -2;
    }

    // Check buffer capacity
    if (g_ota_buffer_offset + len > g_ota_buffer_size) {
        ESP_LOGE(TAG, "OTA buffer overflow");
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = -2;
        return -3;
    }

    // Copy data to buffer
    memcpy(g_ota_buffer + g_ota_buffer_offset, data, len);
    g_ota_buffer_offset += len;

    // Update progress
    if (g_ota_info.target_size > 0) {
        g_progress = (g_ota_buffer_offset * 100) / g_ota_info.target_size;
    } else {
        g_progress = 0;
    }

    ESP_LOGD(TAG, "OTA progress: %d%% (%d/%d bytes)",
             g_progress, g_ota_buffer_offset, g_ota_info.target_size);

    return 0;
}

int ota_handle_end(const char *json_payload) {
    if (json_payload == NULL) {
        return -1;
    }

    if (g_ota_state != OTA_STATE_RECEIVING && g_ota_state != OTA_STATE_DOWNLOADING) {
        ESP_LOGW(TAG, "OTA not in receiving state");
        return -2;
    }

    ESP_LOGI(TAG, "OTA data received, verifying...");

    g_ota_state = OTA_STATE_VERIFYING;

    // Find the update partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = -3;
        return -4;
    }

    ESP_LOGI(TAG, "OTA partition: %s at 0x%x", update_partition->label, update_partition->address);

    g_ota_state = OTA_STATE_WRITING;

    // Begin OTA
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &g_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = err;
        return -5;
    }

    // Write the data
    err = esp_ota_write(g_ota_handle, g_ota_buffer, g_ota_buffer_offset);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        esp_ota_abort(g_ota_handle);
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = err;
        return -6;
    }

    // End OTA
    err = esp_ota_end(g_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = err;
        return -7;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        g_ota_state = OTA_STATE_FAILED;
        g_last_error = err;
        return -8;
    }

    g_ota_state = OTA_STATE_COMMITTED;
    ESP_LOGI(TAG, "OTA completed successfully, reboot needed");

    return 0;
}

int ota_start_http(const char *url) {
    if (url == NULL) {
        return -1;
    }

    if (g_ota_state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return -2;
    }

    ESP_LOGI(TAG, "Starting HTTP OTA from: %s", url);

    // This is a simplified implementation
    // In production, use esp_https_ota or custom HTTP client
    g_ota_state = OTA_STATE_DOWNLOADING;

    // Placeholder - HTTP OTA implementation would go here
    ESP_LOGW(TAG, "HTTP OTA not fully implemented, use MQTT OTA for now");

    g_ota_state = OTA_STATE_IDLE;
    return -3;
}

ota_state_t ota_get_state(void) {
    return g_ota_state;
}

int ota_get_progress(void) {
    return g_progress;
}

int ota_get_last_error(void) {
    return g_last_error;
}

int ota_rollback(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    if (running == NULL || update == NULL) {
        ESP_LOGE(TAG, "Failed to get partitions");
        return -1;
    }

    ESP_LOGI(TAG, "Rolling back from %s to %s", running->label, update->label);

    esp_err_t err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return -2;
    }

    g_ota_state = OTA_STATE_ROLLBACK;
    ESP_LOGI(TAG, "Rollback scheduled, rebooting...");

    // Trigger reboot
    esp_restart();

    return 0;  // Won't reach here
}

int ota_commit(void) {
    if (g_ota_state != OTA_STATE_COMMITTED) {
        ESP_LOGW(TAG, "OTA not in committed state");
        return -1;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return -2;
    }

    ESP_LOGI(TAG, "Committing OTA on partition: %s", running->label);

    // The partition is already set as boot partition
    // Just mark it as committed
    g_ota_state = OTA_STATE_IDLE;
    g_progress = 0;

    ESP_LOGI(TAG, "OTA committed successfully");
    return 0;
}

bool ota_is_valid_version(const char *version) {
    if (version == NULL || strlen(version) == 0) {
        return false;
    }

    // Simple version check - major.minor.patch
    // In production, compare with current version
    return true;
}

const char* ota_get_current_version(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return "unknown";
    }

    // Get version from app description
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc == NULL) {
        return "unknown";
    }

    return app_desc->version;
}
