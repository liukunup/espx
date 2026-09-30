/**
 * @file esp_now_service.c
 * @brief ESP-NOW peer-to-peer communication service implementation
 *
 * Uses espressif/esp-now component for advanced features.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <espnow.h>
#include <cJSON.h>

#include "esp_now_service.h"
#include "app_info.h"

/* Avoid conflict with espnow component functions */
#define ESPNOW_API(x) espnow_ ## x

static const char *TAG = "espx_espnow";

/* Version from espnow component */
#define ESPX_ESPNOW_VERSION_MAJOR 2
#define ESPX_ESPNOW_VERSION_MINOR 5
#define ESPX_ESPNOW_VERSION_PATCH 3

static bool s_initialized = false;
static bool s_running = false;
static uint8_t s_local_mac[6] = {0};
// Sequence number for deduplication (unused but reserved)
// static uint8_t s_seq = 0;

/* Peer management */
static espx_espnow_peer_info_t s_peers[ESPX_ESPNOW_MAX_PEERS] = {0};
static int s_peer_count = 0;

/* Group management */
static espx_espnow_group_info_t s_groups[ESPX_ESPNOW_MAX_GROUPS] = {0};
static int s_group_count = 0;

/* Configuration */
static bool s_security_enabled = false;
static bool s_forward_enabled = true;

/* Callbacks */
static espx_espnow_recv_callback_t s_recv_cb = NULL;

/* Default PMK */
// Default PMK is defined inline in espx_espnow_init()

/* Parse MAC address string to bytes */
static int parse_mac(const char *str, uint8_t *mac)
{
    if (sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5]) != 6) {
        return -1;
    }
    return 0;
}

/* Convert MAC bytes to uppercase hex string */
static void mac_to_str(const uint8_t *mac, char *buf, size_t len)
{
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Forward declarations */
static esp_err_t data_handler(uint8_t *src_addr, void *data,
                               size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);

const char* espx_espnow_version(void)
{
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             ESPX_ESPNOW_VERSION_MAJOR, ESPX_ESPNOW_VERSION_MINOR, ESPX_ESPNOW_VERSION_PATCH);
    return version;
}

esp_err_t espx_espnow_init(bool enable_security, bool enable_forward)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Get local MAC address */
    ESP_ERROR_CHECK(esp_read_mac(s_local_mac, ESP_MAC_WIFI_STA));

    /* Store configuration */
    s_security_enabled = enable_security;
    s_forward_enabled = enable_forward;

    /* Configure ESP-NOW - use default PMK, configure other options */
    espnow_config_t config = ESPNOW_INIT_CONFIG_DEFAULT();
    config.forward_enable = enable_forward;
    config.sec_enable = enable_security;
    config.qsize = 32;
    config.send_retry_num = 10;
    config.send_max_timeout = pdMS_TO_TICKS(3000);

    /* Enable receive types */
    config.receive_enable.data = 1;
    config.receive_enable.ack = 1;
    config.receive_enable.forward = enable_forward;
    config.receive_enable.group = 1;
    config.receive_enable.sec = enable_security;
    config.receive_enable.sec_data = enable_security;

    /* Initialize espnow component */
    esp_err_t err = ESPNOW_API(init)(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espnow_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Set data handler */
    err = ESPNOW_API(set_config_for_data_type)(ESPNOW_DATA_TYPE_DATA, true, data_handler);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_config_for_data_type failed: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW initialized, MAC: " MACSTR, MAC2STR(s_local_mac));
    ESP_LOGI(TAG, "  Security: %s, Forward: %s",
             s_security_enabled ? "enabled" : "disabled",
             s_forward_enabled ? "enabled" : "disabled");

    return ESP_OK;
}

esp_err_t espx_espnow_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        return ESP_OK;
    }

    /* Ensure Wi-Fi is in station mode */
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        if (mode == WIFI_MODE_AP) {
            ESP_LOGW(TAG, "Wi-Fi in AP-only mode, ESP-NOW may not work properly");
        }
    }

    s_running = true;
    ESP_LOGI(TAG, "ESP-NOW service started");

    return ESP_OK;
}

esp_err_t espx_espnow_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;
    ESP_LOGI(TAG, "ESP-NOW service stopped");

    return ESP_OK;
}

esp_err_t espx_espnow_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    /* Remove all peers */
    for (int i = 0; i < s_peer_count; i++) {
        if (s_peers[i].paired) {
            ESPNOW_API(del_peer)(s_peers[i].mac);
            s_peers[i].paired = false;
        }
    }
    s_peer_count = 0;

    /* Remove all groups */
    for (int i = 0; i < s_group_count; i++) {
        if (s_groups[i].active) {
            ESPNOW_API(del_group)(s_groups[i].id);
            s_groups[i].active = false;
        }
    }
    s_group_count = 0;

    /* Deinitialize */
    esp_err_t err = ESPNOW_API(deinit)();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_deinit failed: %s", esp_err_to_name(err));
    }

    s_initialized = false;
    s_running = false;

    ESP_LOGI(TAG, "ESP-NOW deinitialized");

    return ESP_OK;
}

esp_err_t espx_espnow_add_peer(const uint8_t *mac, const uint8_t *lmk)
{
    if (s_peer_count >= ESPX_ESPNOW_MAX_PEERS) {
        ESP_LOGE(TAG, "Max peers reached (%d)", ESPX_ESPNOW_MAX_PEERS);
        return ESP_ERR_NO_MEM;
    }

    /* Check if peer already exists */
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0) {
            ESP_LOGW(TAG, "Peer already exists: " MACSTR, MAC2STR(mac));
            return ESP_OK;
        }
    }

    /* Add peer using espnow component */
    esp_err_t err = ESPNOW_API(add_peer)(mac, lmk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espnow_add_peer failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Store peer info */
    memcpy(s_peers[s_peer_count].mac, mac, 6);
    s_peers[s_peer_count].paired = true;
    s_peers[s_peer_count].last_seen_ms = 0;
    s_peers[s_peer_count].rssi = 0;
    s_peer_count++;

    char mac_str[32];
    mac_to_str(mac, mac_str, sizeof(mac_str));
    ESP_LOGI(TAG, "Peer added: %s (total: %d)", mac_str, s_peer_count);

    return ESP_OK;
}

esp_err_t espx_espnow_remove_peer(const uint8_t *mac)
{
    esp_err_t err = ESPNOW_API(del_peer)(mac);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_del_peer failed: %s", esp_err_to_name(err));
    }

    /* Remove from local list */
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0) {
            /* Shift remaining peers */
            for (int j = i; j < s_peer_count - 1; j++) {
                s_peers[j] = s_peers[j + 1];
            }
            s_peer_count--;
            break;
        }
    }

    return ESP_OK;
}

esp_err_t espx_espnow_add_group(const uint8_t *group_id)
{
    if (s_group_count >= ESPX_ESPNOW_MAX_GROUPS) {
        ESP_LOGE(TAG, "Max groups reached (%d)", ESPX_ESPNOW_MAX_GROUPS);
        return ESP_ERR_NO_MEM;
    }

    /* Check if group already exists */
    for (int i = 0; i < s_group_count; i++) {
        if (memcmp(s_groups[i].id, group_id, 6) == 0) {
            ESP_LOGW(TAG, "Group already exists");
            return ESP_OK;
        }
    }

    /* Add group */
    esp_err_t err = ESPNOW_API(add_group)(group_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espnow_add_group failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Store group info */
    memcpy(s_groups[s_group_count].id, group_id, 6);
    s_groups[s_group_count].active = true;
    s_groups[s_group_count].name[0] = '\0';
    s_group_count++;

    char group_str[32];
    mac_to_str(group_id, group_str, sizeof(group_str));
    ESP_LOGI(TAG, "Group added: %s (total: %d)", group_str, s_group_count);

    return ESP_OK;
}

esp_err_t espx_espnow_remove_group(const uint8_t *group_id)
{
    esp_err_t err = ESPNOW_API(del_group)(group_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_del_group failed: %s", esp_err_to_name(err));
    }

    /* Remove from local list */
    for (int i = 0; i < s_group_count; i++) {
        if (memcmp(s_groups[i].id, group_id, 6) == 0) {
            for (int j = i; j < s_group_count - 1; j++) {
                s_groups[j] = s_groups[j + 1];
            }
            s_group_count--;
            break;
        }
    }

    return ESP_OK;
}

esp_err_t espx_espnow_send(const uint8_t *mac, espx_espnow_data_type_t type,
                           const uint8_t *data, size_t len, uint32_t wait_ticks)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len > 200) {
        ESP_LOGE(TAG, "Payload too large: %zu (max 200)", len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Broadcast address if mac is NULL */
    static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t *dest = mac ? mac : broadcast_addr;

    /* Frame configuration */
    espnow_frame_head_t frame = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame.broadcast = (mac == NULL);
    frame.ack = true;
    frame.retransmit_count = 10;

    /* Send using espnow component */
    esp_err_t err = ESPNOW_API(send)(ESPNOW_DATA_TYPE_DATA, dest, data, len, &frame, wait_ticks);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_send failed: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t espx_espnow_broadcast(espx_espnow_data_type_t type,
                                const uint8_t *data, size_t len)
{
    return espx_espnow_send(NULL, type, data, len, pdMS_TO_TICKS(1000));
}

esp_err_t espx_espnow_send_group(const uint8_t *group_id, espx_espnow_data_type_t type,
                                  const uint8_t *data, size_t len)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_group_count == 0) {
        ESP_LOGW(TAG, "No groups configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* Use first group if none specified */
    (void)group_id;  /* Reserved for future use */
    (void)s_groups;  /* Reserved for future use */

    /* Frame configuration for group */
    espnow_frame_head_t frame = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame.group = true;
    frame.ack = true;
    frame.retransmit_count = 5;

    /* Broadcast address for group send */
    static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    esp_err_t err = ESPNOW_API(send)(ESPNOW_DATA_TYPE_DATA, broadcast_addr, data, len, &frame, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_send (group) failed: %s", esp_err_to_name(err));
    }

    return err;
}

void espx_espnow_get_local_mac(uint8_t *mac)
{
    memcpy(mac, s_local_mac, 6);
}

bool espx_espnow_is_running(void)
{
    return s_running;
}

int espx_espnow_get_peer_count(void)
{
    return s_peer_count;
}

int espx_espnow_get_group_count(void)
{
    return s_group_count;
}

const espx_espnow_peer_info_t* espx_espnow_get_peer(int index)
{
    if (index < 0 || index >= s_peer_count) {
        return NULL;
    }
    return &s_peers[index];
}

const espx_espnow_group_info_t* espx_espnow_get_group(int index)
{
    if (index < 0 || index >= s_group_count) {
        return NULL;
    }
    return &s_groups[index];
}

void espx_espnow_set_recv_callback(espx_espnow_recv_callback_t callback)
{
    s_recv_cb = callback;
}

/* Data handler callback from espnow component */
static esp_err_t data_handler(uint8_t *src_addr, void *data,
                               size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    if (size <= 0 || data == NULL) {
        return ESP_OK;
    }

    int8_t rssi = rx_ctrl ? rx_ctrl->rssi : 0;

    ESP_LOGD(TAG, "ESP-NOW recv from " MACSTR " len=%d rssi=%d",
            MAC2STR(src_addr), size, rssi);

    /* Call user callback if registered */
    if (s_recv_cb) {
        s_recv_cb(src_addr, 0, data, size, rssi);
    }

    /* Update peer last_seen */
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, src_addr, 6) == 0) {
            s_peers[i].last_seen_ms = esp_timer_get_time() / 1000;
            s_peers[i].rssi = rssi;
            break;
        }
    }

    return ESP_OK;
}

esp_err_t espx_espnow_configure(const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if enabled */
    cJSON *enabled = cJSON_GetObjectItem(config, "enabled");
    if (cJSON_IsBool(enabled) && !cJSON_IsTrue(enabled)) {
        return espx_espnow_stop();
    }

    /* Security and forward options */
    bool security = s_security_enabled;
    bool forward = s_forward_enabled;

    cJSON *sec_json = cJSON_GetObjectItem(config, "security");
    if (cJSON_IsBool(sec_json)) {
        security = cJSON_IsTrue(sec_json);
    }

    cJSON *fwd_json = cJSON_GetObjectItem(config, "forward");
    if (cJSON_IsBool(fwd_json)) {
        forward = cJSON_IsTrue(fwd_json);
    }

    /* Initialize if not already */
    if (!s_initialized) {
        ESP_ERROR_CHECK(espx_espnow_init(security, forward));
    }

    /* Add groups */
    cJSON *groups = cJSON_GetObjectItem(config, "groups");
    if (cJSON_IsArray(groups)) {
        cJSON *group;
        cJSON_ArrayForEach(group, groups) {
            cJSON *id_json = cJSON_GetObjectItem(group, "id");
            if (cJSON_IsString(id_json)) {
                uint8_t gid[6];
                if (parse_mac(id_json->valuestring, gid) == 0) {
                    ESP_ERROR_CHECK(espx_espnow_add_group(gid));

                    /* Store name if provided */
                    cJSON *name_json = cJSON_GetObjectItem(group, "name");
                    if (cJSON_IsString(name_json) && s_group_count > 0) {
                        strncpy(s_groups[s_group_count - 1].name, name_json->valuestring, 31);
                    }
                }
            }
        }
    }

    /* Add peers */
    cJSON *peers = cJSON_GetObjectItem(config, "peers");
    if (cJSON_IsArray(peers)) {
        cJSON *peer;
        cJSON_ArrayForEach(peer, peers) {
            cJSON *mac_json = cJSON_GetObjectItem(peer, "mac");
            cJSON *id_json = cJSON_GetObjectItem(peer, "id");
            cJSON *key_json = cJSON_GetObjectItem(peer, "key");

            if (cJSON_IsString(mac_json)) {
                uint8_t mac[6];
                if (parse_mac(mac_json->valuestring, mac) == 0) {
                    uint8_t *peer_key = NULL;
                    uint8_t key_buf[16] = {0};

                    if (cJSON_IsString(key_json) && strlen(key_json->valuestring) >= 32) {
                        peer_key = key_buf;
                        const char *key_str = key_json->valuestring;
                        for (int i = 0; i < 16; i++) {
                            unsigned int val;
                            if (sscanf(&key_str[i * 2], "%02x", &val) == 1) {
                                key_buf[i] = (uint8_t)val;
                            }
                        }
                    }

                    /* Store peer ID if provided */
                    if (s_peer_count < ESPX_ESPNOW_MAX_PEERS && cJSON_IsString(id_json)) {
                        strncpy(s_peers[s_peer_count].id, id_json->valuestring, 31);
                    }

                    ESP_ERROR_CHECK(espx_espnow_add_peer(mac, peer_key));
                }
            }
        }
    }

    /* Start service */
    if (!s_running) {
        return espx_espnow_start();
    }

    return ESP_OK;
}

cJSON* espx_espnow_config_export(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "enabled", s_running);
    cJSON_AddBoolToObject(root, "security", s_security_enabled);
    cJSON_AddBoolToObject(root, "forward", s_forward_enabled);
    cJSON_AddNumberToObject(root, "peer_count", s_peer_count);
    cJSON_AddNumberToObject(root, "group_count", s_group_count);

    /* Export version */
    cJSON_AddStringToObject(root, "version", espx_espnow_version());

    /* Export local MAC */
    char mac_str[32];
    mac_to_str(s_local_mac, mac_str, sizeof(mac_str));
    cJSON_AddStringToObject(root, "mac", mac_str);

    /* Export peers */
    cJSON *peers = cJSON_CreateArray();
    for (int i = 0; i < s_peer_count; i++) {
        cJSON *peer = cJSON_CreateObject();
        if (s_peers[i].id[0]) {
            cJSON_AddStringToObject(peer, "id", s_peers[i].id);
        }
        char peer_mac[32];
        mac_to_str(s_peers[i].mac, peer_mac, sizeof(peer_mac));
        cJSON_AddStringToObject(peer, "mac", peer_mac);
        cJSON_AddBoolToObject(peer, "paired", s_peers[i].paired);
        cJSON_AddNumberToObject(peer, "last_seen_ms", s_peers[i].last_seen_ms);
        cJSON_AddNumberToObject(peer, "rssi", s_peers[i].rssi);
        cJSON_AddItemToArray(peers, peer);
    }
    cJSON_AddItemToObject(root, "peers", peers);

    /* Export groups */
    cJSON *groups = cJSON_CreateArray();
    for (int i = 0; i < s_group_count; i++) {
        cJSON *group = cJSON_CreateObject();
        char group_id[32];
        mac_to_str(s_groups[i].id, group_id, sizeof(group_id));
        cJSON_AddStringToObject(group, "id", group_id);
        if (s_groups[i].name[0]) {
            cJSON_AddStringToObject(group, "name", s_groups[i].name);
        }
        cJSON_AddBoolToObject(group, "active", s_groups[i].active);
        cJSON_AddItemToArray(groups, group);
    }
    cJSON_AddItemToObject(root, "groups", groups);

    return root;
}
