/**
 * @file wifi_service.c
 * @brief ESP Wi-Fi Service 封装层实现
 * 
 * 提供简化的 Wi-Fi 配网功能
 */

#include "wifi_service.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include <string.h>

static const char *TAG = "wifi_service";

/** @brief Wi-Fi 服务状态 */
static volatile wifi_service_state_t g_wifi_state = WIFI_SERVICE_STATE_IDLE;

/** @brief 事件回调 */
static wifi_service_event_callback_t g_event_callback = NULL;
static void *g_user_data = NULL;

/** @brief HTTP 服务器句柄 */
static httpd_handle_t g_httpd_handle = NULL;

/** @brief AP SSID */
static char g_ap_ssid[32] = "ESPX-XXXXXXXX";

/** @brief AP 密码 */
#define PROV_AP_PASSWORD "12345678"

/** @brief 生成随机 SSID */
static void generate_ap_ssid(char *buf, size_t len) {
    const char *prefix = "ESPX-";
    strncpy(buf, prefix, len - 9);
    uint32_t rand = esp_random();
    snprintf(buf + strlen(prefix), len - strlen(prefix), "%08X", rand);
}

/** @brief Wi-Fi 事件处理 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Wi-Fi connected to AP");
                g_wifi_state = WIFI_SERVICE_STATE_CONNECTING;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Wi-Fi disconnected");
                g_wifi_state = WIFI_SERVICE_STATE_DISCONNECTED;
                if (g_event_callback) {
                    wifi_service_event_t event = {
                        .type = WIFI_SERVICE_EVENT_DISCONNECTED,
                        .user_data = g_user_data
                    };
                    g_event_callback(&event, g_user_data);
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Station connected to AP");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Station disconnected from AP");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                g_wifi_state = WIFI_SERVICE_STATE_CONNECTED;
                if (g_event_callback) {
                    wifi_service_event_t event = {
                        .type = WIFI_SERVICE_EVENT_GOT_IP,
                        .user_data = g_user_data
                    };
                    g_event_callback(&event, g_user_data);
                }
                break;
            }
            default:
                break;
        }
    }
}

esp_err_t wifi_service_init(void) {
    if (g_wifi_state != WIFI_SERVICE_STATE_IDLE) {
        ESP_LOGW(TAG, "Wi-Fi service already initialized");
        return ESP_OK;
    }
    
    // 初始化网络层
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 创建默认 STA
    esp_netif_t *sta_netif = esp_netif_create_default_wifiSTA();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }
    
    // 初始化 Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 注册事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                       &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                       &wifi_event_handler, NULL, NULL));
    
    // 设置 STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    g_wifi_state = WIFI_SERVICE_STATE_IDLE;
    ESP_LOGI(TAG, "Wi-Fi service initialized");
    
    return ESP_OK;
}

esp_err_t wifi_service_deinit(void) {
    if (g_wifi_state == WIFI_SERVICE_STATE_PROVISIONING) {
        wifi_service_stop_provisioning();
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    g_wifi_state = WIFI_SERVICE_STATE_IDLE;
    ESP_LOGI(TAG, "Wi-Fi service deinitialized");
    
    return ESP_OK;
}

esp_err_t wifi_service_connect(const char *ssid, const char *password) {
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_wifi_state == WIFI_SERVICE_STATE_PROVISIONING) {
        ESP_LOGW(TAG, "Cannot connect while in provisioning mode");
        return ESP_ERR_INVALID_STATE;
    }
    
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    g_wifi_state = WIFI_SERVICE_STATE_CONNECTING;
    ESP_LOGI(TAG, "Connecting to Wi-Fi: %s", ssid);
    
    return ESP_OK;
}

esp_err_t wifi_service_disconnect(void) {
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) {
        g_wifi_state = WIFI_SERVICE_STATE_DISCONNECTED;
    }
    return err;
}

esp_err_t wifi_service_register_callback(wifi_service_event_callback_t callback, void *user_data) {
    g_event_callback = callback;
    g_user_data = user_data;
    return ESP_OK;
}

wifi_service_state_t wifi_service_get_state(void) {
    return g_wifi_state;
}

esp_netif_t *wifi_service_get_netif(void) {
    return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}

bool wifi_service_is_provisioned(void) {
    // 检查 NVS 中是否有 Wi-Fi 配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }
    
    char ssid[32] = {0};
    size_t len = sizeof(ssid);
    err = nvs_get_str(nvs, "ssid", ssid, &len);
    nvs_close(nvs);
    
    return (err == ESP_OK && strlen(ssid) > 0);
}

// ============================================================================
// 简化的配网页面
// ============================================================================

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
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_set_str(nvs, "ssid", wifi_ssid);
    if (err == ESP_OK && wifi_password != NULL && strlen(wifi_password) > 0) {
        err = nvs_set_str(nvs, "password", wifi_password);
    }
    
    nvs_commit(nvs);
    nvs_close(nvs);
    
    return err;
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

static wifi_ap_record_t g_ap_records[20];
static uint16_t g_ap_count = 0;
static bool g_scan_in_progress = false;

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
        "<div class=msg id=msg></div>"
        "</div>"
        "<script>"
        "fetch('/c').then(r=>r.json()).then(d=>{document.getElementById('m').textContent=d.mac});"
        "var selNet=function(n){document.getElementById('ssid').value=n};"
        "var showPwd=false;"
        "var togglePwd=function(){showPwd=!showPwd;document.getElementById('pass').type=showPwd?'text':'password';};"
        "var doScan=function(){var b=document.getElementById('scanBtn'),l=document.getElementById('netList');"
        "b.disabled=true;b.textContent='Scanning...';l.innerHTML='<div style=padding:8px>Scanning...</div>';"
        "fetch('/scan').then(r=>r.json()).then(d=>{if(d.status==='scanning'){setTimeout(poll,500);}});};"
        "var poll=function(){fetch('/scan_results').then(r=>r.json()).then(function(d){"
        "var b=document.getElementById('scanBtn'),l=document.getElementById('netList');"
        "b.disabled=false;b.textContent='Scan WiFi';"
        "if(d.status==='scanning'){setTimeout(poll,500);return;}"
        "if(d.count>0){var h='';d.networks.forEach(function(n){"
        "h+='<div class=net-i onclick=selNet(&quot;'+n.ssid+'&quot;)>';"
        "h+='<div class=net-s>'+n.ssid+'</div><div class=net-r>'+n.rssi+'dBm | '+n.auth+' | Ch:'+n.ch+'</div></div>';});l.innerHTML=h;}else{l.innerHTML='<div style=padding:8px>No networks found</div>';}"
        "}).catch(function(){b.disabled=false;b.textContent='Scan WiFi';l.innerHTML='<div style=padding:8px>Scan failed</div>';});};"
        "var doSave=function(){var s=document.getElementById('ssid').value.trim(),p=document.getElementById('pass').value;"
        "if(!s){alert('Please enter SSID');return;}"
        "var b=document.getElementById('subBtn'),m=document.getElementById('msg');b.disabled=true;b.textContent='Saving...';"
        "fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'wifi_ssid='+encodeURIComponent(s)+'&wifi_password='+encodeURIComponent(p)})"
        ".then(function(r){return r.json();})"
        ".then(function(d){if(d.success){m.textContent='Success! Rebooting...';m.className='msg suc';b.textContent='Done';}else{m.textContent='Error: '+(d.message||'Save failed');m.className='msg err';b.disabled=false;b.textContent='Save';}});};"
        "</script>"
        "</body>"
        "</html>",
        g_ap_ssid);
    
    if (page == NULL || len < 0) return ESP_FAIL;
    
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
        if (l >= (int)sizeof(pass)) l = sizeof(pass) - 1;
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

    g_wifi_state = WIFI_SERVICE_STATE_CONNECTED;
    httpd_resp_send(req, "{\"success\":true}", -1);
    
    if (g_event_callback) {
        wifi_service_event_t event = {
            .type = WIFI_SERVICE_EVENT_PROVISIONING_SUCCESS,
            .user_data = g_user_data
        };
        g_event_callback(&event, g_user_data);
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
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
    };
    for (size_t i = 0; i < sizeof(h)/sizeof(h[0]); i++) {
        httpd_register_uri_handler(g_httpd_handle, &h[i]);
    }
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

esp_err_t wifi_service_start_provisioning(const char *pop) {
    (void)pop;  // POP 暂未使用
    
    if (g_wifi_state == WIFI_SERVICE_STATE_PROVISIONING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting provisioning mode...");
    
    // 生成随机 SSID
    generate_ap_ssid(g_ap_ssid, sizeof(g_ap_ssid));
    ESP_LOGI(TAG, "AP SSID: %s", g_ap_ssid);
    
    // 创建 AP netif
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }
    
    // 配置 AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .channel = 6,
            .password = PROV_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };
    memcpy(ap_config.ap.ssid, g_ap_ssid, strlen(g_ap_ssid));
    ap_config.ap.ssid_len = strlen(g_ap_ssid);
    
    // 切换到 AP 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // 设置固定 IP
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    esp_netif_ip_info_t ip_info = {
        .ip = {.addr = 0x0104A8C0},      // 192.168.4.1
        .gw = {.addr = 0x0104A8C0},
        .netmask = {.addr = 0x00FFFFFF},
    };
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    
    // 启动 HTTP 服务器
    start_http_server();
    
    g_wifi_state = WIFI_SERVICE_STATE_PROVISIONING;
    
    if (g_event_callback) {
        wifi_service_event_t event = {
            .type = WIFI_SERVICE_EVENT_PROVISIONING_START,
            .user_data = g_user_data
        };
        g_event_callback(&event, g_user_data);
    }
    
    ESP_LOGI(TAG, "Provisioning mode started");
    
    return ESP_OK;
}

esp_err_t wifi_service_stop_provisioning(void) {
    if (g_httpd_handle != NULL) {
        httpd_stop(g_httpd_handle);
        g_httpd_handle = NULL;
    }
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    g_wifi_state = WIFI_SERVICE_STATE_IDLE;
    ESP_LOGI(TAG, "Provisioning mode stopped");
    
    return ESP_OK;
}
