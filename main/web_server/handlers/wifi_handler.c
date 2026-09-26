/**
 * @file wifi_handler.c
 * @brief Wi-Fi API handlers implementation
 */

#include <stdio.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif_types.h>
#include <esp_log.h>
#include <cJSON.h>

#include "web_server.h"
#include "wifi_handler.h"

static const char *TAG = "wifi_handler";

esp_err_t wifi_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddStringToObject(root, "status", "connected");
        cJSON_AddStringToObject(root, "ssid", (char *)ap_info.ssid);
        cJSON_AddNumberToObject(root, "rssi", ap_info.rssi);
        cJSON_AddStringToObject(root, "authmode",
            ap_info.authmode == WIFI_AUTH_OPEN ? "OPEN" :
            ap_info.authmode == WIFI_AUTH_WEP ? "WEP" :
            ap_info.authmode == WIFI_AUTH_WPA_PSK ? "WPA-PSK" :
            ap_info.authmode == WIFI_AUTH_WPA2_PSK ? "WPA2-PSK" :
            ap_info.authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA-WPA2-PSK" :
            ap_info.authmode == WIFI_AUTH_WPA2_ENTERPRISE ? "WPA2-ENTERPRISE" :
            "UNKNOWN");
    } else {
        cJSON_AddStringToObject(root, "status", "disconnected");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return web_server_send_error(req, "Failed to generate JSON", 500);
    }

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char content[512];
    size_t content_len = MIN(req->content_len, sizeof(content) - 1);

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        return web_server_send_error(req, "Failed to read request body", 400);
    }
    content[content_len] = '\0';

    ESP_LOGI(TAG, "Wi-Fi connect request: %s", content);

    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        return web_server_send_error(req, "Invalid JSON", 400);
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(root);
        return web_server_send_error(req, "Missing or invalid SSID", 400);
    }

    // Configure and connect
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid->valuestring, sizeof(wifi_config.sta.ssid) - 1);

    if (password != NULL && cJSON_IsString(password) && strlen(password->valuestring) > 0) {
        strncpy((char *)wifi_config.sta.password, password->valuestring, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    cJSON_Delete(root);

    // Return success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "connecting");
    cJSON_AddStringToObject(response, "message", "Wi-Fi connection initiated");
    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    esp_err_t err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}

esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Starting Wi-Fi scan...");

    // Start scan
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        return web_server_send_error(req, "Failed to start scan", 500);
    }

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count > 50) ap_count = 50;  // Limit to 50

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list == NULL) {
        return web_server_send_error(req, "Out of memory", 500);
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (err != ESP_OK) {
        free(ap_list);
        return web_server_send_error(req, "Failed to get scan results", 500);
    }

    // Build JSON response
    cJSON *root = cJSON_CreateArray();

    for (uint16_t i = 0; i < ap_count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)ap_list[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_list[i].rssi);
        cJSON_AddStringToObject(ap, "authmode",
            ap_list[i].authmode == WIFI_AUTH_OPEN ? "OPEN" :
            ap_list[i].authmode == WIFI_AUTH_WEP ? "WEP" :
            ap_list[i].authmode == WIFI_AUTH_WPA_PSK ? "WPA-PSK" :
            ap_list[i].authmode == WIFI_AUTH_WPA2_PSK ? "WPA2-PSK" :
            ap_list[i].authmode == WIFI_AUTH_WPA_WPA2_PSK ? "WPA-WPA2-PSK" :
            ap_list[i].authmode == WIFI_AUTH_WPA2_ENTERPRISE ? "WPA2-ENTERPRISE" :
            "UNKNOWN");
        cJSON_AddItemToArray(root, ap);
    }

    free(ap_list);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return web_server_send_error(req, "Failed to generate JSON", 500);
    }

    err = web_server_send_json(req, json_str, 200);
    free(json_str);

    return err;
}
