/**
 * @file web_server.c
 * @brief HTTPS Web Server implementation
 */

#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_http_server.h>

#include "web_server.h"
#include "cert_manager/cert_manager.h"

// Include embedded HTML from SPIFFS-like partition
// For simplicity, we embed the HTML directly as a string
// In production, this could be served from a partition or SPIFFS

static const char *TAG = "web_server";

static httpd_handle_t g_server = NULL;
static bool g_running = false;

/**
 * @brief Get content type from file extension
 */
static const char* get_content_type(const char *filename)
{
    if (strstr(filename, ".html")) return "text/html";
    if (strstr(filename, ".css")) return "text/css";
    if (strstr(filename, ".js")) return "application/javascript";
    if (strstr(filename, ".png")) return "image/png";
    if (strstr(filename, ".jpg") || strstr(filename, ".jpeg")) return "image/jpeg";
    if (strstr(filename, ".gif")) return "image/gif";
    if (strstr(filename, ".ico")) return "image/x-icon";
    if (strstr(filename, ".svg")) return "image/svg+xml";
    if (strstr(filename, ".json")) return "application/json";
    if (strstr(filename, ".xml")) return "application/xml";
    return "application/octet-stream";
}

// Embedded HTML page
static const char index_html[] = 
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>ESPX Device Manager</title>"
    "<style>"
        ":root { --primary: #2563eb; --primary-hover: #1d4ed8; --bg: #f8fafc; --card-bg: #ffffff; --text: #1e293b; --text-secondary: #64748b; --border: #e2e8f0; --success: #22c55e; --warning: #f59e0b; --error: #ef4444; }"
        "* { margin: 0; padding: 0; box-sizing: border-box; }"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg); color: var(--text); line-height: 1.6; }"
        "header { background: var(--primary); color: white; padding: 1rem 2rem; display: flex; justify-content: space-between; align-items: center; }"
        "header h1 { font-size: 1.25rem; }"
        ".status { font-size: 0.875rem; display: flex; gap: 1rem; align-items: center; }"
        ".status-dot { width: 10px; height: 10px; border-radius: 50%; background: var(--error); }"
        ".status-dot.connected { background: var(--success); }"
        "main { max-width: 1200px; margin: 2rem auto; padding: 0 1rem; }"
        "nav { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; flex-wrap: wrap; }"
        ".tab-btn { padding: 0.75rem 1.5rem; border: none; background: var(--card-bg); color: var(--text-secondary); border-radius: 0.5rem; cursor: pointer; transition: all 0.2s; border: 1px solid var(--border); }"
        ".tab-btn:hover { border-color: var(--primary); }"
        ".tab-btn.active { background: var(--primary); color: white; border-color: var(--primary); }"
        ".card { background: var(--card-bg); border-radius: 0.75rem; padding: 1.5rem; box-shadow: 0 1px 3px rgba(0,0,0,0.1); margin-bottom: 1rem; }"
        ".card h2 { font-size: 1.125rem; margin-bottom: 1rem; color: var(--text); }"
        ".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 1rem; }"
        ".info-item { padding: 0.75rem; background: var(--bg); border-radius: 0.5rem; }"
        ".info-label { font-size: 0.75rem; color: var(--text-secondary); text-transform: uppercase; }"
        ".info-value { font-size: 1rem; font-weight: 500; word-break: break-all; }"
        ".form-group { margin-bottom: 1rem; }"
        ".form-group label { display: block; margin-bottom: 0.25rem; font-size: 0.875rem; font-weight: 500; }"
        ".form-group input { width: 100%; padding: 0.625rem; border: 1px solid var(--border); border-radius: 0.375rem; font-size: 0.875rem; }"
        ".form-group input:focus { outline: none; border-color: var(--primary); }"
        ".btn { padding: 0.625rem 1.25rem; border: none; border-radius: 0.375rem; cursor: pointer; font-size: 0.875rem; font-weight: 500; transition: all 0.2s; }"
        ".btn-primary { background: var(--primary); color: white; }"
        ".btn-primary:hover { background: var(--primary-hover); }"
        ".btn-secondary { background: var(--bg); color: var(--text); border: 1px solid var(--border); }"
        ".btn-secondary:hover { border-color: var(--primary); }"
        ".btn-danger { background: var(--error); color: white; }"
        ".btn-danger:hover { opacity: 0.9; }"
        ".btn:disabled { opacity: 0.5; cursor: not-allowed; }"
        ".btn-group { display: flex; gap: 0.5rem; }"
        ".tab-content { display: none; }"
        ".tab-content.active { display: block; }"
        ".progress-bar { width: 100%; height: 8px; background: var(--bg); border-radius: 4px; overflow: hidden; margin-top: 0.5rem; }"
        ".progress-fill { height: 100%; background: var(--primary); transition: width 0.3s; }"
        ".wifi-list { max-height: 300px; overflow-y: auto; }"
        ".wifi-item { display: flex; justify-content: space-between; align-items: center; padding: 0.75rem; border-bottom: 1px solid var(--border); cursor: pointer; }"
        ".wifi-item:hover { background: var(--bg); }"
        ".wifi-ssid { font-weight: 500; }"
        ".wifi-rssi { font-size: 0.75rem; color: var(--text-secondary); }"
        ".toast { position: fixed; bottom: 1rem; right: 1rem; padding: 1rem 1.5rem; border-radius: 0.5rem; color: white; font-weight: 500; transform: translateY(100px); opacity: 0; transition: all 0.3s; z-index: 1000; }"
        ".toast.success { background: var(--success); }"
        ".toast.error { background: var(--error); }"
        ".toast.warning { background: var(--warning); }"
        ".toast.show { transform: translateY(0); opacity: 1; }"
        "@media (max-width: 640px) { header { flex-direction: column; gap: 0.5rem; } .info-grid { grid-template-columns: 1fr; } }"
    "</style>"
"</head>"
"<body>"
    "<header>"
        "<h1>ESPX Device Manager</h1>"
        "<div class=\"status\">"
            "<span id=\"connection-status\">Connecting...</span>"
            "<div id=\"status-dot\" class=\"status-dot\"></div>"
        "</div>"
    "</header>"
    "<main>"
        "<nav>"
            "<button class=\"tab-btn active\" data-tab=\"system\">System</button>"
            "<button class=\"tab-btn\" data-tab=\"params\">Parameters</button>"
            "<button class=\"tab-btn\" data-tab=\"wifi\">Wi-Fi</button>"
            "<button class=\"tab-btn\" data-tab=\"ota\">OTA</button>"
            "<button class=\"tab-btn\" data-tab=\"certs\">Certificates</button>"
        "</nav>"
        "<div id=\"system-tab\" class=\"tab-content active\">"
            "<div class=\"card\">"
                "<h2>Device Information</h2>"
                "<div class=\"info-grid\">"
                    "<div class=\"info-item\"><div class=\"info-label\">Device Name</div><div class=\"info-value\" id=\"device-name\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Device ID</div><div class=\"info-value\" id=\"device-id\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Firmware Version</div><div class=\"info-value\" id=\"firmware-ver\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Chip Model</div><div class=\"info-value\" id=\"chip-model\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Uptime</div><div class=\"info-value\" id=\"uptime\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Free Heap</div><div class=\"info-value\" id=\"free-heap\">-</div></div>"
                "</div>"
            "</div>"
            "<div class=\"card\"><h2>Actions</h2><div class=\"btn-group\"><button class=\"btn btn-danger\" id=\"reboot-btn\">Reboot Device</button></div></div>"
        "</div>"
        "<div id=\"params-tab\" class=\"tab-content\">"
            "<div class=\"card\">"
                "<h2>MQTT Configuration</h2>"
                "<form id=\"mqtt-form\">"
                    "<div class=\"form-group\"><label>Broker URL</label><input type=\"text\" id=\"mqtt-broker\" placeholder=\"mqtt://localhost:1883\"></div>"
                    "<div class=\"form-group\"><label>Username</label><input type=\"text\" id=\"mqtt-username\" placeholder=\"Username\"></div>"
                    "<div class=\"form-group\"><label>Password</label><input type=\"password\" id=\"mqtt-password\" placeholder=\"Password\"></div>"
                    "<div class=\"btn-group\"><button type=\"submit\" class=\"btn btn-primary\">Save MQTT Settings</button></div>"
                "</form>"
            "</div>"
            "<div class=\"card\">"
                "<h2>OTA Configuration</h2>"
                "<form id=\"ota-form\">"
                    "<div class=\"form-group\"><label>OTA Server URL</label><input type=\"text\" id=\"ota-url\" placeholder=\"https://server.com/ota/\"></div>"
                    "<div class=\"btn-group\"><button type=\"submit\" class=\"btn btn-primary\">Save OTA Settings</button></div>"
                "</form>"
            "</div>"
        "</div>"
        "<div id=\"wifi-tab\" class=\"tab-content\">"
            "<div class=\"card\">"
                "<h2>Wi-Fi Status</h2>"
                "<div class=\"info-grid\">"
                    "<div class=\"info-item\"><div class=\"info-label\">Status</div><div class=\"info-value\" id=\"wifi-status\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">SSID</div><div class=\"info-value\" id=\"wifi-ssid\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Signal Strength</div><div class=\"info-value\" id=\"wifi-rssi\">-</div></div>"
                "</div>"
            "</div>"
            "<div class=\"card\">"
                "<h2>Wi-Fi Configuration</h2>"
                "<form id=\"wifi-form\">"
                    "<div class=\"form-group\"><label>SSID</label><input type=\"text\" id=\"wifi-connect-ssid\" placeholder=\"Network Name\"></div>"
                    "<div class=\"form-group\"><label>Password</label><input type=\"password\" id=\"wifi-connect-password\" placeholder=\"Password\"></div>"
                    "<div class=\"btn-group\"><button type=\"submit\" class=\"btn btn-primary\">Connect</button><button type=\"button\" class=\"btn btn-secondary\" id=\"scan-btn\">Scan Networks</button></div>"
                "</form>"
            "</div>"
            "<div class=\"card\"><h2>Available Networks</h2><div id=\"wifi-list\" class=\"wifi-list\"><p style=\"color: var(--text-secondary); text-align: center; padding: 1rem;\">Click \"Scan Networks\" to find available Wi-Fi networks</p></div></div>"
        "</div>"
        "<div id=\"ota-tab\" class=\"tab-content\">"
            "<div class=\"card\">"
                "<h2>OTA Status</h2>"
                "<div class=\"info-grid\">"
                    "<div class=\"info-item\"><div class=\"info-label\">Current Version</div><div class=\"info-value\" id=\"ota-current-ver\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">State</div><div class=\"info-value\" id=\"ota-state\">IDLE</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Progress</div><div class=\"info-value\" id=\"ota-progress-text\">0%</div></div>"
                "</div>"
                "<div class=\"progress-bar\" id=\"ota-progress-bar\" style=\"display: none;\"><div class=\"progress-fill\" id=\"ota-progress-fill\" style=\"width: 0%;\"></div></div>"
            "</div>"
            "<div class=\"card\">"
                "<h2>Start OTA Update</h2>"
                "<form id=\"ota-update-form\">"
                    "<div class=\"form-group\"><label>OTA Server URL</label><input type=\"text\" id=\"ota-update-url\" placeholder=\"https://server.com/ota/manifest.json\"></div>"
                    "<div class=\"btn-group\"><button type=\"submit\" class=\"btn btn-primary\" id=\"ota-start-btn\">Start Update</button><button type=\"button\" class=\"btn btn-danger\" id=\"ota-cancel-btn\" disabled>Cancel</button></div>"
                "</form>"
            "</div>"
        "</div>"
        "<div id=\"certs-tab\" class=\"tab-content\">"
            "<div class=\"card\">"
                "<h2>Certificate Information</h2>"
                "<div class=\"info-grid\">"
                    "<div class=\"info-item\"><div class=\"info-label\">Valid</div><div class=\"info-value\" id=\"cert-valid\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Device</div><div class=\"info-value\" id=\"cert-device\">-</div></div>"
                    "<div class=\"info-item\"><div class=\"info-label\">Algorithm</div><div class=\"info-value\" id=\"cert-algo\">-</div></div>"
                "</div>"
            "</div>"
            "<div class=\"card\">"
                "<h2>Certificate Actions</h2>"
                "<p style=\"margin-bottom: 1rem; color: var(--text-secondary);\">Regenerating the certificate will create a new self-signed certificate. The device will need to be rebooted for changes to take effect.</p>"
                "<div class=\"btn-group\"><button class=\"btn btn-secondary\" id=\"regen-cert-btn\">Regenerate Certificate</button></div>"
            "</div>"
        "</div>"
    "</main>"
    "<div id=\"toast\" class=\"toast\"></div>"
    "<script>"
        "const API_BASE = '';"
        "let systemInfo = {};"
        "function showToast(message, type = 'success') { const toast = document.getElementById('toast'); toast.textContent = message; toast.className = 'toast ' + type + ' show'; setTimeout(() => toast.classList.remove('show'), 3000); }"
        "function formatBytes(bytes) { if (bytes < 1024) return bytes + ' B'; if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'; return (bytes / (1024 * 1024)).toFixed(1) + ' MB'; }"
        "function formatUptime(seconds) { const d = Math.floor(seconds / 86400); const h = Math.floor((seconds % 86400) / 3600); const m = Math.floor((seconds % 3600) / 60); const s = seconds % 60; if (d > 0) return d + 'd ' + h + 'h ' + m + 'm'; if (h > 0) return h + 'h ' + m + 'm ' + s + 's'; return m + 'm ' + s + 's'; }"
        "async function api(endpoint, options = {}) { try { const response = await fetch(API_BASE + endpoint, { headers: { 'Content-Type': 'application/json' }, ...options }); return await response.json(); } catch (error) { console.error('API error:', error); showToast('Connection error', 'error'); throw error; } }"
        "async function loadSystemInfo() { try { systemInfo = await api('/api/system/info'); document.getElementById('device-name').textContent = systemInfo.device_name || '-'; document.getElementById('device-id').textContent = systemInfo.device_id || '-'; document.getElementById('firmware-ver').textContent = systemInfo.firmware_ver || '-'; document.getElementById('chip-model').textContent = systemInfo.chip_model || '-'; document.getElementById('uptime').textContent = formatUptime(systemInfo.uptime || 0); document.getElementById('free-heap').textContent = formatBytes(systemInfo.free_heap || 0); document.getElementById('ota-current-ver').textContent = systemInfo.firmware_ver || '-'; if (systemInfo.wifi_ssid) { document.getElementById('wifi-ssid').textContent = systemInfo.wifi_ssid; document.getElementById('wifi-rssi').textContent = systemInfo.wifi_rssi + ' dBm'; document.getElementById('status-dot').classList.add('connected'); document.getElementById('connection-status').textContent = 'Connected'; } } catch (e) { console.error('Failed to load system info:', e); } }"
        "async function loadParams() { try { const params = await api('/api/params'); if (params.mqtt_broker) document.getElementById('mqtt-broker').value = params.mqtt_broker; if (params.mqtt_username) document.getElementById('mqtt-username').value = params.mqtt_username; if (params.ota_server_url) { document.getElementById('ota-url').value = params.ota_server_url; document.getElementById('ota-update-url').value = params.ota_server_url; } } catch (e) { console.error('Failed to load params:', e); } }"
        "async function loadWifiStatus() { try { const status = await api('/api/wifi/status'); document.getElementById('wifi-status').textContent = status.status || '-'; } catch (e) { console.error('Failed to load Wi-Fi status:', e); } }"
        "async function loadOtaStatus() { try { const status = await api('/api/ota/status'); document.getElementById('ota-state').textContent = status.state || 'IDLE'; document.getElementById('ota-progress-text').textContent = Math.round(status.progress || 0) + '%'; document.getElementById('ota-progress-fill').style.width = (status.progress || 0) + '%'; } catch (e) { console.error('Failed to load OTA status:', e); } }"
        "async function loadCertInfo() { try { const info = await api('/api/certs/info'); document.getElementById('cert-valid').textContent = info.valid ? 'Yes' : 'No'; document.getElementById('cert-device').textContent = info.device || '-'; document.getElementById('cert-algo').textContent = info.algorithm || '-'; } catch (e) { console.error('Failed to load cert info:', e); } }"
        "document.querySelectorAll('.tab-btn').forEach(btn => { btn.addEventListener('click', () => { document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active')); document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active')); btn.classList.add('active'); document.getElementById(btn.dataset.tab + '-tab').classList.add('active'); }); });"
        "document.getElementById('mqtt-form').addEventListener('submit', async (e) => { e.preventDefault(); const data = { mqtt_broker: document.getElementById('mqtt-broker').value, mqtt_username: document.getElementById('mqtt-username').value, mqtt_password: document.getElementById('mqtt-password').value }; try { await api('/api/params/batch', { method: 'POST', body: JSON.stringify(data) }); showToast('MQTT settings saved'); } catch (e) { showToast('Failed to save MQTT settings', 'error'); } });"
        "document.getElementById('ota-form').addEventListener('submit', async (e) => { e.preventDefault(); const data = { ota_server_url: document.getElementById('ota-url').value }; try { await api('/api/params/batch', { method: 'POST', body: JSON.stringify(data) }); showToast('OTA settings saved'); } catch (e) { showToast('Failed to save OTA settings', 'error'); } });"
        "document.getElementById('wifi-form').addEventListener('submit', async (e) => { e.preventDefault(); const data = { ssid: document.getElementById('wifi-connect-ssid').value, password: document.getElementById('wifi-connect-password').value }; try { await api('/api/wifi/connect', { method: 'POST', body: JSON.stringify(data) }); showToast('Wi-Fi connection initiated'); setTimeout(loadWifiStatus, 2000); } catch (e) { showToast('Failed to connect', 'error'); } });"
        "document.getElementById('scan-btn').addEventListener('click', async () => { const btn = document.getElementById('scan-btn'); btn.disabled = true; btn.textContent = 'Scanning...'; try { const networks = await api('/api/wifi/scan', { method: 'POST' }); const list = document.getElementById('wifi-list'); if (networks.length === 0) { list.innerHTML = '<p style=\"color: var(--text-secondary); text-align: center;\">No networks found</p>'; } else { list.innerHTML = networks.map(n => '<div class=\"wifi-item\" onclick=\"selectNetwork(\\'' + n.ssid + '\\')\"><div><div class=\"wifi-ssid\">' + n.ssid + '</div><div class=\"wifi-rssi\">' + n.authmode + '</div></div><div class=\"wifi-rssi\">' + n.rssi + ' dBm</div></div>').join(''); } } catch (e) { showToast('Scan failed', 'error'); } btn.disabled = false; btn.textContent = 'Scan Networks'; });"
        "function selectNetwork(ssid) { document.getElementById('wifi-connect-ssid').value = ssid; document.getElementById('wifi-connect-password').value = ''; }"
        "document.getElementById('ota-update-form').addEventListener('submit', async (e) => { e.preventDefault(); const url = document.getElementById('ota-update-url').value; if (!url) { showToast('Please enter OTA URL', 'warning'); return; } try { await api('/api/ota/start', { method: 'POST', body: JSON.stringify({ url }) }); showToast('OTA update started'); document.getElementById('ota-start-btn').disabled = true; document.getElementById('ota-cancel-btn').disabled = false; } catch (e) { showToast('Failed to start OTA', 'error'); } });"
        "document.getElementById('ota-cancel-btn').addEventListener('click', async () => { try { await api('/api/ota/cancel', { method: 'POST' }); showToast('OTA cancelled'); document.getElementById('ota-start-btn').disabled = false; document.getElementById('ota-cancel-btn').disabled = true; } catch (e) { showToast('Failed to cancel OTA', 'error'); } });"
        "document.getElementById('reboot-btn').addEventListener('click', async () => { if (confirm('Are you sure you want to reboot the device?')) { try { await api('/api/system/reboot', { method: 'POST' }); } catch (e) { } } });"
        "document.getElementById('regen-cert-btn').addEventListener('click', async () => { if (confirm('Regenerate certificate? The device will need to be rebooted.')) { try { await api('/api/certs/regenerate', { method: 'POST' }); showToast('Certificate regenerated. Please reboot.'); loadCertInfo(); } catch (e) { showToast('Failed to regenerate certificate', 'error'); } } });"
        "loadSystemInfo(); loadParams(); loadWifiStatus(); loadOtaStatus(); loadCertInfo(); setInterval(loadSystemInfo, 5000); setInterval(loadOtaStatus, 2000);"
    "</script>"
"</body>"
"</html>";

/**
 * @brief Root handler - serve main page
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Static file handler
 */
static esp_err_t static_handler(httpd_req_t *req)
{
    // For now, serve the embedded page for all static requests
    return root_handler(req);
}

// Include handlers
#include "handlers/system_handler.h"
#include "handlers/params_handler.h"
#include "handlers/wifi_handler.h"
#include "handlers/ota_handler.h"
#include "handlers/mqtt_handler.h"

/**
 * @brief Register all HTTP handlers
 */
static esp_err_t register_handlers(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        // Root
        { .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = NULL },
        // Static files
        { .uri = "/static/*", .method = HTTP_GET, .handler = static_handler, .user_ctx = NULL },
        // API: System
        { .uri = "/api/system/info", .method = HTTP_GET, .handler = system_info_handler, .user_ctx = NULL },
        { .uri = "/api/system/reboot", .method = HTTP_POST, .handler = system_reboot_handler, .user_ctx = NULL },
        // API: Parameters
        { .uri = "/api/params", .method = HTTP_GET, .handler = params_get_all_handler, .user_ctx = NULL },
        { .uri = "/api/params/batch", .method = HTTP_POST, .handler = params_batch_handler, .user_ctx = NULL },
        // API: Wi-Fi
        { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler, .user_ctx = NULL },
        { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler, .user_ctx = NULL },
        { .uri = "/api/wifi/scan", .method = HTTP_POST, .handler = wifi_scan_handler, .user_ctx = NULL },
        // API: OTA
        { .uri = "/api/ota/status", .method = HTTP_GET, .handler = ota_status_handler, .user_ctx = NULL },
        { .uri = "/api/ota/start", .method = HTTP_POST, .handler = ota_start_handler, .user_ctx = NULL },
        { .uri = "/api/ota/cancel", .method = HTTP_POST, .handler = ota_cancel_handler, .user_ctx = NULL },
        // API: MQTT
        { .uri = "/api/mqtt/status", .method = HTTP_GET, .handler = mqtt_status_handler, .user_ctx = NULL },
        { .uri = "/api/mqtt/reconnect", .method = HTTP_POST, .handler = mqtt_reconnect_handler, .user_ctx = NULL },
        // API: Certificates
        { .uri = "/api/certs/info", .method = HTTP_GET, .handler = certs_info_handler, .user_ctx = NULL },
        { .uri = "/api/certs/regenerate", .method = HTTP_POST, .handler = certs_regenerate_handler, .user_ctx = NULL },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI %s: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

// Public API implementation
esp_err_t web_server_start(void)
{
    if (g_running) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    server_cert_t cert;
    esp_err_t err;

    // Get certificate
    err = cert_manager_get_server_cert(&cert);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate: %s", esp_err_to_name(err));
        return err;
    }

    // HTTPS server configuration
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = (uint8_t *)cert.cert_pem;
    config.servercert_len = strlen(cert.cert_pem);
    config.privkey = (uint8_t *)cert.key_pem;
    config.privkey_len = strlen(cert.key_pem);
    config.httpd.task_stack = 8192;

    ESP_LOGI(TAG, "Starting HTTPS server on port %d...", config.port);

    err = httpd_ssl_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
        cert_manager_free_cert(&cert);
        return err;
    }

    // Register handlers
    err = register_handlers(g_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register handlers: %s", esp_err_to_name(err));
        httpd_ssl_stop(g_server);
        g_server = NULL;
        cert_manager_free_cert(&cert);
        return err;
    }

    cert_manager_free_cert(&cert);

    g_running = true;
    ESP_LOGI(TAG, "HTTPS server started successfully");

    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!g_running || g_server == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTPS server...");

    esp_err_t err = httpd_ssl_stop(g_server);
    if (err == ESP_OK) {
        g_server = NULL;
        g_running = false;
        ESP_LOGI(TAG, "HTTPS server stopped");
    }

    return err;
}

bool web_server_is_running(void)
{
    return g_running;
}

esp_err_t web_server_send_json(void *httpd_req, const char *json, int status)
{
    httpd_req_t *req = (httpd_req_t *)httpd_req;
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                               status == 400 ? "400 Bad Request" :
                               status == 404 ? "404 Not Found" :
                               status == 500 ? "500 Internal Server Error" : "500");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_server_send_error(void *httpd_req, const char *message, int status)
{
    char error_json[256];
    snprintf(error_json, sizeof(error_json), "{\"error\": \"%s\"}", message);
    return web_server_send_json(httpd_req, error_json, status);
}
