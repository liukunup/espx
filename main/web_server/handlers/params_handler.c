/**
 * @file params_handler.c
 * @brief Parameters API handlers implementation
 */

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <cJSON.h>

#include "web_server.h"
#include "params_handler.h"
#include "param_store/param_store.h"

static const char *TAG = "params_handler";

esp_err_t params_get_all_handler(httpd_req_t *req)
{
    char json[4096];
    esp_err_t err = param_store_get_all(json, sizeof(json));
    if (err != ESP_OK) {
        return web_server_send_error(req, "Failed to get parameters", 500);
    }

    return web_server_send_json(req, json, 200);
}

esp_err_t params_batch_handler(httpd_req_t *req)
{
    char content[2048];
    size_t content_len = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        return web_server_send_error(req, "Failed to read request body", 400);
    }
    content[content_len] = '\0';

    ESP_LOGI(TAG, "Batch update: %s", content);

    esp_err_t err = param_store_batch_set(content);
    if (err != ESP_OK) {
        return web_server_send_error(req, "Failed to update parameters", 400);
    }

    // Return updated parameters
    char json[4096];
    err = param_store_get_all(json, sizeof(json));
    if (err != ESP_OK) {
        return web_server_send_error(req, "Update succeeded but failed to read back", 200);
    }

    return web_server_send_json(req, json, 200);
}
