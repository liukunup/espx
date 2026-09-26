#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <esp_http_server.h>

esp_err_t wifi_status_handler(httpd_req_t *req);
esp_err_t wifi_connect_handler(httpd_req_t *req);
esp_err_t wifi_scan_handler(httpd_req_t *req);

#endif
