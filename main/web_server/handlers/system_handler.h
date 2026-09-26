#ifndef SYSTEM_HANDLER_H
#define SYSTEM_HANDLER_H

#include <esp_http_server.h>

esp_err_t system_info_handler(httpd_req_t *req);
esp_err_t system_reboot_handler(httpd_req_t *req);
esp_err_t certs_info_handler(httpd_req_t *req);
esp_err_t certs_regenerate_handler(httpd_req_t *req);

#endif
