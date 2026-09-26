/**
 * @file params_handler.h
 * @brief Parameters API handlers
 */

#ifndef PARAMS_HANDLER_H
#define PARAMS_HANDLER_H

#include <esp_http_server.h>

/**
 * @brief GET /api/params - Get all parameters
 */
esp_err_t params_get_all_handler(httpd_req_t *req);

/**
 * @brief POST /api/params/batch - Batch update parameters
 */
esp_err_t params_batch_handler(httpd_req_t *req);

#endif // PARAMS_HANDLER_H
