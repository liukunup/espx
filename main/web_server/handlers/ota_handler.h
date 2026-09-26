#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <esp_http_server.h>

esp_err_t ota_status_handler(httpd_req_t *req);
esp_err_t ota_start_handler(httpd_req_t *req);
esp_err_t ota_cancel_handler(httpd_req_t *req);

#endif
