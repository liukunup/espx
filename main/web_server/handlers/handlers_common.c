/**
 * @file handlers_common.c
 * @brief Shared HTTP response helpers (see handlers.h)
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "handlers.h"

static const char *status_text(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    default:  return "500 Internal Server Error";
    }
}

esp_err_t api_send_json(httpd_req_t *req, cJSON *json, int status)
{
    if (req == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }

    char *str = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);

    if (str == NULL) {
        httpd_resp_set_status(req, status_text(500));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"out of memory\"}", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, str, strlen(str));
    free(str);
    return err;
}

esp_err_t api_send_error(httpd_req_t *req, const char *msg, int status)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return api_send_json(req, NULL, 500);
    }
    cJSON_AddStringToObject(json, "error", msg ? msg : "error");
    return api_send_json(req, json, status);
}
