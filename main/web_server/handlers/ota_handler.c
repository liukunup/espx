/**
 * @file ota_handler.c
 * @brief /api/ota endpoints — delta OTA status, start and cancel
 */

#include <cJSON.h>

#include "handlers.h"
#include "ota_service/ota_service.h"

static const char *OTA_STATE_NAMES[] = {
    "IDLE", "CONNECTING", "DOWNLOADING", "VERIFYING",
    "APPLYING", "REBOOTING", "SUCCESS", "FAILED"
};

/**
 * @brief GET /api/ota/status - OTA status
 */
static esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    ota_status_t st;
    ota_service_get_status(&st);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state",
                            OTA_STATE_NAMES[st.state <= OTA_STATE_FAILED ? st.state : OTA_STATE_FAILED]);
    cJSON_AddNumberToObject(json, "progress", st.progress);
    cJSON_AddNumberToObject(json, "bytes_read", st.bytes_read);
    cJSON_AddNumberToObject(json, "total_size", st.total_size);
    cJSON_AddStringToObject(json, "url", st.url);
    cJSON_AddStringToObject(json, "error", st.error);
    cJSON_AddStringToObject(json, "running_version", st.running_version);
    cJSON_AddBoolToObject(json, "running", ota_service_is_running());

    return api_send_json(req, json, 200);
}

/**
 * @brief POST /api/ota/start - start delta OTA
 * Body: {"url": "http://server/espx.patch"}  (url optional if stored)
 */
static esp_err_t api_ota_start_handler(httpd_req_t *req)
{
    char buf[384];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return api_send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) return api_send_error(req, "Invalid JSON", 400);

    cJSON *jurl = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(jurl) || jurl->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return api_send_error(req, "Missing 'url'", 400);
    }

    esp_err_t err = ota_service_start(jurl->valuestring);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "started", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
    }
    return api_send_json(req, resp, err == ESP_OK ? 200 : 400);
}

/**
 * @brief POST /api/ota/cancel - cancel OTA
 */
static esp_err_t api_ota_cancel_handler(httpd_req_t *req)
{
    esp_err_t err = ota_service_cancel();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "cancelled", err == ESP_OK);
    return api_send_json(req, resp, 200);
}

esp_err_t ota_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/ota/status", .method = HTTP_GET,  .handler = api_ota_status_handler },
        { .uri = "/api/ota/start",  .method = HTTP_POST, .handler = api_ota_start_handler },
        { .uri = "/api/ota/cancel", .method = HTTP_POST, .handler = api_ota_cancel_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
