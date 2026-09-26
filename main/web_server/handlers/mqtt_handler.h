#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <esp_http_server.h>

esp_err_t mqtt_status_handler(httpd_req_t *req);
esp_err_t mqtt_reconnect_handler(httpd_req_t *req);

#endif
