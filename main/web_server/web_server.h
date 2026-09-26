/**
 * @file web_server.h
 * @brief HTTPS Web Server for ESPX device management
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTPS web server
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTPS web server
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t web_server_stop(void);

/**
 * @brief Check if web server is running
 *
 * @return true if running, false otherwise
 */
bool web_server_is_running(void);

/**
 * @brief Send JSON response
 *
 * @param httpd_req HTTP request handle
 * @param json JSON string to send
 * @param status HTTP status code
 * @return ESP_OK on success, error code on failure
 */
esp_err_t web_server_send_json(void *httpd_req, const char *json, int status);

/**
 * @brief Send error response
 *
 * @param httpd_req HTTP request handle
 * @param message Error message
 * @param status HTTP status code
 * @return ESP_OK on success, error code on failure
 */
esp_err_t web_server_send_error(void *httpd_req, const char *message, int status);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
