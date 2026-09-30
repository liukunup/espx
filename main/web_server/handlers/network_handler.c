/**
 * @file network_handler.c
 * @brief GET/PUT /api/network — MQTT / network settings
 */

#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "str_utils.h"
#include "node_config.h"
#include "esp_now_service.h"

static const char *TAG = "http_network";

static esp_err_t api_network_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        cJSON *cfg = node_config_get();
        cJSON *net = cfg ? cJSON_GetObjectItem(cfg, "network") : NULL;
        cJSON *out = net ? cJSON_Duplicate(net, true) : cJSON_CreateObject();
        if (cfg) cJSON_Delete(cfg);
        return api_send_json(req, out, 200);
    }

    if (req->method == HTTP_PUT) {
        char buf[512];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *net = cJSON_GetObjectItem(cfg, "network");
        if (!cJSON_IsObject(net)) {
            net = cJSON_AddObjectToObject(cfg, "network");
        }

        /* Walk the incoming members: strings, numbers and bools are all
         * accepted (the network section holds ports and flags too). */
        cJSON *item = incoming->child;
        while (item != NULL) {
            if (cJSON_IsString(item)) {
                json_set_string(net, item->string, item->valuestring);
            } else if (cJSON_IsNumber(item) || cJSON_IsBool(item)) {
                cJSON *dup = cJSON_Duplicate(item, true);
                cJSON_DeleteItemFromObject(net, item->string);
                cJSON_AddItemToObject(net, item->string, dup);
            }
            /* Never log secrets */
            if (str_contains(item->string, "password")) {
                ESP_LOGI(TAG, "Network config: %s = <set>", item->string);
            } else {
                ESP_LOGI(TAG, "Network config: %s = %s", item->string,
                         cJSON_IsString(item) ? item->valuestring : "<non-string>");
            }
            item = item->next;
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return api_send_error(req, "Method not allowed", 400);
}

static esp_err_t api_esp_now_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Export ESP-NOW status and peers */
        cJSON *status = espx_espnow_config_export();
        return api_send_json(req, status, 200);
    }

    if (req->method == HTTP_POST) {
        /* Configure ESP-NOW peers */
        char buf[2048];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *config = cJSON_Parse(buf);
        if (config == NULL) return api_send_error(req, "Invalid JSON", 400);

        esp_err_t err = espx_espnow_configure(config);
        cJSON_Delete(config);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        if (err != ESP_OK) {
            cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
        }
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return api_send_error(req, "Method not allowed", 400);
}

esp_err_t network_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/network", .method = HTTP_GET, .handler = api_network_handler },
        { .uri = "/api/network", .method = HTTP_PUT, .handler = api_network_handler },
        { .uri = "/api/esp_now", .method = HTTP_GET, .handler = api_esp_now_handler },
        { .uri = "/api/esp_now", .method = HTTP_POST, .handler = api_esp_now_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
