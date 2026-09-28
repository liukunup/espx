/**
 * @file cert_handler.c
 * @brief GET /api/certs/info — certificate metadata
 */

#include <cJSON.h>

#include "handlers.h"
#include "cert_manager/cert_manager.h"

/**
 * @brief GET /api/certs/info - certificate info
 */
static esp_err_t api_certs_info_handler(httpd_req_t *req)
{
    char info[256];
    cert_manager_get_info(info, sizeof(info));
    cJSON *json = cJSON_Parse(info);
    if (json == NULL) {
        return api_send_error(req, "Failed to build cert info", 500);
    }
    return api_send_json(req, json, 200);
}

esp_err_t cert_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/certs/info", .method = HTTP_GET, .handler = api_certs_info_handler },
    };

    esp_err_t err = httpd_register_uri_handler(server, &uris[0]);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}
