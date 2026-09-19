/**
 * @file wifi_provisioning.c
 * @brief Simple AP Provisioning - WiFi only
 */

#include "wifi_provisioning.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_storage.h"

static const char *TAG = "provisioning";
static volatile provisioning_state_t g_prov_state = PROV_STATE_IDLE;
static httpd_handle_t g_httpd_handle = NULL;
static char g_ap_ssid[32] = "ESPX-XXXXXXXX";
static bool g_initialized = false;

static wifi_ap_record_t g_ap_records[20];
static uint16_t g_ap_count = 0;
static bool g_scan_in_progress = false;

#define PROV_AP_PASSWORD "12345678"

static void generate_random_hex(char *buf, int len) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) {
        buf[i] = hex_chars[esp_random() & 0x0F];
    }
    buf[len] = '\0';
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* ev = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" connected", MAC2STR(ev->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* ev = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" disconnected", MAC2STR(ev->mac));
    }
}

static void url_decode(char *dst, size_t dst_len, const char *src) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dst_len - 1; si++) {
        if (src[si] == '%' && si + 2 < strlen(src)) {
            int hi = -1, lo = -1;
            char c1 = src[si + 1], c2 = src[si + 2];
            if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
            else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
            else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
            if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
            else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
            else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                si += 2;
            } else {
                dst[di++] = src[si];
            }
        } else if (src[si] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static esp_err_t save_config_to_nvs(const char *wifi_ssid, const char *wifi_password) {
    storage_init();
    storage_set_string("wifi_ssid", wifi_ssid);
    if (wifi_password && strlen(wifi_password) > 0) {
        storage_set_blob("wifi_password", wifi_password, strlen(wifi_password));
    }
    storage_commit();
    return 0;
}

static const char* get_auth_mode_str(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "Unknown";
    }
}

static void wifi_scan_task(void *params) {
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    wifi_config_t sta_cfg = {
        .sta = {.threshold = {.authmode = WIFI_AUTH_WPA2_PSK}},
    };
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {.active = {.min = 100, .max = 300}},
    };
    
    g_scan_in_progress = true;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    
    if (err == ESP_OK) {
        g_ap_count = 20;
        err = esp_wifi_scan_get_ap_records(&g_ap_count, g_ap_records);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Found %d networks", g_ap_count);
        } else {
            g_ap_count = 0;
        }
    } else {
        ESP_LOGW(TAG, "Scan failed");
        g_ap_count = 0;
    }
    
    esp_wifi_set_mode(WIFI_MODE_AP);
    g_scan_in_progress = false;
    vTaskDelete(NULL);
}

static esp_err_t scan_handler(httpd_req_t *req) {
    if (!g_scan_in_progress) {
        xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan", 4096, NULL, 3, NULL, 0);
    }
    httpd_resp_send(req, "{\"status\":\"scanning\"}", -1);
    return ESP_OK;
}

static esp_err_t scan_results_handler(httpd_req_t *req) {
    if (g_scan_in_progress) {
        httpd_resp_send(req, "{\"status\":\"scanning\",\"count\":0}", -1);
        return ESP_OK;
    }
    
    char json[4096];
    int off = snprintf(json, sizeof(json), "{\"status\":\"done\",\"count\":%d,\"networks\":[", g_ap_count);
    
    for (int i = 0; i < g_ap_count && i < 15; i++) {
        if (i > 0) off += snprintf(json + off, sizeof(json) - off, ",");
        off += snprintf(json + off, sizeof(json) - off,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"ch\":%d}",
            g_ap_records[i].ssid, g_ap_records[i].rssi,
            get_auth_mode_str(g_ap_records[i].authmode),
            g_ap_records[i].primary);
    }
    off += snprintf(json + off, sizeof(json) - off, "]}");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req) {
    char *page = NULL;
    int len = asprintf(&page,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>WiFi Config</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:system-ui,sans-serif;background:#667eea;min-height:100vh;display:flex;justify-content:center;align-items:center;padding:15px}"
        ".c{background:#fff;border-radius:12px;padding:24px;width:100%%;max-width:360px;box-shadow:0 10px 40px rgba(0,0,0,0.2)}"
        "h1{color:#333;text-align:center;font-size:20px;margin-bottom:16px}"
        ".info{background:#f0f0f0;padding:10px;border-radius:6px;margin-bottom:16px;font-size:12px;color:#666}"
        ".g{margin-bottom:14px}"
        "label{display:block;color:#555;font-size:13px;margin-bottom:4px}"
        "input{width:100%%;padding:10px;border:2px solid #ddd;border-radius:6px;font-size:14px}"
        "input:focus{border-color:#667eea;outline:none}"
        ".btn{width:100%%;padding:12px;background:#667eea;color:#fff;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer;margin-top:8px}"
        ".btn:hover{background:#5568d3}"
        ".btn:disabled{background:#ccc;cursor:not-allowed}"
        ".btn-g{background:#4CAF50}"
        ".btn-g:hover{background:#45a049}"
        ".msg{margin-top:12px;padding:10px;border-radius:6px;text-align:center;font-size:13px;display:none}"
        ".msg.suc{background:#d4edda;color:#155724;display:block}"
        ".msg.err{background:#f8d7da;color:#721c24;display:block}"
        ".net{background:#f5f5f5;padding:8px;border-radius:6px;max-height:150px;overflow-y:auto;margin-top:8px}"
        ".net-i{padding:8px;cursor:pointer;font-size:12px;border-bottom:1px solid #ddd}"
        ".net-i:last-child{border-bottom:none}"
        ".net-i:hover{background:#e0e0e0}"
        ".net-s{font-weight:600}"
        ".net-r{font-size:10px;color:#666}"
        ".pw-wrap{display:flex;gap:8px}"
        ".pw-wrap input{flex:1}"
        ".pw-btn{padding:10px 12px;background:#eee;border:2px solid #ddd;border-radius:6px;cursor:pointer;font-size:16px}"
        ".pw-btn:hover{background:#ddd}"
        "</style>"
        "</head>"
        "<body>"
        "<div class=c>"
        "<h1>WiFi Config</h1>"
        "<div class=info>AP: %s<br>MAC: <span id=m>-</span></div>"
        
        "<div class=g>"
        "<button class='btn btn-g' id=scanBtn onclick=doScan()>Scan WiFi</button>"
        "<div class=net id=netList>"
        "<div class='net-i' onclick=selNet('')>-- Manual --</div>"
        "<div style='padding:8px;font-size:12px;color:#888'>Click scan button</div>"
        "</div>"
        "</div>"
        
        "<div class=g>"
        "<label>SSID</label>"
        "<input id=ssid placeholder='WiFi name' required>"
        "</div>"
        
        "<div class=g>"
        "<label>Password</label>"
        "<div class=pw-wrap>"
        "<input type=password id=pass name=pass placeholder='Password (empty if none)'>"
        "<button class=pw-btn type=button onclick=togglePwd()>&#128065;</button>"
        "</div>"
        "</div>"
        
        "<button class=btn id=subBtn onclick=doSave()>Save & Reboot</button>"
        "<div style='margin-top:20px;border-top:1px solid #eee;padding-top:16px'>"
        "<button class=btn style='background:#f44336' onclick=doReset()>Factory Reset</button>"
        "</div>"
        "<div class=msg id=msg></div>"
        "</div>"
        
        "<script>"
        "fetch('/c').then(r=>r.json()).then(d=>{document.getElementById('m').textContent=d.mac});"
        "var selNet=function(n){document.getElementById('ssid').value=n};"
        "var showPwd=false;"
        "var togglePwd=function(){"
        "showPwd=!showPwd;"
        "var i=document.getElementById('pass');"
        "i.type=showPwd?'text':'password';"
        "};"
        "var doScan=function(){"
        "var b=document.getElementById('scanBtn'),l=document.getElementById('netList');"
        "b.disabled=true;b.textContent='Scanning...';"
        "l.innerHTML='<div style=padding:8px>Scanning...</div>';"
        "fetch('/scan').then(r=>r.json()).then(d=>{"
        "if(d.status==='scanning'){setTimeout(function(){poll();},500);}"
        "});"
        "};"
        "var poll=function(){"
        "fetch('/scan_results').then(r=>r.json()).then(function(d){"
        "var b=document.getElementById('scanBtn'),l=document.getElementById('netList');"
        "b.disabled=false;b.textContent='Scan WiFi';"
        "if(d.status==='scanning'){setTimeout(poll,500);return;}"
        "if(d.count>0){var h='';d.networks.forEach(function(n){"
        "h+='<div class=net-i onclick=selNet(&quot;'+n.ssid+'&quot;)>';"
        "h+='<div class=net-s>'+n.ssid+'</div>';"
        "h+='<div class=net-r>'+n.rssi+'dBm | '+n.auth+' | Ch:'+n.ch+'</div></div>';"
        "});l.innerHTML=h;}else{l.innerHTML='<div style=padding:8px>No networks found</div>';}"
        "}).catch(function(){b.disabled=false;b.textContent='Scan WiFi';l.innerHTML='<div style=padding:8px>Scan failed</div>';});"
        "};"
        "var doSave=function(){"
        "var s=document.getElementById('ssid').value.trim();"
        "var p=document.getElementById('pass').value;"
        "if(!s){alert('Please enter SSID');return;}"
        "var b=document.getElementById('subBtn'),m=document.getElementById('msg');"
        "b.disabled=true;b.textContent='Saving...';"
        "var pr='wifi_ssid='+encodeURIComponent(s)+'&wifi_password='+encodeURIComponent(p);"
        "fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:pr})"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
        "if(d.success){m.textContent='Success! Rebooting...';m.className='msg suc';b.textContent='Done';}"
        "else{m.textContent='Error: '+(d.message||'Save failed');m.className='msg err';b.disabled=false;b.textContent='Save';}"
        "})"
        ".catch(function(e){m.textContent='Error: '+e.message;m.className='msg err';b.disabled=false;b.textContent='Save';});"
        "};"
        "var doReset=function(){" 
        "if(!confirm('Factory reset? All settings will be erased!')){return;}"
        "fetch('/reset').then(function(r){return r.json();})"
        ".then(function(d){if(d.success){alert('Reset! Rebooting...');}else{alert('Reset failed');}});"
        "};"
        "</script>"
        "</body>"
        "</html>",
        g_ap_ssid);
    
    if (page == NULL || len < 0) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req, page, len);
    free(page);
    return err;
}

static esp_err_t config_handler(httpd_req_t *req) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char json[64];
    snprintf(json, sizeof(json), "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req) {
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send(req, "{\"success\":false,\"message\":\"No data\"}", -1);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char ssid[64] = {0}, pass[64] = {0};

    char *p = strstr(buf, "wifi_ssid=");
    if (p) {
        p += 10;
        char *e = strchr(p, '&');
        int l = e ? (e - p) : strlen(p);
        url_decode(ssid, sizeof(ssid), p);
        ssid[l] = '\0';
    }

    p = strstr(buf, "wifi_password=");
    if (p) {
        p += 14;
        char *e = strchr(p, '&');
        int l = e ? (e - p) : strlen(p);
        if (l >= sizeof(pass)) l = sizeof(pass) - 1;
        char tmp[64] = {0};
        strncpy(tmp, p, l);
        url_decode(pass, sizeof(pass), tmp);
    }

    if (!strlen(ssid)) {
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Missing SSID\"}", -1);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "WiFi: %s", ssid);

    esp_err_t err = save_config_to_nvs(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Save failed\"}", -1);
        return ESP_OK;
    }

    g_prov_state = PROV_STATE_COMPLETE;
    httpd_resp_send(req, "{\"success\":true}", -1);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Factory reset requested via web");
    
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"success\":true}");
    httpd_resp_send(req, resp, -1);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    nvs_flash_erase();
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    if (httpd_start(&g_httpd_handle, &config) != ESP_OK) {
        return ESP_FAIL;
    }
    httpd_uri_t h[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/c", .method = HTTP_GET, .handler = config_handler},
        {.uri = "/scan", .method = HTTP_GET, .handler = scan_handler},
        {.uri = "/scan_results", .method = HTTP_GET, .handler = scan_results_handler},
        {.uri = "/save", .method = HTTP_POST, .handler = save_handler},
        {.uri = "/reset", .method = HTTP_GET, .handler = reset_handler},
    };
    for (size_t i = 0; i < sizeof(h)/sizeof(h[0]); i++) {
        httpd_register_uri_handler(g_httpd_handle, &h[i]);
    }
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

int provisioning_init(void) {
    if (g_initialized) return 0;
    g_initialized = true;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_ap() == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return -1;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi initialized");
    return 0;
}

int provisioning_start(void) {
    if (g_prov_state != PROV_STATE_IDLE) return -1;

    generate_random_hex(g_ap_ssid + 5, 8);
    ESP_LOGI(TAG, "SSID: %s", g_ap_ssid);

    wifi_config_t cfg = {
        .ap = {
            .ssid = "", .ssid_len = 0, .channel = 6,
            .password = PROV_AP_PASSWORD, .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK, .pmf_cfg = {.required = false},
        },
    };
    memcpy(cfg.ap.ssid, g_ap_ssid, strlen(g_ap_ssid));
    cfg.ap.ssid_len = strlen(g_ap_ssid);

    wifi_country_t country = {
        .cc = "CN", .schan = 1, .nchan = 13, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif) {
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
        esp_netif_ip_info_t ip_info = {
            .ip = {.addr = 0x0104A8C0},
            .gw = {.addr = 0x0104A8C0},
            .netmask = {.addr = 0x00FFFFFF},
        };
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(40);

    g_prov_state = PROV_STATE_AP_READY;
    ESP_LOGI(TAG, "AP started - %s", g_ap_ssid);

    start_http_server();
    return 0;
}

provisioning_state_t provisioning_get_state(void) {
    return g_prov_state;
}

const char* provisioning_get_ap_ssid(void) {
    return g_ap_ssid;
}
