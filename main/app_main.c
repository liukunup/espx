/* Application Main Entry

   This example demonstrates how to use the wifi_prov component
   for Wi-Fi provisioning with configurable parameters.

   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

#include "wifi_prov/wifi_prov.h"

static const char *TAG = "app";

/* ============================================
 * Application Task
 * ============================================ */

#if WIFI_PROV_REPROVISIONING
static void app_task(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "Hello World!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

/* ============================================
 * Optional: Custom Application Callback
 * ============================================ */

#if WIFI_PROV_ENABLE_APP_CALLBACK

static void app_wifi_prov_callback(void *user_data, wifi_prov_event_t event, void *event_data)
{
    switch (event) {
    case WIFI_PROV_EVENT_WIFI_CRED_RECV:
        ESP_LOGI(TAG, "App callback: Wi-Fi credentials received");
        break;

    case WIFI_PROV_EVENT_WIFI_CRED_SUCCESS:
        ESP_LOGI(TAG, "App callback: Wi-Fi credentials accepted");
        break;

    case WIFI_PROV_EVENT_WIFI_CRED_FAIL:
        ESP_LOGI(TAG, "App callback: Wi-Fi credentials failed");
        break;

    case WIFI_PROV_EVENT_CONNECTED:
        ESP_LOGI(TAG, "App callback: Wi-Fi connected");
        break;

    case WIFI_PROV_EVENT_PROVISIONING_END:
        ESP_LOGI(TAG, "App callback: Provisioning ended");
        break;

    default:
        break;
    }
}

static wifi_prov_event_handler_t app_handler = {
    .event_cb = app_wifi_prov_callback,
    .user_data = NULL,
};

#endif /* WIFI_PROV_ENABLE_APP_CALLBACK */

/* ============================================
 * Main Entry Point
 * ============================================ */

void app_main(void)
{
    /* Initialize Wi-Fi provisioning */
    ESP_ERROR_CHECK(wifi_prov_init());

    /* Start provisioning (checks if already provisioned) */
#if WIFI_PROV_ENABLE_APP_CALLBACK
    ESP_ERROR_CHECK(wifi_prov_start(&app_handler));
#else
    ESP_ERROR_CHECK(wifi_prov_start(NULL));
#endif

    /* Wait for Wi-Fi connection */
    wifi_prov_wait_for_connection();

    ESP_LOGI(TAG, "Wi-Fi connected! Starting application...");

#if WIFI_PROV_REPROVISIONING
    /* Create application task with reprovisioning support */
    xTaskCreate(app_task, "app_task", 4096, NULL, 2, NULL);

    /* Periodically reset provisioning state for re-provisioning test */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Checking if re-provisioning is needed...");
        /* In production, implement your own logic to trigger reprovisioning */
    }
#else
    /* Main application loop */
    while (1) {
        ESP_LOGI(TAG, "Hello World!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
