/**
 * @file ws_server.h
 * @brief WebSocket endpoint for live state and commands
 *
 * Endpoint: /ws  (wss:// under the HTTPS server)
 *
 * Server -> client
 *   {"type":"hello","id":"espx-…","version":"…","time":"…","mdns":"….local"}
 *   {"type":"state","devices":[{id,type,enabled,initialized,value,config}],
 *                   "time":"…","uptime":123}
 *   {"type":"event","event":"device_value_changed","id":"relay_a"}
 *   {"type":"result","ok":true,"id":"relay_a","value":true}
 *   {"type":"error","error":"…"}
 *
 * Client -> server
 *   {"type":"ping"}
 *   {"type":"refresh"}                                  -> immediate state push
 *   {"type":"read","id":"relay_a"}
 *   {"type":"write","id":"relay_a","value":true}
 *   {"type":"config","yaml":"…"}                        -> apply a config document
 *
 * Rationale: the REST API remains the source of truth for mutations; the
 * WebSocket exists so the UI does not have to poll, and so a device value
 * change reaches the browser immediately.
 */

#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the /ws endpoint and start the broadcast task
 *
 * @param server Running HTTP(S) server handle
 */
esp_err_t ws_server_start(httpd_handle_t server);

/**
 * @brief Stop the broadcast task (the endpoint stays registered)
 */
esp_err_t ws_server_stop(void);

/**
 * @brief Number of currently connected WebSocket clients
 */
int ws_server_client_count(void);

/**
 * @brief Request an immediate state broadcast
 *
 * Safe to call from an event handler: it only sets a flag for the
 * broadcast task.
 */
void ws_server_request_broadcast(void);

#ifdef __cplusplus
}
#endif

#endif /* WS_SERVER_H */
