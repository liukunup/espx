/**
 * @file ota_handler.h
 * @brief OTA API handlers
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <esp_http_server.h>

/**
 * @brief GET /api/ota/status - Get OTA status
 */
esp_err_t ota_status_handler(httpd_req_t *req);

/**
 * @brief POST /api/ota/start - Start OTA update
 */
esp_err_t ota_start_handler(httpd_req_t *req);

/**
 * @brief POST /api/ota/cancel - Cancel OTA update
 */
esp_err_t ota_cancel_handler(httpd_req_t *req);

#endif // OTA_HANDLER_H
