/**
 * @file esp_now_service.c
 * @brief ESP-NOW peer-to-peer communication service implementation
 *
 * Uses espressif/esp-now component (v2.5.3) for:
 * - Peer discovery and management
 * - ACK and retransmission
 * - Forwarding and mesh-like capabilities
 * - Groups support
 * - Security encryption
 * - OTA updates (via espnow_ota)
 * - Provisioning (via espnow_prov)
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
#include <espnow_ota.h>
#include <espnow_prov.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
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

/* Peer management */
static espx_espnow_peer_info_t s_peers[ESPX_ESPNOW_MAX_PEERS] = {0};
static int s_peer_count = 0;

/* Group management */
static espx_espnow_group_info_t s_groups[ESPX_ESPNOW_MAX_GROUPS] = {0};
static int s_group_count = 0;

/* Discovery management */
static espx_espnow_discovered_t s_discovered[ESPX_ESPNOW_MAX_DISCOVERED] = {0};
static int s_discovered_count = 0;

/* Configuration */
static bool s_security_enabled = false;
static bool s_forward_enabled = true;

/* Callbacks */
static espx_espnow_recv_callback_t s_recv_cb = NULL;
static espx_espnow_peer_discovered_cb_t s_peer_discovered_cb = NULL;

/* Periodic announce timer handle */
static esp_timer_handle_t s_announce_timer = NULL;

/* Device info for announcements */
static char s_device_id[32] = {0};
static char s_device_name[32] = {0};
static char s_device_version[16] = {0};

/* Forward declarations */
static esp_err_t data_handler(uint8_t *src_addr, void *data,
                               size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);
static void send_announce(void);
static void announce_timer_callback(void *arg);

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

    /* Get device info from app_info if available */
    #ifdef CONFIG_ESPX_DEVICE_ID
    strncpy(s_device_id, CONFIG_ESPX_DEVICE_ID, sizeof(s_device_id) - 1);
    #else
    snprintf(s_device_id, sizeof(s_device_id), "%02X%02X%02X",
             s_local_mac[3], s_local_mac[4], s_local_mac[5]);
    #endif

    #ifdef CONFIG_ESPX_DEVICE_NAME
    strncpy(s_device_name, CONFIG_ESPX_DEVICE_NAME, sizeof(s_device_name) - 1);
    #else
    snprintf(s_device_name, sizeof(s_device_name), "ESPX-%02X%02X",
             s_local_mac[4], s_local_mac[5]);
    #endif

    #ifdef CONFIG_ESPX_FIRMWARE_VERSION
    strncpy(s_device_version, CONFIG_ESPX_FIRMWARE_VERSION, sizeof(s_device_version) - 1);
    #else
    strncpy(s_device_version, "1.0.0", sizeof(s_device_version) - 1);
    #endif

    ESP_LOGI(TAG, "Device info: id=%s name=%s ver=%s",
             s_device_id, s_device_name, s_device_version);

    /* Configure ESP-NOW */
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

    /* Create periodic announce timer */
    if (s_announce_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = &announce_timer_callback,
            .arg = NULL,
            .name = "espnow_announce",
            .dispatch_method = ESP_TIMER_TASK
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_announce_timer));
    }

    /* Start periodic announce timer */
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_announce_timer,
                                              ESPX_ESPNOW_ANNOUNCE_INTERVAL_MS * 1000));

    s_running = true;
    ESP_LOGI(TAG, "ESP-NOW service started, announcing every %ds",
             ESPX_ESPNOW_ANNOUNCE_INTERVAL_MS / 1000);

    /* Send initial announce */
    send_announce();

    return ESP_OK;
}

esp_err_t espx_espnow_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    /* Stop announce timer */
    if (s_announce_timer) {
        esp_timer_stop(s_announce_timer);
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

    /* Clear discovered list */
    s_discovered_count = 0;
    memset(s_discovered, 0, sizeof(s_discovered));

    /* Delete announce timer */
    if (s_announce_timer) {
        esp_timer_stop(s_announce_timer);
        esp_timer_delete(s_announce_timer);
        s_announce_timer = NULL;
    }

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

    /* Add to espnow */
    esp_err_t err = ESPNOW_API(add_peer)(mac, lmk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(err));
        return err;
    }

    /* Store peer info */
    memset(&s_peers[s_peer_count], 0, sizeof(espx_espnow_peer_info_t));
    memcpy(s_peers[s_peer_count].mac, mac, 6);
    s_peers[s_peer_count].paired = true;
    s_peers[s_peer_count].last_seen_ms = esp_timer_get_time() / 1000;
    s_peer_count++;

    ESP_LOGI(TAG, "Peer added: " MACSTR, MAC2STR(mac));

    return ESP_OK;
}

esp_err_t espx_espnow_remove_peer(const uint8_t *mac)
{
    esp_err_t err = ESPNOW_API(del_peer)(mac);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to remove peer: %s", esp_err_to_name(err));
    }

    /* Remove from local list */
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0) {
            memmove(&s_peers[i], &s_peers[i + 1],
                    (s_peer_count - i - 1) * sizeof(espx_espnow_peer_info_t));
            s_peer_count--;
            ESP_LOGI(TAG, "Peer removed: " MACSTR, MAC2STR(mac));
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
            return ESP_OK;
        }
    }

    espnow_group_t group;
    memcpy(group, group_id, 6);
    esp_err_t err = ESPNOW_API(add_group)(group);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add group: %s", esp_err_to_name(err));
        return err;
    }

    /* Store group info */
    memcpy(s_groups[s_group_count].id, group_id, 6);
    s_groups[s_group_count].active = true;
    s_group_count++;

    char gid_str[32];
    mac_to_str(group_id, gid_str, sizeof(gid_str));
    ESP_LOGI(TAG, "Group added: %s", gid_str);

    return ESP_OK;
}

esp_err_t espx_espnow_remove_group(const uint8_t *group_id)
{
    espnow_group_t group;
    memcpy(group, group_id, 6);
    esp_err_t err = ESPNOW_API(del_group)(group);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to remove group: %s", esp_err_to_name(err));
    }

    /* Remove from local list */
    for (int i = 0; i < s_group_count; i++) {
        if (memcmp(s_groups[i].id, group_id, 6) == 0) {
            memmove(&s_groups[i], &s_groups[i + 1],
                    (s_group_count - i - 1) * sizeof(espx_espnow_group_info_t));
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

    /* Build frame header */
    espnow_frame_head_t frame = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame.ack = true;
    frame.retransmit_count = 5;

    esp_err_t err = ESPNOW_API(send)(ESPNOW_DATA_TYPE_DATA, mac, data, len, &frame, wait_ticks);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "espnow_send failed: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t espx_espnow_broadcast(espx_espnow_data_type_t type,
                                const uint8_t *data, size_t len)
{
    static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return espx_espnow_send(broadcast_addr, type, data, len, pdMS_TO_TICKS(1000));
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
    (void)group_id;

    /* Frame configuration for group */
    espnow_frame_head_t frame = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame.group = true;
    frame.ack = true;
    frame.retransmit_count = 5;

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

void espx_espnow_on_peer_discovered(espx_espnow_peer_discovered_cb_t callback)
{
    s_peer_discovered_cb = callback;
}

/* Find or add discovered device */
static espx_espnow_discovered_t* find_or_add_discovered(const uint8_t *mac)
{
    /* Check if already in discovered list */
    for (int i = 0; i < s_discovered_count; i++) {
        if (memcmp(s_discovered[i].mac, mac, 6) == 0) {
            return &s_discovered[i];
        }
    }

    /* Add new discovered device */
    if (s_discovered_count >= ESPX_ESPNOW_MAX_DISCOVERED) {
        ESP_LOGW(TAG, "Discovered list full, removing oldest");
        memmove(&s_discovered[0], &s_discovered[1],
                (ESPX_ESPNOW_MAX_DISCOVERED - 1) * sizeof(espx_espnow_discovered_t));
        s_discovered_count--;
    }

    memset(&s_discovered[s_discovered_count], 0, sizeof(espx_espnow_discovered_t));
    memcpy(s_discovered[s_discovered_count].mac, mac, 6);
    s_discovered_count++;

    return &s_discovered[s_discovered_count - 1];
}

/* Check if device is already paired */
static bool is_peer_paired(const uint8_t *mac)
{
    for (int i = 0; i < s_peer_count; i++) {
        if (memcmp(s_peers[i].mac, mac, 6) == 0 && s_peers[i].paired) {
            return true;
        }
    }
    return false;
}

/* Process discovery/announce message */
static void process_discovery_msg(uint8_t *src_addr, const uint8_t *payload, size_t len, int8_t rssi)
{
    if (len < 3) return;

    uint8_t msg_type = payload[0];
    uint8_t ver_major = payload[1];
    uint8_t ver_minor = payload[2];

    ESP_LOGD(TAG, "Discovery msg from " MACSTR ": type=%d v%d.%d",
             MAC2STR(src_addr), msg_type, ver_major, ver_minor);

    /* Don't process our own messages */
    if (memcmp(src_addr, s_local_mac, 6) == 0) {
        return;
    }

    /* Add to discovered list */
    espx_espnow_discovered_t *disc = find_or_add_discovered(src_addr);
    disc->last_seen_ms = esp_timer_get_time() / 1000;
    disc->rssi = rssi;
    snprintf(disc->version, sizeof(disc->version), "%d.%d", ver_major, ver_minor);

    /* Parse optional fields if present */
    if (len > 3 && payload[3] != 0) {
        size_t offset = 3;
        size_t id_len = payload[offset];
        if (id_len > 0 && offset + 1 + id_len <= len && id_len < 32) {
            strncpy(disc->device_id, (const char*)&payload[offset + 1], id_len);
            disc->device_id[id_len] = 0;
            offset += 1 + id_len;
        }

        if (offset < len && payload[offset] != 0) {
            size_t name_len = payload[offset++];
            if (name_len > 0 && offset + name_len <= len && name_len < 32) {
                strncpy(disc->name, (const char*)&payload[offset], name_len);
                disc->name[name_len] = 0;
            }
        }
    }

    ESP_LOGI(TAG, "Discovered device: %s (%s) v%s RSSI=%d",
             disc->device_id[0] ? disc->device_id : "unknown",
             disc->name[0] ? disc->name : "no-name",
             disc->version, rssi);

    /* Auto-pair: automatically add to peer list */
    if (!is_peer_paired(src_addr)) {
        espx_espnow_add_peer(src_addr, NULL);
    }

    /* Notify callback */
    if (s_peer_discovered_cb) {
        s_peer_discovered_cb(src_addr, disc->device_id, disc->name, disc->version, rssi);
    }
}

/* Build and send announce message */
static void send_announce(void)
{
    uint8_t payload[64] = {0};
    size_t len = 0;

    payload[len++] = ESPX_ESPNOW_ANNOUNCE_MSG;
    payload[len++] = (uint8_t)atoi(s_device_version);
    payload[len++] = (uint8_t)atoi(strchr(s_device_version, '.') ? strchr(s_device_version, '.') + 1 : "0");

    /* Device ID */
    size_t id_len = strlen(s_device_id);
    if (id_len > 0) {
        payload[len++] = (uint8_t)id_len;
        memcpy(&payload[len], s_device_id, id_len);
        len += id_len;
    } else {
        payload[len++] = 0;
    }

    /* Name */
    size_t name_len = strlen(s_device_name);
    if (name_len > 0) {
        payload[len++] = (uint8_t)name_len;
        memcpy(&payload[len], s_device_name, name_len);
        len += name_len;
    } else {
        payload[len++] = 0;
    }

    esp_err_t err = espx_espnow_broadcast(ESPX_ESPNOW_TYPE_DATA, payload, len);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Announce sent, %d bytes", len);
    }
}

/* Announce timer callback */
static void announce_timer_callback(void *arg)
{
    (void)arg;
    if (s_running) {
        send_announce();
    }
}

/* Data handler callback from espnow component */
static esp_err_t data_handler(uint8_t *src_addr, void *data,
                               size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    if (size <= 0 || data == NULL) {
        return ESP_OK;
    }

    int8_t rssi = rx_ctrl ? rx_ctrl->rssi : 0;
    uint8_t *payload = (uint8_t*)data;

    ESP_LOGD(TAG, "ESP-NOW recv from " MACSTR " len=%d rssi=%d type=%d",
            MAC2STR(src_addr), size, rssi, payload[0]);

    /* Check for discovery messages */
    if (payload[0] == ESPX_ESPNOW_DISCOVER_MSG || payload[0] == ESPX_ESPNOW_ANNOUNCE_MSG) {
        process_discovery_msg(src_addr, payload, size, rssi);
        return ESP_OK;
    }

    /* Call user callback for other messages */
    if (s_recv_cb) {
        s_recv_cb(src_addr, payload[0], payload + 1, size - 1, rssi);
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

/* ========== Discovery API ========== */

esp_err_t espx_espnow_discover(void)
{
    if (!s_running) {
        ESP_LOGW(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    /* Send DISCOVER message */
    uint8_t payload[3] = {
        ESPX_ESPNOW_DISCOVER_MSG,
        (uint8_t)atoi(s_device_version),
        (uint8_t)atoi(strchr(s_device_version, '.') ? strchr(s_device_version, '.') + 1 : "0")
    };

    ESP_LOGI(TAG, "Sending DISCOVER broadcast...");
    return espx_espnow_broadcast(ESPX_ESPNOW_TYPE_DATA, payload, 3);
}

int espx_espnow_get_discovered_count(void)
{
    return s_discovered_count;
}

const espx_espnow_discovered_t* espx_espnow_get_discovered(int index)
{
    if (index < 0 || index >= s_discovered_count) {
        return NULL;
    }
    return &s_discovered[index];
}

esp_err_t espx_espnow_pair_discovered(const uint8_t *mac)
{
    /* Find in discovered list */
    for (int i = 0; i < s_discovered_count; i++) {
        if (memcmp(s_discovered[i].mac, mac, 6) == 0) {
            esp_err_t err = espx_espnow_add_peer(mac, NULL);
            if (err == ESP_OK && s_discovered[i].device_id[0] && s_peer_count > 0) {
                strncpy(s_peers[s_peer_count - 1].id, s_discovered[i].device_id, 31);
            }
            return err;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void espx_espnow_clear_discovered(void)
{
    s_discovered_count = 0;
    memset(s_discovered, 0, sizeof(s_discovered));
}

/* ========== Configuration ========== */

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

    /* Add/Remove groups */
    cJSON *groups = cJSON_GetObjectItem(config, "groups");
    if (cJSON_IsArray(groups)) {
        cJSON *group;
        cJSON_ArrayForEach(group, groups) {
            cJSON *id_json = cJSON_GetObjectItem(group, "id");
            cJSON *action_json = cJSON_GetObjectItem(group, "_action");
            if (cJSON_IsString(id_json)) {
                uint8_t gid[6];
                if (parse_mac(id_json->valuestring, gid) == 0) {
                    if (cJSON_IsString(action_json) && strcmp(action_json->valuestring, "remove") == 0) {
                        espx_espnow_remove_group(gid);
                    } else {
                        ESP_ERROR_CHECK(espx_espnow_add_group(gid));
                        cJSON *name_json = cJSON_GetObjectItem(group, "name");
                        if (cJSON_IsString(name_json) && s_group_count > 0) {
                            strncpy(s_groups[s_group_count - 1].name, name_json->valuestring, 31);
                        }
                    }
                }
            }
        }
    }

    /* Add/Remove peers */
    cJSON *peers = cJSON_GetObjectItem(config, "peers");
    if (cJSON_IsArray(peers)) {
        cJSON *peer;
        cJSON_ArrayForEach(peer, peers) {
            cJSON *mac_json = cJSON_GetObjectItem(peer, "mac");
            cJSON *id_json = cJSON_GetObjectItem(peer, "id");
            cJSON *key_json = cJSON_GetObjectItem(peer, "key");
            cJSON *action_json = cJSON_GetObjectItem(peer, "_action");

            if (cJSON_IsString(mac_json)) {
                uint8_t mac[6];
                if (parse_mac(mac_json->valuestring, mac) == 0) {
                    if (cJSON_IsString(action_json) && strcmp(action_json->valuestring, "remove") == 0) {
                        espx_espnow_remove_peer(mac);
                    } else {
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

                        if (s_peer_count < ESPX_ESPNOW_MAX_PEERS && cJSON_IsString(id_json)) {
                            strncpy(s_peers[s_peer_count].id, id_json->valuestring, 31);
                        }

                        ESP_ERROR_CHECK(espx_espnow_add_peer(mac, peer_key));
                    }
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
    cJSON_AddNumberToObject(root, "discovered_count", s_discovered_count);

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

    /* Export discovered devices */
    cJSON *discovered = cJSON_CreateArray();
    for (int i = 0; i < s_discovered_count; i++) {
        bool already_paired = false;
        for (int j = 0; j < s_peer_count; j++) {
            if (memcmp(s_discovered[i].mac, s_peers[j].mac, 6) == 0) {
                already_paired = true;
                break;
            }
        }
        if (already_paired) continue;

        cJSON *dev = cJSON_CreateObject();
        char disc_mac[32];
        mac_to_str(s_discovered[i].mac, disc_mac, sizeof(disc_mac));
        cJSON_AddStringToObject(dev, "mac", disc_mac);
        if (s_discovered[i].device_id[0]) {
            cJSON_AddStringToObject(dev, "device_id", s_discovered[i].device_id);
        }
        if (s_discovered[i].name[0]) {
            cJSON_AddStringToObject(dev, "name", s_discovered[i].name);
        }
        if (s_discovered[i].version[0]) {
            cJSON_AddStringToObject(dev, "version", s_discovered[i].version);
        }
        cJSON_AddNumberToObject(dev, "rssi", s_discovered[i].rssi);
        cJSON_AddNumberToObject(dev, "last_seen_ms", s_discovered[i].last_seen_ms);
        cJSON_AddItemToArray(discovered, dev);
    }
    cJSON_AddItemToObject(root, "discovered", discovered);

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

/* ========== ESP-NOW OTA Implementation (using espnow_ota) ========== */

static bool s_ota_active = false;

esp_err_t espx_espnow_ota_init(bool skip_version_check, uint8_t progress_interval)
{
    espnow_ota_config_t config = {
        .skip_version_check = skip_version_check,
        .progress_report_interval = progress_interval,
    };

    ESP_LOGI(TAG, "Initializing ESP-NOW OTA responder");
    return espnow_ota_responder_start(&config);
}

esp_err_t espx_espnow_ota_scan(TickType_t timeout_ticks, espnow_ota_responder_t **info_list, size_t *num)
{
    if (!s_running) {
        ESP_LOGE(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    s_ota_active = true;
    ESP_LOGI(TAG, "Scanning for OTA responders...");
    return espnow_ota_initiator_scan(info_list, num, timeout_ticks);
}

esp_err_t espx_espnow_ota_scan_result_free(void)
{
    return espnow_ota_initiator_scan_result_free();
}

esp_err_t espx_espnow_ota_send(const uint8_t *addr, size_t size,
                                espnow_ota_initiator_data_cb_t data_cb,
                                espnow_ota_result_t *result)
{
    if (!s_running) {
        ESP_LOGE(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get SHA-256 of running firmware */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware: %s", app_desc.version);
    }

    /* Get target partition SHA */
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }

    /* Calculate SHA */
    uint8_t sha_256[32] = {0};
    esp_err_t err = esp_partition_get_sha256(update, sha_256);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get partition SHA: %s", esp_err_to_name(err));
        /* Use zeros as fallback */
    }

    ESP_LOGI(TAG, "Sending OTA to " MACSTR ", size=%d", MAC2STR(addr), size);

    s_ota_active = true;
    /* espnow_ota_initiator_send expects espnow_addr_t[6] */
    espnow_addr_t target_addr;
    memcpy(target_addr, addr, 6);
    err = espnow_ota_initiator_send(&target_addr, 1, sha_256, size, data_cb, result);

    if (err != ESP_OK) {
        s_ota_active = false;
    }

    return err;
}

esp_err_t espx_espnow_ota_stop(void)
{
    ESP_LOGI(TAG, "Stopping OTA");
    espnow_ota_initiator_stop();
    s_ota_active = false;
    return ESP_OK;
}

esp_err_t espx_espnow_ota_result_free(espnow_ota_result_t *result)
{
    return espnow_ota_initiator_result_free(result);
}

bool espx_espnow_ota_is_active(void)
{
    return s_ota_active;
}

/* ========== ESP-NOW Provisioning Implementation (using espnow_prov) ========== */

esp_err_t espx_espnow_prov_init(void)
{
    ESP_LOGI(TAG, "ESP-NOW provisioning initialized");
    return ESP_OK;
}

esp_err_t espx_espnow_prov_start_responder(const char *product_id, const char *device_name,
                                            const char *ssid, const char *password,
                                            uint32_t beacon_duration_s)
{
    if (!s_running) {
        ESP_LOGE(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    /* Prepare WiFi config */
    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    /* Prepare responder info */
    espnow_prov_responder_t responder_info = {0};
    strncpy(responder_info.product_id, product_id ? product_id : "espx", sizeof(responder_info.product_id) - 1);
    strncpy(responder_info.device_name, device_name ? device_name : s_device_name, sizeof(responder_info.device_name) - 1);

    /* Prepare WiFi provisioning data */
    espnow_prov_wifi_t wifi_prov = {0};
    wifi_prov.mode = WIFI_MODE_STA;
    memcpy(&wifi_prov.sta, &wifi_config.sta, sizeof(wifi_sta_config_t));
    strncpy(wifi_prov.token, "espx_prov", sizeof(wifi_prov.token) - 1);

    ESP_LOGI(TAG, "Starting ESP-NOW provisioning responder: product=%s, device=%s, ssid=%s",
             product_id, device_name, ssid);

    return espnow_prov_responder_start(&responder_info,
                                        pdMS_TO_TICKS(beacon_duration_s * 1000),
                                        &wifi_prov, NULL);
}

esp_err_t espx_espnow_prov_start_initiator(const char *product_id, const char *device_name,
                                             uint8_t auth_mode, const char *secret,
                                             TickType_t timeout_ticks)
{
    if (!s_running) {
        ESP_LOGE(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    /* Prepare initiator info */
    espnow_prov_initiator_t initiator_info = {0};
    strncpy(initiator_info.product_id, product_id ? product_id : "espx", sizeof(initiator_info.product_id) - 1);
    strncpy(initiator_info.device_name, device_name ? device_name : s_device_name, sizeof(initiator_info.device_name) - 1);
    initiator_info.auth_mode = auth_mode;
    if (secret) {
        strncpy(initiator_info.device_secret, secret, sizeof(initiator_info.device_secret) - 1);
    }

    /* Scan for responder */
    espnow_addr_t responder_addr;
    espnow_prov_responder_t responder_info;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    ESP_LOGI(TAG, "Scanning for provisioning responder: product=%s", product_id);

    esp_err_t err = espnow_prov_initiator_scan(responder_addr, &responder_info, &rx_ctrl, timeout_ticks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan for responder: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Found responder: " MACSTR " product=%s device=%s",
             MAC2STR(responder_addr), responder_info.product_id, responder_info.device_name);

    /* Request WiFi credentials */
    ESP_LOGI(TAG, "Requesting WiFi credentials...");

    err = espnow_prov_initiator_send(responder_addr, &initiator_info, NULL, pdMS_TO_TICKS(10000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send provision request: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi credentials received, connecting...");
    /* Note: The WiFi connection will be handled by the callback */

    return ESP_OK;
}

void espx_espnow_prov_stop(void)
{
    ESP_LOGI(TAG, "Stopping ESP-NOW provisioning");
}

bool espx_espnow_prov_is_active(void)
{
    /* TODO: Track provisioning state */
    return false;
}
