/* Application Module - Simplified version for build testing */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "app.h"

static const char *TAG = "app";

/* ============================================
 * Public API Implementation
 * ============================================ */

esp_err_t app_init(void)
{
    return ESP_OK;
}

void app_loop(void)
{
    // Main loop - just idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
