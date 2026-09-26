#ifndef PARAMS_HANDLER_H
#define PARAMS_HANDLER_H

#include <esp_http_server.h>

esp_err_t params_get_all_handler(httpd_req_t *req);
esp_err_t params_batch_handler(httpd_req_t *req);

#endif
