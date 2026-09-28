/**
 * @file web_server.c
 * @brief ESPX HTTPS Web Server implementation
 *
 * Uses mbedtls self-signed certificate.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_http_server.h>

#include "web_server.h"
#include "cert_manager/cert_manager.h"
#include "ws_server.h"
#include "handlers/handlers.h"

static const char *TAG = "web_server";

static httpd_handle_t g_server = NULL;

// Embedded web UI (see main/web_server/web_files/index.html)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/**
 * @brief Root handler - serve embedded UI
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

esp_err_t web_server_start(void)
{
    if (g_server != NULL) {
        return ESP_OK;
    }

    // Get embedded certificate from cert_manager
    server_cert_t server_cert;
    esp_err_t err = cert_manager_get_server_cert(&server_cert);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate: %s", esp_err_to_name(err));
        return err;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = (uint8_t *)server_cert.cert_pem;
    config.servercert_len = server_cert.cert_len;
    config.prvtkey_pem = (uint8_t *)server_cert.key_pem;
    config.prvtkey_len = server_cert.key_len;
    config.httpd.max_uri_handlers = 32;

    /* Wildcards are NOT matched by default: with uri_match_fn == NULL the
     * server does a plain string compare, so a pattern ending in a star never
     * matches a real path such as "/api/peripherals/relay_a/read". Every such
     * request would 404 with the server's own "Nothing matches the given URI".
     * Selecting the wildcard matcher is what makes the dispatcher reachable. */
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    /* 4 comfortably covers one browser (1 WebSocket + a couple of in-flight requests). */
    config.httpd.max_open_sockets = 4;
    /* Increase stack size for HTTPD task to prevent stack overflow */
    config.httpd.stack_size = 8192;

    ESP_LOGI(TAG, "Starting HTTPS server...");
    err = httpd_ssl_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS: %s", esp_err_to_name(err));
        cert_manager_free_cert(&server_cert);
        return err;
    }

    /* / is the only route owned by this file; everything else registers itself. */
    static const httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_handler,
    };
    if (httpd_register_uri_handler(g_server, &root_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /");
    }

    /* Exact paths first. */
    node_handler_register(g_server);
    network_handler_register(g_server);
    config_handler_register(g_server);
    system_handler_register(g_server);
    ota_handler_register(g_server);
    cert_handler_register(g_server);
    wifi_handler_register(g_server);

    /* LAST: owns the /api/peripherals wildcard routes */
    device_handler_register(g_server);

#if CONFIG_ESPX_WS_ENABLE
    if (ws_server_start(g_server) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the WebSocket endpoint");
    }
#endif

    free(server_cert.cert_pem);
    free(server_cert.key_pem);

    ESP_LOGI(TAG, "HTTPS server started on port 443");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (g_server == NULL) {
        return ESP_OK;
    }
    esp_err_t err = httpd_ssl_stop(g_server);
    g_server = NULL;
    return err;
}

bool web_server_is_running(void)
{
    return g_server != NULL;
}
