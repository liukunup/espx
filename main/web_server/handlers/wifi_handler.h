/**
 * @file wifi_handler.h
 * @brief Wi-Fi API handlers
 */

#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <esp_http_server.h>

/**
 * @brief GET /api/wifi/status - Get Wi-Fi status
 */
esp_err_t wifi_status_handler(httpd_req_t *req);

/**
 * @brief POST /api/wifi/connect - Connect to Wi-Fi
 */
esp_err_t wifi_connect_handler(httpd_req_t *req);

/**
 * @brief POST /api/wifi/scan - Scan Wi-Fi networks
 */
esp_err_t wifi_scan_handler(httpd_req_t *req);

#endif // WIFI_HANDLER_H
