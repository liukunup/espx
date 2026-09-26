/**
 * @file system_handler.c
 * @brief System API handlers implementation
 */

#include <stdio.h>
#include <string.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <cJSON.h>

#include "web_server.h"
#include "system_handler.h"
#include "param_store/param_store.h"
#include "cert_manager/cert_manager.h"

static const char *TAG = "system_handler";

extern const char *APP_TAG;

/**
 * @brief Get system uptime in seconds
 */
static uint32_t get_uptime_seconds(void)
{
    return esp_timer_get_time() / 1000000;
}

esp_err_t system_info_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // Device info
    cJSON_AddStringToObject(root, "device_name", CONFIG_DEVICE_NAME);
    cJSON_AddStringToObject(root, "device_id", param_store_get_device_id());
    cJSON_AddStringToObject(root, "chip_model", "ESP32-S3");
    cJSON_AddStringToObject(root, "firmware_ver", CONFIG_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(root, "uptime", get_uptime_seconds());

    // Memory info
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_8BIT);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());

    // Wi-Fi info
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", (char *)ap_info.ssid);
        cJSON_AddNumberToObject(root, "wifi_rssi", ap_info.rssi);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return web_server_send_error(req, "Failed to generate JSON", 500);
    }

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t system_reboot_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reboot requested via API");

    // Give time for response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_restart();

    // Never reached
    return ESP_OK;
}

esp_err_t certs_info_handler(httpd_req_t *req)
{
    char info[512];
    esp_err_t err = cert_manager_get_info(info, sizeof(info));
    if (err != ESP_OK) {
        return web_server_send_error(req, "Failed to get certificate info", 500);
    }

    return web_server_send_json(req, info, 200);
}

esp_err_t certs_regenerate_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Certificate regeneration requested");

    esp_err_t err = cert_manager_regenerate();
    if (err != ESP_OK) {
        return web_server_send_error(req, "Failed to regenerate certificate", 500);
    }

    // Return success
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "Certificate regenerated. Restart required.");
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t result = web_server_send_json(req, json_str, 200);
    free(json_str);

    return result;
}
