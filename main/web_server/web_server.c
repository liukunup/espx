/**
 * @file web_server.c
 * @brief ESPX HTTPS Web Server implementation
 *
 * Uses mbedtls self-signed certificate.
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_http_server.h>
#include <cJSON.h>

#include "web_server.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "cert_manager/cert_manager.h"

static const char *TAG = "web_server";

static httpd_handle_t g_server = NULL;

// Embedded HTML page (kept simple - in production use SPIFFS or partition)
static const char index_html[] =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESPX Device Manager</title>"
"<style>"
":root{--p:#2563eb;--bg:#f8fafc;--card:#fff;--text:#1e293b;--muted:#64748b;--border:#e2e8f0;--ok:#22c55e;--err:#ef4444}"
"*{*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.6}"
"header{background:var(--p);color:#fff;padding:1rem 2rem;display:flex;justify-content:space-between;align-items:center}"
"header h1{font-size:1.25rem}"
".status{font-size:.875rem;display:flex;gap:1rem;align-items:center}"
".dot{width:10px;height:10px;border-radius:50%;background:var(--err)}"
".dot.ok{background:var(--ok)}"
"main{max-width:1200px;margin:2rem auto;padding:0 1rem}"
"nav{display:flex;gap:.5rem;margin-bottom:1.5rem;flex-wrap:wrap}"
".tab{padding:.75rem 1.5rem;border:none;background:var(--card);color:var(--muted);border-radius:.5rem;cursor:pointer;border:1px solid var(--border)}"
".tab.active{background:var(--p);color:#fff;border-color:var(--p)}"
".card{background:var(--card);border-radius:.75rem;padding:1.5rem;box-shadow:0 1px 3px rgba(0,0,0,.1);margin-bottom:1rem}"
".card h2{font-size:1.125rem;margin-bottom:1rem}"
".info{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem}"
".info-item{padding:.75rem;background:var(--bg);border-radius:.5rem}"
".lbl{font-size:.75rem;color:var(--muted);text-transform:uppercase}"
".val{font-size:1rem;font-weight:500}"
".row{display:flex;justify-content:space-between;align-items:center;padding:.75rem;border-bottom:1px solid var(--border)}"
".row:last-child{border-bottom:none}"
".badge{padding:.25rem .5rem;border-radius:.25rem;font-size:.75rem;background:var(--bg);color:var(--muted)}"
".badge.ok{background:#dcfce7;color:#166534}"
".badge.err{background:#fee2e2;color:#991b1b}"
"button{padding:.5rem 1rem;border:none;border-radius:.375rem;cursor:pointer;font-size:.875rem}"
".btn-p{background:var(--p);color:#fff}"
".btn-d{background:var(--err);color:#fff}"
".btn-s{background:var(--bg);border:1px solid var(--border)}"
"input,select{padding:.5rem;border:1px solid var(--border);border-radius:.375rem;font-size:.875rem;width:100%}"
".form-g{margin-bottom:.75rem}"
".form-g label{display:block;font-size:.875rem;margin-bottom:.25rem}"
".toast{position:fixed;bottom:1rem;right:1rem;padding:1rem 1.5rem;border-radius:.5rem;color:#fff;font-weight:500;transform:translateY(100px);opacity:0;transition:all .3s;z-index:1000}"
".toast.show{transform:translateY(0);opacity:1}"
".toast.success{background:var(--ok)}"
".toast.error{background:var(--err)}"
".modal{position:fixed;inset:0;background:rgba(0,0,0,.5);display:none;align-items:center;justify-content:center;z-index:100}"
".modal.show{display:flex}"
".modal-content{background:#fff;border-radius:.75rem;padding:2rem;max-width:500px;width:90%;max-height:80vh;overflow-y:auto}"
".modal-content h3{margin-bottom:1rem}"
"</style></head>"
"<body>"
"<header><h1>ESPX Device Manager</h1><div class='status'><span id='cs'>Connecting...</span><div id='cd' class='dot'></div></div></header>"
"<main>"
"<nav>"
"<button class='tab active' data-tab='dashboard'>Dashboard</button>"
"<button class='tab' data-tab='devices'>Devices</button>"
"<button class='tab' data-tab='system'>System</button>"
"</nav>"
"<div id='dashboard-tab' class='tab-content'>"
"<div class='card'><h2>Node Info</h2><div class='info'>"
"<div class='info-item'><div class='lbl'>Device ID</div><div class='val' id='i-device-id'>-</div></div>"
"<div class='info-item'><div class='lbl'>Name</div><div class='val' id='i-name'>-</div></div>"
"<div class='info-item'><div class='lbl'>Version</div><div class='val' id='i-version'>-</div></div>"
"<div class='info-item'><div class='lbl'>Devices</div><div class='val' id='i-device-count'>-</div></div>"
"</div></div>"
"<div class='card'><h2>Device Values</h2><div id='device-values'>Loading...</div></div>"
"</div>"
"<div id='devices-tab' class='tab-content' style='display:none'>"
"<div class='card'>"
"<div style='display:flex;justify-content:space-between;margin-bottom:1rem'>"
"<h2>Devices</h2>"
"<button class='btn-p' id='add-device-btn'>+ Add Device</button>"
"</div>"
"<div id='device-list'>Loading...</div>"
"</div>"
"</div>"
"<div id='system-tab' class='tab-content' style='display:none'>"
"<div class='card'>"
"<h2>System Actions</h2>"
"<div style='display:flex;gap:.5rem;margin-top:1rem'>"
"<button class='btn-d' id='reboot-btn'>Reboot Device</button>"
"<button class='btn-s' id='reload-btn'>Reload Devices</button>"
"</div>"
"</div>"
"</div>"
"</main>"
"<div id='toast' class='toast'></div>"
"<div id='add-modal' class='modal'>"
"<div class='modal-content'>"
"<h3>Add Device</h3>"
"<div class='form-g'><label>Device ID</label><input id='m-id' placeholder='my_device_1'></div>"
"<div class='form-g'><label>Type</label><select id='m-type'></select></div>"
"<div id='m-config-fields'></div>"
"<div style='display:flex;gap:.5rem;margin-top:1rem'>"
"<button class='btn-p' id='m-save'>Save</button>"
"<button class='btn-s' id='m-cancel'>Cancel</button>"
"</div></div></div>"
"<script>"
"const A='';"
"function toast(m,t='success'){const e=document.getElementById('toast');e.textContent=m;e.className='toast '+t+' show';setTimeout(()=>e.classList.remove('show'),3000)}"
"async function api(p,o={}){try{const r=await fetch(A+p,{headers:{'Content-Type':'application/json'},...o});const d=await r.json();return d}catch(e){toast('Connection error','error');throw e}}"
"async function loadNode(){const i=await api('/api/node');document.getElementById('i-device-id').textContent=i.device_id;document.getElementById('i-name').textContent=i.name;document.getElementById('i-version').textContent=i.version;document.getElementById('i-device-count').textContent=i.device_count;document.getElementById('cs').textContent='Connected';document.getElementById('cd').classList.add('ok')}"
"async function loadDevices(){const d=await api('/api/devices');const c=document.getElementById('device-values');if(d.length===0){c.innerHTML='<p style=\"color:var(--muted);text-align:center\">No devices configured</p>';return}"
"c.innerHTML=d.map(x=>`<div class=\"row\"><div><strong>${x.id}</strong><br><small style=\"color:var(--muted)\">${x.type}</small></div><div class=\"val\">${x.value!==undefined?JSON.stringify(x.value):'-'}</div></div>`).join('');"
"const list=document.getElementById('device-list');"
"if(d.length===0){list.innerHTML='<p style=\"color:var(--muted);text-align:center\">No devices configured</p>'}"
"else{list.innerHTML=d.map(x=>`<div class=\"row\"><div><strong>${x.id}</strong> <span class=\"badge\">${x.type}</span><br><small style=\"color:var(--muted)\">${x.initialized?'initialized':'not initialized'}</small></div><div style=\"display:flex;gap:.5rem\"><button class=\"btn-s\" onclick=\"readDev('${x.id}')\">Read</button><button class=\"btn-s\" onclick=\"toggleDev('${x.id}',${!x.enabled})\">${x.enabled?'Disable':'Enable'}</button><button class=\"btn-d\" onclick=\"delDev('${x.id}')\">Delete</button></div></div>`).join('')}"
"}"
"async function loadTypes(){const t=await api('/api/device-types');return t}"
"document.querySelectorAll('.tab').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));document.querySelectorAll('.tab-content').forEach(x=>x.style.display='none');b.classList.add('active');document.getElementById(b.dataset.tab+'-tab').style.display='block'}))"
"async function readDev(id){try{const v=await api('/api/devices/'+id+'/read',{method:'POST'});toast(id+': '+JSON.stringify(v));loadDevices()}catch(e){}}"
"async function toggleDev(id,en){try{await api('/api/devices/'+id+'/enable',{method:'POST',body:JSON.stringify({enabled:en})});loadDevices()}catch(e){}}"
"async function delDev(id){if(!confirm('Delete '+id+'?'))return;try{await api('/api/devices/'+id,{method:'DELETE'});loadDevices();toast('Deleted')}catch(e){}}"
"document.getElementById('reboot-btn').addEventListener('click',async()=>{if(confirm('Reboot?'))await api('/api/system/reboot',{method:'POST'})})"
"document.getElementById('reload-btn').addEventListener('click',async()=>{await api('/api/devices/reload',{method:'POST'});loadDevices();toast('Reloaded')})"
"document.getElementById('add-device-btn').addEventListener('click',async()=>{const types=await loadTypes();const sel=document.getElementById('m-type');sel.innerHTML=types.map(t=>`<option value=\"${t.name}\">${t.name} - ${t.description}</option>`).join('');sel.onchange=()=>updateConfigFields(sel.value);document.getElementById('add-modal').classList.add('show')})"
"function updateConfigFields(type){const types=window._deviceTypes||[];const t=types.find(x=>x.name===type);if(!t)return;const c=document.getElementById('m-config-fields');c.innerHTML=Object.keys(t.default_config).map(k=>`<div class=\"form-g\"><label>${k}</label><input id=\"m-cfg-${k}\" value=\"${t.default_config[k]}\"></div>`).join('')}"
"document.getElementById('m-cancel').addEventListener('click',()=>document.getElementById('add-modal').classList.remove('show'))"
"document.getElementById('m-save').addEventListener('click',async()=>{const id=document.getElementById('m-id').value;const type=document.getElementById('m-type').value;const cfg={};"
"document.querySelectorAll('[id^=\"m-cfg-\"]').forEach(e=>{cfg[e.id.substring(6)]=isNaN(+e.value)?e.value:+e.value});"
"try{await api('/api/devices',{method:'POST',body:JSON.stringify({id,type,config:cfg})});toast('Added');document.getElementById('add-modal').classList.remove('show');loadDevices()}catch(e){}})"
"async function init(){try{await loadNode();await loadDevices()}catch(e){console.error(e)}"
"const types=await loadTypes();window._deviceTypes=types;}"
"init();setInterval(loadDevices,5000);"
"</script></body></html>";

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
 * @brief Helper: send JSON response
 */
static esp_err_t send_json(httpd_req_t *req, cJSON *json, int status)
{
    char *str = cJSON_PrintUnformatted(json);
    if (str == NULL) return ESP_FAIL;

    const char *status_str = (status == 200) ? "200 OK" :
                             (status == 400) ? "400 Bad Request" :
                             (status == 404) ? "404 Not Found" : "500 Internal Server Error";

    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/**
 * @brief Helper: send error response
 */
static esp_err_t send_error(httpd_req_t *req, const char *msg, int status)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", msg);
    return send_json(req, json, status);
}

/**
 * @brief GET /api/node - node info
 */
static esp_err_t api_node_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_id", node_config_get_device_id());
    cJSON_AddStringToObject(json, "name", node_config_get_name());
    cJSON_AddStringToObject(json, "version", CONFIG_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(json, "device_count", device_get_count());

    return send_json(req, json, 200);
}

/**
 * @brief GET /api/devices - list devices with values
 */
static esp_err_t api_devices_list_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    size_t count;
    const device_t *devices = device_get_all(&count);

    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", devices[i].id);
        cJSON_AddStringToObject(item, "type", devices[i].type->name);
        cJSON_AddStringToObject(item, "description", devices[i].type->description);
        cJSON_AddBoolToObject(item, "enabled", devices[i].enabled);
        cJSON_AddBoolToObject(item, "initialized", devices[i].initialized);

        // Get current value
        if (devices[i].enabled && devices[i].initialized && devices[i].type->read) {
            cJSON *value = cJSON_CreateObject();
            if (devices[i].type->read((device_t*)&devices[i], value) == ESP_OK) {
                cJSON_AddItemToObject(item, "value", value);
            } else {
                cJSON_Delete(value);
            }
        }

        cJSON_AddItemToArray(json, item);
    }

    return send_json(req, json, 200);
}

/**
 * @brief POST /api/devices - add device
 */
static esp_err_t api_device_add_handler(httpd_req_t *req)
{
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return send_error(req, "Invalid JSON", 400);

    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *config = cJSON_GetObjectItem(root, "config");

    if (!cJSON_IsString(id) || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return send_error(req, "Missing id or type", 400);
    }

    esp_err_t err = device_add(id->valuestring, type->valuestring, config);
    if (err == ESP_OK) {
        device_manager_save();
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    cJSON_AddStringToObject(resp, "error", err != ESP_OK ? esp_err_to_name(err) : "");
    int status = (err == ESP_OK) ? 200 : 400;
    cJSON_Delete(root);
    return send_json(req, resp, status);
}

/**
 * @brief Helper: parse device id and action from /api/devices/{id}[/{action}]
 *
 * @param uri      Request URI
 * @param id_out   Output buffer for device id
 * @param id_size  Size of id_out
 * @param action_out Output buffer for action (may be empty string)
 * @param action_size Size of action_out
 * @return true if URI matched
 */
static bool parse_device_uri(const char *uri, char *id_out, size_t id_size,
                             char *action_out, size_t action_size)
{
    const char *prefix = "/api/devices/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *rest = uri + prefix_len;
    const char *slash = strchr(rest, '/');

    if (slash == NULL) {
        strncpy(id_out, rest, id_size - 1);
        id_out[id_size - 1] = '\0';
        if (action_out && action_size > 0) action_out[0] = '\0';
    } else {
        size_t id_len = slash - rest;
        if (id_len >= id_size) id_len = id_size - 1;
        memcpy(id_out, rest, id_len);
        id_out[id_len] = '\0';

        if (action_out && action_size > 0) {
            strncpy(action_out, slash + 1, action_size - 1);
            action_out[action_size - 1] = '\0';
        }
    }

    return true;
}

/**
 * @brief Dispatcher for /api/devices/{id}[/action]
 *
 * GET    /api/devices/{id}          - get device info
 * DELETE /api/devices/{id}          - remove device
 * POST   /api/devices/{id}/read     - read value
 * POST   /api/devices/{id}/write    - write value
 * POST   /api/devices/{id}/enable   - enable/disable
 */
static esp_err_t api_device_dispatch_handler(httpd_req_t *req)
{
    char id[64];
    char action[32];

    if (!parse_device_uri(req->uri, id, sizeof(id), action, sizeof(action))) {
        return send_error(req, "Invalid URI", 400);
    }

    // ---- POST actions ----
    if (req->method == HTTP_POST) {
        if (strcmp(action, "read") == 0) {
            cJSON *json = cJSON_CreateObject();
            esp_err_t err = device_read(id, json);
            if (err != ESP_OK) {
                cJSON_Delete(json);
                return send_error(req, esp_err_to_name(err), 400);
            }
            return send_json(req, json, 200);
        }

        if (strcmp(action, "write") == 0) {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return send_error(req, "Invalid JSON", 400);

            esp_err_t err = device_write(id, root);
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            if (err != ESP_OK) {
                cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
            }
            return send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        if (strcmp(action, "enable") == 0) {
            char buf[128];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(root, "enabled"));
            cJSON_Delete(root);

            esp_err_t err = device_set_enabled(id, enabled);
            if (err == ESP_OK) {
                device_manager_save();
            }

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return send_json(req, resp, err == ESP_OK ? 200 : 404);
        }

        // POST /api/devices/{id} with new config -> update config
        if (action[0] == '\0') {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return send_error(req, "Invalid JSON", 400);

            cJSON *config = cJSON_GetObjectItem(root, "config");
            esp_err_t err = device_update_config(id, config);
            if (err == ESP_OK) {
                device_manager_save();
            }
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        return send_error(req, "Unknown action", 400);
    }

    // ---- GET /api/devices/{id} ----
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        esp_err_t err = device_get_json(id, json);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            return send_error(req, "Device not found", 404);
        }
        return send_json(req, json, 200);
    }

    // ---- DELETE /api/devices/{id} ----
    if (req->method == HTTP_DELETE) {
        esp_err_t err = device_remove(id);
        if (err == ESP_OK) {
            device_manager_save();
        }
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return send_json(req, resp, err == ESP_OK ? 200 : 404);
    }

    return send_error(req, "Method not allowed", 400);
}

/**
 * @brief POST /api/devices/reload - reload all devices
 */
static esp_err_t api_devices_reload_handler(httpd_req_t *req)
{
    esp_err_t err = device_manager_reload();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    return send_json(req, resp, err == ESP_OK ? 200 : 500);
}

/**
 * @brief GET /api/device-types - list device types
 */
static esp_err_t api_device_types_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    size_t count;
    const device_type_t *types = device_type_get_all(&count);

    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", types[i].name);
        cJSON_AddStringToObject(item, "description", types[i].description);
        cJSON_AddNumberToObject(item, "capabilities", types[i].capabilities);

        cJSON *cfg = cJSON_CreateObject();
        if (types[i].get_default_config) {
            types[i].get_default_config(cfg);
        }
        cJSON_AddItemToObject(item, "default_config", cfg);

        cJSON_AddItemToArray(json, item);
    }

    return send_json(req, json, 200);
}

/**
 * @brief POST /api/system/reboot
 */
static esp_err_t api_system_reboot_handler(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
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
    config.httpd.max_uri_handlers = 16;

    ESP_LOGI(TAG, "Starting HTTPS server...");
    err = httpd_ssl_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS: %s", esp_err_to_name(err));
        cert_manager_free_cert(&server_cert);
        return err;
    }

    // Register handlers - ORDER MATTERS: exact paths before wildcards
    const httpd_uri_t uris[] = {
        { .uri = "/",                   .method = HTTP_GET,    .handler = root_handler },
        { .uri = "/api/node",           .method = HTTP_GET,    .handler = api_node_handler },
        { .uri = "/api/devices",        .method = HTTP_GET,    .handler = api_devices_list_handler },
        { .uri = "/api/devices",        .method = HTTP_POST,   .handler = api_device_add_handler },
        { .uri = "/api/devices/reload", .method = HTTP_POST,   .handler = api_devices_reload_handler },
        { .uri = "/api/device-types",   .method = HTTP_GET,    .handler = api_device_types_handler },
        { .uri = "/api/system/reboot",  .method = HTTP_POST,   .handler = api_system_reboot_handler },
        // Wildcard dispatcher must be last
        { .uri = "/api/devices/*",      .method = HTTP_GET,    .handler = api_device_dispatch_handler },
        { .uri = "/api/devices/*",      .method = HTTP_POST,   .handler = api_device_dispatch_handler },
        { .uri = "/api/devices/*",      .method = HTTP_DELETE, .handler = api_device_dispatch_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(g_server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
        }
    }

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
