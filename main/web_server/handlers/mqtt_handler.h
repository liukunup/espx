/**
 * @file mqtt_handler.h
 * @brief MQTT API handlers
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <esp_http_server.h>

/**
 * @brief GET /api/mqtt/status - Get MQTT status
 */
esp_err_t mqtt_status_handler(httpd_req_t *req);

/**
 * @brief POST /api/mqtt/reconnect - Reconnect MQTT
 */
esp_err_t mqtt_reconnect_handler(httpd_req_t *req);

#endif // MQTT_HANDLER_H
