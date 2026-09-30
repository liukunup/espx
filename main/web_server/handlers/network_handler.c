/**
 * @file network_handler.c
 * @brief GET/PUT /api/network — MQTT / network settings
 */

#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "str_utils.h"
#include "node_config.h"
#include "esp_now_service.h"

static const char *TAG = "http_network";

static esp_err_t api_network_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        cJSON *cfg = node_config_get();
        cJSON *net = cfg ? cJSON_GetObjectItem(cfg, "network") : NULL;
        cJSON *out = net ? cJSON_Duplicate(net, true) : cJSON_CreateObject();
        if (cfg) cJSON_Delete(cfg);
        return api_send_json(req, out, 200);
    }

    if (req->method == HTTP_PUT) {
        char buf[512];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *net = cJSON_GetObjectItem(cfg, "network");
        if (!cJSON_IsObject(net)) {
            net = cJSON_AddObjectToObject(cfg, "network");
        }

        /* Walk the incoming members: strings, numbers and bools are all
         * accepted (the network section holds ports and flags too). */
        cJSON *item = incoming->child;
        while (item != NULL) {
            if (cJSON_IsString(item)) {
                json_set_string(net, item->string, item->valuestring);
            } else if (cJSON_IsNumber(item) || cJSON_IsBool(item)) {
                cJSON *dup = cJSON_Duplicate(item, true);
                cJSON_DeleteItemFromObject(net, item->string);
                cJSON_AddItemToObject(net, item->string, dup);
            }
            /* Never log secrets */
            if (str_contains(item->string, "password")) {
                ESP_LOGI(TAG, "Network config: %s = <set>", item->string);
            } else {
                ESP_LOGI(TAG, "Network config: %s = %s", item->string,
                         cJSON_IsString(item) ? item->valuestring : "<non-string>");
            }
            item = item->next;
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return api_send_error(req, "Method not allowed", 400);
}

static esp_err_t api_esp_now_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Export ESP-NOW status, peers, and discovered devices */
        cJSON *status = espx_espnow_config_export();
        return api_send_json(req, status, 200);
    }

    if (req->method == HTTP_POST) {
        /* Configure ESP-NOW or trigger discovery */
        char buf[2048];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *config = cJSON_Parse(buf);
        if (config == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *resp = cJSON_CreateObject();
        esp_err_t err = ESP_OK;

        /* Check for special actions */
        cJSON *action = cJSON_GetObjectItem(config, "action");
        if (cJSON_IsString(action)) {
            if (strcmp(action->valuestring, "discover") == 0) {
                /* Trigger discovery scan */
                err = espx_espnow_discover();
                cJSON_AddStringToObject(resp, "action", "discover");
            } else if (strcmp(action->valuestring, "clear_discovered") == 0) {
                espx_espnow_clear_discovered();
                cJSON_AddStringToObject(resp, "action", "clear_discovered");
            } else if (strcmp(action->valuestring, "pair") == 0) {
                /* Pair with a discovered device */
                cJSON *mac_json = cJSON_GetObjectItem(config, "mac");
                if (cJSON_IsString(mac_json)) {
                    uint8_t mac[6];
                    if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
                        err = espx_espnow_pair_discovered(mac);
                        cJSON_AddStringToObject(resp, "action", "pair");
                        cJSON_AddStringToObject(resp, "mac", mac_json->valuestring);
                    } else {
                        err = ESP_ERR_INVALID_ARG;
                        cJSON_AddStringToObject(resp, "error", "Invalid MAC format");
                    }
                } else {
                    err = ESP_ERR_INVALID_ARG;
                    cJSON_AddStringToObject(resp, "error", "Missing MAC address");
                }
            } else if (strcmp(action->valuestring, "ota_scan") == 0) {
                /* Scan for OTA responders */
                cJSON *timeout_json = cJSON_GetObjectItem(config, "timeout_ms");
                uint32_t timeout_ms = timeout_json ? cJSON_GetNumberValue(timeout_json) : 30000;
                
                espnow_ota_responder_t *info_list = NULL;
                size_t num = 0;
                err = espx_espnow_ota_scan(pdMS_TO_TICKS(timeout_ms), &info_list, &num);
                
                cJSON_AddStringToObject(resp, "action", "ota_scan");
                cJSON_AddNumberToObject(resp, "found", num);
                
                if (num > 0) {
                    cJSON *devices = cJSON_CreateArray();
                    for (size_t i = 0; i < num; i++) {
                        cJSON *dev = cJSON_CreateObject();
                        char mac_str[32];
                        mac_to_str(info_list[i].mac, mac_str, sizeof(mac_str));
                        cJSON_AddStringToObject(dev, "mac", mac_str);
                        cJSON_AddNumberToObject(dev, "rssi", info_list[i].rssi);
                        cJSON_AddNumberToObject(dev, "channel", info_list[i].channel);
                        cJSON_AddStringToObject(dev, "version", info_list[i].app_desc.version);
                        cJSON_AddItemToArray(devices, dev);
                    }
                    cJSON_AddItemToObject(resp, "devices", devices);
                    espx_espnow_ota_scan_result_free();
                }
            } else if (strcmp(action->valuestring, "ota_send") == 0) {
                /* Send OTA to target */
                cJSON *mac_json = cJSON_GetObjectItem(config, "mac");
                if (cJSON_IsString(mac_json)) {
                    uint8_t mac[6];
                    if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
                        /* TODO: Implement OTA send with HTTP URL or local firmware */
                        cJSON_AddStringToObject(resp, "action", "ota_send");
                        cJSON_AddStringToObject(resp, "status", "not_implemented");
                        cJSON_AddStringToObject(resp, "hint", "Use espnow_ota_initiator_send() with HTTP firmware");
                    } else {
                        err = ESP_ERR_INVALID_ARG;
                        cJSON_AddStringToObject(resp, "error", "Invalid MAC format");
                    }
                } else {
                    err = ESP_ERR_INVALID_ARG;
                    cJSON_AddStringToObject(resp, "error", "Missing MAC address");
                }
            } else if (strcmp(action->valuestring, "ota_init") == 0) {
                /* Initialize OTA responder */
                cJSON *skip_check = cJSON_GetObjectItem(config, "skip_version_check");
                cJSON *interval = cJSON_GetObjectItem(config, "progress_interval");
                err = espx_espnow_ota_init(
                    skip_check ? cJSON_IsTrue(skip_check) : false,
                    interval ? (uint8_t)cJSON_GetNumberValue(interval) : 10
                );
                cJSON_AddStringToObject(resp, "action", "ota_init");
            } else if (strcmp(action->valuestring, "prov_start") == 0) {
                /* Start provisioning responder */
                cJSON *ssid_json = cJSON_GetObjectItem(config, "ssid");
                cJSON *pass_json = cJSON_GetObjectItem(config, "password");
                cJSON *duration_json = cJSON_GetObjectItem(config, "duration_s");
                
                if (cJSON_IsString(ssid_json)) {
                    uint32_t duration = duration_json ? (uint32_t)cJSON_GetNumberValue(duration_json) : 30;
                    err = espx_espnow_prov_start_responder(
                        "espx", "ESPX-Provisioner",
                        ssid_json->valuestring,
                        cJSON_IsString(pass_json) ? pass_json->valuestring : NULL,
                        duration
                    );
                    cJSON_AddStringToObject(resp, "action", "prov_start");
                } else {
                    err = ESP_ERR_INVALID_ARG;
                    cJSON_AddStringToObject(resp, "error", "Missing SSID");
                }
            } else if (strcmp(action->valuestring, "prov_stop") == 0) {
                espx_espnow_prov_stop();
                cJSON_AddStringToObject(resp, "action", "prov_stop");
            } else {
                err = ESP_ERR_INVALID_ARG;
                cJSON_AddStringToObject(resp, "error", "Unknown action");
            }
        } else {
            /* Configure ESP-NOW peers/groups */
            err = espx_espnow_configure(config);
        }

        cJSON_Delete(config);

        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        if (err != ESP_OK && !cJSON_HasObjectItem(resp, "error")) {
            cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
        }
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return api_send_error(req, "Method not allowed", 400);
}

esp_err_t network_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/network", .method = HTTP_GET, .handler = api_network_handler },
        { .uri = "/api/network", .method = HTTP_PUT, .handler = api_network_handler },
        { .uri = "/api/esp_now", .method = HTTP_GET, .handler = api_esp_now_handler },
        { .uri = "/api/esp_now", .method = HTTP_POST, .handler = api_esp_now_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
