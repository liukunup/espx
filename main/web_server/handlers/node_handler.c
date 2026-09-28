/**
 * @file node_handler.c
 * @brief GET/PUT /api/node — node identity
 */

#include <string.h>

#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "node_config.h"
#include "app_info.h"
#include "device_manager.h"

/* api_node_handler() moves here verbatim from web_server.c, with three changes:
 *   - send_json(...)      -> api_send_json(...)
 *   - send_error(...)     -> api_send_error(...)
 *   - json_set_string(..) -> json_set_string(..) from json_utils.h
 */
static esp_err_t api_node_handler(httpd_req_t *req)
{
    if (req->method == HTTP_PUT) {
        char buf[256];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *node = cJSON_GetObjectItem(cfg, "node");
        if (!cJSON_IsObject(node)) {
            node = cJSON_AddObjectToObject(cfg, "node");
        }

        const char *name = json_get_string(incoming, "name", NULL);
        if (name != NULL && name[0] != '\0') {
            json_set_string(node, "name", name);
        }
        /* device_id is set at the factory and cannot change at runtime. */

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    cJSON *json = cJSON_CreateObject();
    {
        const char *id = node_config_get_device_id();
        if (strncmp(id, "espx-", 5) == 0) {
            id += 5;
        }
        cJSON_AddStringToObject(json, "device_id", id);
    }
    cJSON_AddStringToObject(json, "name", node_config_get_name());
    cJSON_AddStringToObject(json, "version", app_version());
    cJSON_AddNumberToObject(json, "peripheral_count", device_get_count());

    return api_send_json(req, json, 200);
}

esp_err_t node_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/node", .method = HTTP_GET, .handler = api_node_handler },
        { .uri = "/api/node", .method = HTTP_PUT, .handler = api_node_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
