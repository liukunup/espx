/**
 * @file ota_service.c
 * @brief OTA Service implementation using esp_delta_ota
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_delta_ota.h>

#include "ota_service.h"
#include "param_store/param_store.h"

static const char *TAG = "ota_service";

static ota_state_t g_state = OTA_STATE_IDLE;
static float g_progress = 0.0f;
static char g_version[32] = {0};
static char g_error_msg[128] = {0};
static ota_event_cb_t g_callback = NULL;
static void *g_user_data = NULL;
static bool g_cancelled = false;
static TaskHandle_t g_ota_task = NULL;

/**
 * @brief Update state and notify callback
 */
static void update_state(ota_state_t new_state, float progress)
{
    g_state = new_state;
    g_progress = progress;

    if (g_callback) {
        g_callback(new_state, progress, g_user_data);
    }

    ESP_LOGI(TAG, "OTA State: %d, Progress: %.1f%%", new_state, progress * 100);
}

/**
 * @brief HTTP event handler for OTA download
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_PROGRESS:
        if (evt->response_totlen > 0) {
            g_progress = (float)evt->progress / evt->response_totlen;
        }
        break;
    case HTTP_EVENT_ON_ERROR:
        snprintf(g_error_msg, sizeof(g_error_msg), "HTTP error");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief OTA task
 */
static void ota_task(void *pv参数)
{
    char *url = (char *)pv参数;
    esp_err_t err;

    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    update_state(OTA_STATE_DOWNLOADING, 0.0f);

    // Configure HTTPS OTA
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    // Use delta OTA if available
    esp_delta_ota_config_t delta_config = {
        .http_config = &config,
    };

    // Try delta OTA first, fall back to regular OTA
    err = esp_delta_ota(&delta_config);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Delta OTA not supported, using regular OTA");
        err = esp_https_ota(&config);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        snprintf(g_error_msg, sizeof(g_error_msg), "%s", esp_err_to_name(err));
        update_state(OTA_STATE_FAILED, g_progress);
    } else {
        update_state(OTA_STATE_SUCCESS, 1.0f);
        ESP_LOGI(TAG, "OTA update successful! Rebooting...");
        update_state(OTA_STATE_REBOOTING, 1.0f);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    free(url);
    g_ota_task = NULL;
    vTaskDelete(NULL);
}

// Public API implementation
esp_err_t ota_service_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA service");
    g_state = OTA_STATE_IDLE;
    g_progress = 0.0f;
    memset(g_error_msg, 0, sizeof(g_error_msg));
    return ESP_OK;
}

esp_err_t ota_service_check(void)
{
    char url[512];
    size_t len = sizeof(url);

    if (param_store_get("ota_server_url", url, &len) != ESP_OK || strlen(url) == 0) {
        ESP_LOGW(TAG, "No OTA server URL configured");
        return ESP_FAIL;
    }

    // In a real implementation, this would:
    // 1. Fetch manifest from OTA server
    // 2. Compare versions
    // 3. Return ESP_OK if update available

    ESP_LOGI(TAG, "OTA check would query: %s", url);
    return ESP_FAIL;  // No update available for now
}

esp_err_t ota_service_start(const char *url)
{
    if (g_state != OTA_STATE_IDLE && g_state != OTA_STATE_FAILED) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (url == NULL || strlen(url) == 0) {
        // Try to get URL from param store
        char stored_url[512];
        size_t len = sizeof(stored_url);
        if (param_store_get("ota_server_url", stored_url, &len) != ESP_OK || strlen(stored_url) == 0) {
            ESP_LOGE(TAG, "No OTA URL provided and none configured");
            return ESP_ERR_INVALID_ARG;
        }
        url = stored_url;
    }

    g_cancelled = false;
    update_state(OTA_STATE_CHECKING, 0.0f);

    // Start OTA in background task
    char *url_copy = strdup(url);
    if (url_copy == NULL) {
        ESP_LOGE(TAG, "Failed to allocate URL");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(&ota_task, "ota_task", 8192, url_copy, 5, &g_ota_task) != pdPASS) {
        free(url_copy);
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ota_service_cancel(void)
{
    if (g_ota_task != NULL) {
        ESP_LOGI(TAG, "Cancelling OTA");
        g_cancelled = true;
        vTaskDelete(g_ota_task);
        g_ota_task = NULL;
    }

    update_state(OTA_STATE_IDLE, 0.0f);
    return ESP_OK;
}

esp_err_t ota_service_get_status(ota_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->state = g_state;
    status->progress = g_progress;
    strncpy(status->version, g_version, sizeof(status->version) - 1);
    strncpy(status->error_msg, g_error_msg, sizeof(status->error_msg) - 1);

    return ESP_OK;
}

void ota_service_set_callback(ota_event_cb_t cb, void *user_data)
{
    g_callback = cb;
    g_user_data = user_data;
}
