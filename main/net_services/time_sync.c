/**
 * @file time_sync.c
 * @brief NTP time synchronisation implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_sntp.h>

#include "time_sync.h"

static const char *TAG = "time_sync";

#define SYNCED_BIT BIT0

static EventGroupHandle_t s_events = NULL;
static bool s_started = false;

static void on_time_synced(struct timeval *tv)
{
    time_t now = tv ? tv->tv_sec : time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tm);
    ESP_LOGI(TAG, "Time synchronised: %s", buf);

    if (s_events) {
        xEventGroupSetBits(s_events, SYNCED_BIT);
    }
}

esp_err_t time_sync_init(void)
{
    if (s_events == NULL) {
        s_events = xEventGroupCreate();
        if (s_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* Apply the configured timezone before anything formats a timestamp. */
    setenv("TZ", CONFIG_ESPX_TZ, 1);
    tzset();

    if (!s_started) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, CONFIG_ESPX_NTP_SERVER);
        esp_sntp_set_time_sync_notification_cb(on_time_synced);
        ESP_LOGI(TAG, "NTP configured: server=%s tz=%s",
                 CONFIG_ESPX_NTP_SERVER, CONFIG_ESPX_TZ);
    }

    return ESP_OK;
}

esp_err_t time_sync_start(void)
{
    if (s_events == NULL) {
        esp_err_t err = time_sync_init();
        if (err != ESP_OK) return err;
    }

    if (s_started) {
        return ESP_OK;
    }

    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "NTP started, syncing with %s", CONFIG_ESPX_NTP_SERVER);
    return ESP_OK;
}

esp_err_t time_sync_wait(uint32_t timeout_ms)
{
    if (s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* ESP_SNTP already reports "synced" through esp_sntp_get_sync_status(); the
     * event bit is set by the notification callback. */
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_events, SYNCED_BIT, pdFALSE, pdTRUE, ticks);

    if (bits & SYNCED_BIT) {
        return ESP_OK;
    }

    /* A late callback may have been missed; fall back to the SNTP state. */
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        xEventGroupSetBits(s_events, SYNCED_BIT);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "NTP sync not confirmed within %u ms", (unsigned)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

bool time_sync_is_synced(void)
{
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        return true;
    }
    if (s_events && (xEventGroupGetBits(s_events) & SYNCED_BIT)) {
        return true;
    }
    return false;
}

int64_t time_sync_epoch(void)
{
    time_t now = time(NULL);
    /* A clock still at the 1970 epoch means SNTP has not set it. */
    return (now > 1600000000) ? (int64_t)now : 0;
}

esp_err_t time_sync_iso8601(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);
    if (now <= 1600000000) {
        buf[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", &tm);
    return ESP_OK;
}

const char* time_sync_timezone(void)
{
    return CONFIG_ESPX_TZ;
}

const char* time_sync_server(void)
{
    return CONFIG_ESPX_NTP_SERVER;
}
