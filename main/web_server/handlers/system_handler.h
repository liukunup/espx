/**
 * @file system_handler.h
 * @brief System API handlers
 */

#ifndef SYSTEM_HANDLER_H
#define SYSTEM_HANDLER_H

#include <esp_http_server.h>

/**
 * @brief GET /api/system/info - Get system information
 */
esp_err_t system_info_handler(httpd_req_t *req);

/**
 * @brief POST /api/system/reboot - Reboot device
 */
esp_err_t system_reboot_handler(httpd_req_t *req);

/**
 * @brief GET /api/certs/info - Get certificate info
 */
esp_err_t certs_info_handler(httpd_req_t *req);

/**
 * @brief POST /api/certs/regenerate - Regenerate certificate
 */
esp_err_t certs_regenerate_handler(httpd_req_t *req);

#endif // SYSTEM_HANDLER_H
