/**
 * @file handlers.h
 * @brief Shared helpers and per-module registration for the HTTPS API
 *
 * web_server.c owns the server lifecycle; each handler module owns its own
 * URI table and registers itself. Registration ORDER matters: the device
 * module registers the wildcard /api/peripherals routes and must therefore
 * be registered last, or it swallows the exact paths.
 */

#ifndef HANDLERS_H
#define HANDLERS_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send a JSON document as the response body
 *
 * Takes ownership of @p json and deletes it, on success and on failure.
 *
 * @param status HTTP status: 200, 400, 404 or 500 (anything else becomes 500)
 */
esp_err_t api_send_json(httpd_req_t *req, cJSON *json, int status);

/**
 * @brief Send {"error": msg} with the given status
 */
esp_err_t api_send_error(httpd_req_t *req, const char *msg, int status);

/* Registration order in web_server_start():
 *   root → node → network → config → system → ota → cert → wifi → device(last)
 */
esp_err_t node_handler_register(httpd_handle_t server);
esp_err_t network_handler_register(httpd_handle_t server);
esp_err_t config_handler_register(httpd_handle_t server);
esp_err_t device_handler_register(httpd_handle_t server);
esp_err_t system_handler_register(httpd_handle_t server);
esp_err_t ota_handler_register(httpd_handle_t server);
esp_err_t cert_handler_register(httpd_handle_t server);
esp_err_t wifi_handler_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* HANDLERS_H */
