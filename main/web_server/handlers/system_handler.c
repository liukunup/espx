/**
 * @file system_handler.c
 * @brief /api/system endpoints — runtime info, reboot and test-mode entry
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <cJSON.h>

#include "handlers.h"
#include "sys_info.h"
#include "test_mode/test_mode.h"

static const char *TAG = "http_system";

/**
 * @brief GET /api/system/info - runtime info
 */
static esp_err_t api_system_info_handler(httpd_req_t *req)
{
    cJSON *json = sys_info_build();
    if (json == NULL) {
        return api_send_error(req, "failed to build system info", 500);
    }
    return api_send_json(req, json, 200);
}

/**
 * @brief POST /api/system/reboot
 */
static esp_err_t api_system_reboot_handler(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    api_send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

/**
 * @brief POST /api/system/testmode - reboot into manufacturing test mode
 */
static esp_err_t api_system_testmode_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Test mode requested via web");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "rebooting_into_test_mode", true);
    api_send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(200));
    test_mode_request();
    esp_restart();
    return ESP_OK;
}

esp_err_t system_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/system/info",     .method = HTTP_GET,  .handler = api_system_info_handler },
        { .uri = "/api/system/reboot",   .method = HTTP_POST, .handler = api_system_reboot_handler },
        { .uri = "/api/system/testmode", .method = HTTP_POST, .handler = api_system_testmode_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
