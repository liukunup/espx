/**
 * @file ota_handler.c
 * @brief OTA API handlers implementation
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <cJSON.h>

#include "web_server.h"
#include "ota_handler.h"

static const char *TAG = "ota_handler";

esp_err_t ota_status_handler(httpd_req_t *req)
{
    // TODO: Get status from ota_service
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", "IDLE");
    cJSON_AddNumberToObject(root, "progress", 0.0);
    cJSON_AddStringToObject(root, "version", CONFIG_FIRMWARE_VERSION);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return web_server_send_error(req, "Failed to generate JSON", 500);
    }

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t ota_start_handler(httpd_req_t *req)
{
    char content[512];
    size_t content_len = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        return web_server_send_error(req, "Failed to read request body", 400);
    }
    content[content_len] = '\0';

    ESP_LOGI(TAG, "OTA start request: %s", content);

    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        return web_server_send_error(req, "Invalid JSON", 400);
    }

    cJSON *url = cJSON_GetObjectItem(root, "url");

    if (!cJSON_IsString(url) || strlen(url->valuestring) == 0) {
        cJSON_Delete(root);
        return web_server_send_error(req, "Missing URL", 400);
    }

    // TODO: Call ota_service_start(url->valuestring)
    ESP_LOGI(TAG, "OTA URL: %s", url->valuestring);

    cJSON_Delete(root);

    // Return success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "started");
    cJSON_AddStringToObject(response, "message", "OTA update started");
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t ota_cancel_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA cancel requested");

    // TODO: Call ota_service_cancel()

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "cancelled");
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}
