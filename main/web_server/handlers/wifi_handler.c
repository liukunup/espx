/**
 * @file wifi_handler.c
 * @brief /api/wifi endpoints — Wi-Fi scan and credential save
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "str_utils.h"
#include "node_config.h"

static const char *TAG = "http_wifi";

/**
 * @brief Helper: convert auth mode to string
 */
static const char* wifi_auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN:            return "Open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        default:                        return "Unknown";
    }
}

/**
 * @brief POST /api/wifi/scan - scan for Wi-Fi networks
 */
static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return api_send_error(req, "Scan failed", 500);
    }

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));

    if (ap_num == 0) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddArrayToObject(json, "networks");
        return api_send_json(req, json, 200);
    }

    if (ap_num > 32) ap_num = 32;
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (ap_info == NULL) {
        return api_send_error(req, "Out of memory", 500);
    }

    uint16_t count = ap_num;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, ap_info));

    cJSON *json = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(json, "networks");

    for (int i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (const char *)ap_info[i].ssid);
        char bssid[18];
        mac_to_str((const uint8_t *)&ap_info[i].bssid, bssid, sizeof(bssid));
        cJSON_AddStringToObject(net, "bssid", bssid);
        cJSON_AddNumberToObject(net, "rssi", ap_info[i].rssi);
        cJSON_AddStringToObject(net, "auth", wifi_auth_mode_str(ap_info[i].authmode));
        cJSON_AddItemToArray(networks, net);
    }

    free(ap_info);
    return api_send_json(req, json, 200);
}

/**
 * @brief PUT /api/wifi/config - save Wi-Fi credentials and reboot
 */
static esp_err_t api_wifi_config_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return api_send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *incoming = cJSON_Parse(buf);
    if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

    cJSON *ssid = cJSON_GetObjectItem(incoming, "ssid");
    cJSON *password = cJSON_GetObjectItem(incoming, "password");

    if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(incoming);
        return api_send_error(req, "SSID required", 400);
    }

    /* node_config is the AUTHORITATIVE source for Wi-Fi credentials: on boot
     * wifi_prov reads network.wifi_ssid / wifi_password from it and calls
     * esp_wifi_set_config(). Writing any other NVS namespace would be ignored. */
    cJSON *cfg = node_config_get();
    if (cfg == NULL) cfg = cJSON_CreateObject();

    cJSON *net = cJSON_GetObjectItem(cfg, "network");
    if (!cJSON_IsObject(net)) {
        net = cJSON_AddObjectToObject(cfg, "network");
    }

    json_set_string(net, "wifi_ssid", ssid->valuestring);
    if (cJSON_IsString(password)) {
        json_set_string(net, "wifi_password", password->valuestring);
    } else {
        json_set_string(net, "wifi_password", NULL);
    }

    char saved_ssid[64];
    snprintf(saved_ssid, sizeof(saved_ssid), "%s", ssid->valuestring);

    esp_err_t err = node_config_set(cfg);
    cJSON_Delete(cfg);
    cJSON_Delete(incoming);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi config saved: %s", saved_ssid);
    } else {
        ESP_LOGE(TAG, "Failed to save Wi-Fi config: %s", esp_err_to_name(err));
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", err == ESP_OK);
    esp_err_t resp_err = api_send_json(req, resp, err == ESP_OK ? 200 : 500);

    if (err == ESP_OK) {
        // Wait long enough for the HTTP response to flush, then reboot
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    return resp_err;
}

esp_err_t wifi_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/wifi/scan",   .method = HTTP_POST, .handler = api_wifi_scan_handler },
        { .uri = "/api/wifi/config", .method = HTTP_PUT,  .handler = api_wifi_config_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
