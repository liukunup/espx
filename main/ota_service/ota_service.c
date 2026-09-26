/**
 * @file ota_service.c
 * @brief OTA Service - simplified stub version
 */

#include <string.h>
#include <esp_log.h>
#include <esp_err.h>

#include "ota_service.h"

static const char *TAG = "ota_service";

static ota_state_t g_state = OTA_STATE_IDLE;
static float g_progress = 0.0f;

esp_err_t ota_service_init(void)
{
    ESP_LOGI(TAG, "OTA service init (simplified stub)");
    return ESP_OK;
}

esp_err_t ota_service_check(void)
{
    return ESP_FAIL;
}

esp_err_t ota_service_start(const char *url)
{
    ESP_LOGW(TAG, "OTA start not implemented yet: %s", url ? url : "NULL");
    g_state = OTA_STATE_FAILED;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_service_cancel(void)
{
    g_state = OTA_STATE_IDLE;
    return ESP_OK;
}

esp_err_t ota_service_get_status(ota_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status->state = g_state;
    status->progress = g_progress;
    status->version[0] = '\0';
    status->error_msg[0] = '\0';
    return ESP_OK;
}

void ota_service_set_callback(ota_event_cb_t cb, void *user_data)
{
    // Not implemented
}
