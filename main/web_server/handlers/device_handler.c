/**
 * @file device_handler.c
 * @brief /api/peripherals and /api/peripheral/options handlers
 *
 * Owns the wildcard routes under /api/peripherals/, so device_handler_register()
 * MUST be the last register call in web_server_start(): with
 * httpd_uri_match_wildcard the first matching handler wins, and a wildcard
 * registered before the exact paths would swallow /api/peripherals and
 * /api/peripherals/reload.
 */

#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"
#include "device_manager.h"
#include "device_type.h"
#include "str_utils.h"

static const char *TAG = "device_handler";

/**
 * @brief GET /api/peripherals - list devices with values
 */
static esp_err_t api_devices_list_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    for (size_t i = 0; i < device_get_count(); i++) {
        const device_t *dev = device_get_by_index(i);
        if (dev == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", dev->id);
        cJSON_AddStringToObject(item, "type", dev->type->name);
        cJSON_AddStringToObject(item, "description", dev->type->description);
        cJSON_AddBoolToObject(item, "enabled", dev->enabled);
        cJSON_AddBoolToObject(item, "initialized", dev->initialized);

        if (dev->config != NULL) {
            cJSON_AddItemToObject(item, "config", cJSON_Duplicate((cJSON *)dev->config, true));
        }

        /* Current value (only for live, readable devices) */
        if (dev->enabled && dev->initialized && dev->type->read) {
            cJSON *value = cJSON_CreateObject();
            if (dev->type->read((device_t *)dev, value) == ESP_OK) {
                cJSON_AddItemToObject(item, "value", value);
            } else {
                cJSON_Delete(value);
            }
        }

        cJSON_AddItemToArray(json, item);
    }

    return api_send_json(req, json, 200);
}

/**
 * @brief POST /api/peripherals - add device
 */
static esp_err_t api_device_add_handler(httpd_req_t *req)
{
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return api_send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return api_send_error(req, "Invalid JSON", 400);

    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *config = cJSON_GetObjectItem(root, "config");

    if (!cJSON_IsString(id) || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return api_send_error(req, "Missing id or type", 400);
    }

    esp_err_t err = device_add(id->valuestring, type->valuestring, config);
    if (err == ESP_OK) {
        device_manager_save();
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    cJSON_AddStringToObject(resp, "error", err != ESP_OK ? esp_err_to_name(err) : "");
    int status = (err == ESP_OK) ? 200 : 400;
    cJSON_Delete(root);
    return api_send_json(req, resp, status);
}

/**
 * @brief Helper: parse device id and action from /api/peripherals/{id}[/{action}]
 *
 * @param uri      Request URI
 * @param id_out   Output buffer for device id
 * @param id_size  Size of id_out
 * @param action_out Output buffer for action (may be empty string)
 * @param action_size Size of action_out
 * @return true if URI matched
 */
static bool parse_device_uri(const char *uri, char *id_out, size_t id_size,
                             char *action_out, size_t action_size)
{
    const char *prefix = "/api/peripherals/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *rest = uri + prefix_len;
    const char *slash = strchr(rest, '/');

    if (slash == NULL) {
        str_copy(id_out, id_size, rest);
        if (action_out && action_size > 0) action_out[0] = '\0';
    } else {
        size_t id_len = slash - rest;
        if (id_len >= id_size) id_len = id_size - 1;
        memcpy(id_out, rest, id_len);
        id_out[id_len] = '\0';

        if (action_out && action_size > 0) {
            str_copy(action_out, action_size, slash + 1);
        }
    }

    return true;
}

/**
 * @brief Dispatcher for /api/peripherals/{id}[/action]
 *
 * GET    /api/peripherals/{id}          - get device info
 * DELETE /api/peripherals/{id}          - remove device
 * POST   /api/peripherals/{id}/read     - read value
 * POST   /api/peripherals/{id}/write    - write value
 * POST   /api/peripherals/{id}/enable   - enable/disable
 */
static esp_err_t api_device_dispatch_handler(httpd_req_t *req)
{
    char id[64];
    char action[32];

    if (!parse_device_uri(req->uri, id, sizeof(id), action, sizeof(action))) {
        return api_send_error(req, "Invalid URI", 400);
    }

    // ---- POST actions ----
    if (req->method == HTTP_POST) {
        if (strcmp(action, "read") == 0) {
            cJSON *json = cJSON_CreateObject();
            esp_err_t err = device_read(id, json);
            if (err != ESP_OK) {
                cJSON_Delete(json);
                return api_send_error(req, esp_err_to_name(err), 400);
            }
            return api_send_json(req, json, 200);
        }

        if (strcmp(action, "write") == 0) {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return api_send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return api_send_error(req, "Invalid JSON", 400);

            esp_err_t err = device_write(id, root);
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            if (err != ESP_OK) {
                cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
            }
            return api_send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        if (strcmp(action, "enable") == 0) {
            char buf[128];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return api_send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return api_send_error(req, "Invalid JSON", 400);

            /* Require an explicit boolean. Without this test a malformed or
             * empty body made cJSON_IsTrue(NULL-ish) evaluate to false and the
             * device was silently DISABLED -- i.e. a garbled request switched an
             * actuator off. Every other handler in this file rejects bad input
             * with 400; this one now does too. */
            cJSON *jenabled = cJSON_GetObjectItem(root, "enabled");
            if (!cJSON_IsBool(jenabled)) {
                cJSON_Delete(root);
                return api_send_error(req, "Missing 'enabled' boolean", 400);
            }
            bool enabled = cJSON_IsTrue(jenabled);
            cJSON_Delete(root);

            esp_err_t err = device_set_enabled(id, enabled);
            if (err == ESP_OK) {
                device_manager_save();
            }

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return api_send_json(req, resp, err == ESP_OK ? 200 : 404);
        }

        // POST /api/peripherals/{id} with new config -> update config
        if (action[0] == '\0') {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return api_send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return api_send_error(req, "Invalid JSON", 400);

            cJSON *config = cJSON_GetObjectItem(root, "config");
            esp_err_t err = device_update_config(id, config);
            if (err == ESP_OK) {
                device_manager_save();
            }
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return api_send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        return api_send_error(req, "Unknown action", 400);
    }

    // ---- GET /api/peripherals/{id} ----
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        esp_err_t err = device_get_json(id, json);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            return api_send_error(req, "Device not found", 404);
        }
        return api_send_json(req, json, 200);
    }

    // ---- DELETE /api/peripherals/{id} ----
    if (req->method == HTTP_DELETE) {
        esp_err_t err = device_remove(id);
        if (err == ESP_OK) {
            device_manager_save();
        }
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 404);
    }

    return api_send_error(req, "Method not allowed", 400);
}

/**
 * @brief POST /api/peripherals/reload - reload all devices
 */
static esp_err_t api_devices_reload_handler(httpd_req_t *req)
{
    esp_err_t err = device_manager_reload();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
}

/**
 * @brief GET /api/peripheral/options - list device types
 */
static esp_err_t api_device_types_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    for (size_t i = 0; i < device_type_count(); i++) {
        const device_type_t *t = device_type_get_by_index(i);
        if (t == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", t->name);
        cJSON_AddStringToObject(item, "description", t->description);
        cJSON_AddStringToObject(item, "description_zh", t->description_zh);
        cJSON_AddNumberToObject(item, "capabilities", t->capabilities);

        cJSON *cfg = cJSON_CreateObject();
        if (t->get_default_config) {
            t->get_default_config(cfg);
        }
        cJSON_AddItemToObject(item, "default_config", cfg);

        cJSON_AddItemToArray(json, item);
    }

    return api_send_json(req, json, 200);
}

esp_err_t device_handler_register(httpd_handle_t server)
{
    /* ORDER: exact paths must be registered before the wildcard patterns.
     * With httpd_uri_match_wildcard the first match wins, so a wildcard
     * registered first would swallow the /api/peripherals/reload route. */
    static const httpd_uri_t uris[] = {
        { .uri = "/api/peripherals",          .method = HTTP_GET,    .handler = api_devices_list_handler },
        { .uri = "/api/peripherals",          .method = HTTP_POST,   .handler = api_device_add_handler },
        { .uri = "/api/peripherals/reload",   .method = HTTP_POST,   .handler = api_devices_reload_handler },
        { .uri = "/api/peripheral/options",   .method = HTTP_GET,    .handler = api_device_types_handler },
        /* wildcards last */
        { .uri = "/api/peripherals/*",        .method = HTTP_GET,    .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",        .method = HTTP_POST,   .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",        .method = HTTP_DELETE, .handler = api_device_dispatch_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
            return err;
        }
    }
    return ESP_OK;
}
