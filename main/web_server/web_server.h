/**
 * @file web_server.h
 * @brief ESPX HTTPS Web Server
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start web server
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop web server
 */
esp_err_t web_server_stop(void);

/**
 * @brief Check if running
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
