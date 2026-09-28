/**
 * @file config_handler.c
 * @brief GET/POST /api/config — export and apply a configuration document
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "handlers.h"
#include "config_apply.h"

/**
 * @brief POST /api/config - apply a YAML or JSON configuration document
 *
 * Body: the document itself. Same semantics as MQTT <prefix>/cmd/config.
 */
static esp_err_t api_config_apply_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        return api_send_error(req, "missing or oversized configuration body", 400);
    }

    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        return api_send_error(req, "out of memory", 500);
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return api_send_error(req, "failed to read body", 400);
        }
        received += r;
    }
    buf[received] = '\0';

    config_apply_result_t res;
    char err[128] = {0};
    esp_err_t rc = config_apply_payload(buf, &res, err, sizeof(err));
    free(buf);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", rc == ESP_OK && res.devices_failed == 0);

    cJSON *counts = cJSON_AddObjectToObject(out, "applied");
    cJSON_AddNumberToObject(counts, "added", res.devices_added);
    cJSON_AddNumberToObject(counts, "updated", res.devices_updated);
    cJSON_AddNumberToObject(counts, "removed", res.devices_removed);
    cJSON_AddNumberToObject(counts, "failed", res.devices_failed);

    cJSON_AddBoolToObject(out, "reboot_required", res.reboot_recommended);
    const char *msg = res.error[0] ? res.error : err;
    if (msg[0]) cJSON_AddStringToObject(out, "error", msg);

    return api_send_json(req, out, rc == ESP_OK ? 200 : 400);
}

/**
 * @brief GET /api/config - current configuration as JSON
 */
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *cfg = config_export();
    if (cfg == NULL) {
        return api_send_error(req, "failed to export configuration", 500);
    }
    return api_send_json(req, cfg, 200);
}

esp_err_t config_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/config", .method = HTTP_GET,  .handler = api_config_get_handler },
        { .uri = "/api/config", .method = HTTP_POST, .handler = api_config_apply_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
