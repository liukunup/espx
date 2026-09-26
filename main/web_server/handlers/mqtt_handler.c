/**
 * @file mqtt_handler.c
 * @brief MQTT API handlers implementation
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <cJSON.h>

#include "web_server.h"
#include "mqtt_handler.h"

static const char *TAG = "mqtt_handler";

esp_err_t mqtt_status_handler(httpd_req_t *req)
{
    // TODO: Get status from mqtt_client
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "connected", "false");
    cJSON_AddStringToObject(root, "broker", CONFIG_MQTT_BROKER_URL);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return web_server_send_error(req, "Failed to generate JSON", 500);
    }

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t mqtt_reconnect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "MQTT reconnect requested");

    // TODO: Call mqtt_client_reconnect()

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "reconnecting");
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}
