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

/* Discovery management */
static espx_espnow_discovered_t s_discovered[ESPX_ESPNOW_MAX_DISCOVERED] = {0};
static int s_discovered_count = 0;

/* Configuration */
static bool s_security_enabled = false;
static bool s_forward_enabled = true;

/* Callbacks */
static espx_espnow_recv_callback_t s_recv_cb = NULL;

/* Periodic announce timer handle */
static esp_timer_handle_t s_announce_timer = NULL;

/* Device info for announcements */
static char s_device_id[32] = {0};
static char s_device_name[32] = {0};
static char s_device_version[16] = {0};

/* ========== ESP-NOW OTA State ========== */
#define ESPX_OTA_MAGIC         0x45535058  /**< "ESPX" */
#define ESPX_OTA_HEADER_SIZE   32

/** OTA send state */
typedef struct {
    bool active;
    uint8_t target_mac[6];
    const uint8_t *firmware_data;
    size_t firmware_size;
    size_t sent_bytes;
    uint16_t chunk_count;
    char version[16];
    espx_ota_progress_cb_t progress_cb;
    espx_ota_complete_cb_t complete_cb;
} espx_ota_send_t;

/** OTA receive state */
typedef struct {
    bool active;
    uint8_t src_mac[6];
    uint8_t *firmware_data;
    size_t firmware_size;
    size_t received_bytes;
    uint16_t expected_chunks;
    char version[16];
    espx_ota_receive_cb_t progress_cb;
    espx_ota_receive_done_cb_t complete_cb;
} espx_ota_recv_t;

static espx_ota_send_t s_ota_send = {0};
static espx_ota_recv_t s_ota_recv = {0};

/* ========== ESP-NOW Provisioning State ========== */

/** Provisioning send state (for provisioner) */
typedef struct {
    bool listening;
    espx_prov_request_cb_t request_cb;
    espx_prov_complete_cb_t complete_cb;
    uint8_t target_mac[6];
    bool waiting_response;
} espx_prov_listener_t;

/** Provisioning receive state (for new device) */
typedef struct {
    bool waiting_response;
    espx_prov_complete_cb_t complete_cb;
} espx_prov_device_t;

static espx_prov_listener_t s_prov_listener = {0};
static espx_prov_device_t s_prov_device = {0};

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
static void send_announce(void);
static void announce_timer_callback(void *arg);
static void process_ota_message(uint8_t *src_addr, const uint8_t *payload, size_t len, int8_t rssi);
static void process_prov_message(uint8_t *src_addr, const uint8_t *payload, size_t len, int8_t rssi);
static esp_err_t ota_send_chunk(void);

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
    /* Use last 6 chars of MAC as default device_id */
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
        /* Remove oldest (first) and shift */
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
    if (len < 3) return;  /* Need at least msg_type + version[2] */

    uint8_t msg_type = payload[0];
    uint8_t ver_major = payload[1];
    uint8_t ver_minor = payload[2];

    ESP_LOGI(TAG, "Discovery msg from " MACSTR ": type=%d v%d.%d",
             MAC2STR(src_addr), msg_type, ver_major, ver_minor);

    /* Don't process our own messages */
    if (memcmp(src_addr, s_local_mac, 6) == 0) {
        return;
    }

    /* Skip if already paired */
    if (is_peer_paired(src_addr)) {
        /* Update last_seen */
        for (int i = 0; i < s_peer_count; i++) {
            if (memcmp(s_peers[i].mac, src_addr, 6) == 0) {
                s_peers[i].last_seen_ms = esp_timer_get_time() / 1000;
                s_peers[i].rssi = rssi;
                break;
            }
        }
        return;
    }

    /* Add to discovered list */
    espx_espnow_discovered_t *disc = find_or_add_discovered(src_addr);
    disc->last_seen_ms = esp_timer_get_time() / 1000;
    disc->rssi = rssi;
    snprintf(disc->version, sizeof(disc->version), "%d.%d", ver_major, ver_minor);

    /* Parse optional fields if present */
    if (len > 3 && payload[3] != 0) {
        /* device_id follows */
        size_t offset = 3;
        size_t id_len = payload[offset];
        if (id_len > 0 && offset + 1 + id_len <= len) {
            strncpy(disc->device_id, (const char*)&payload[offset + 1],
                    id_len < 31 ? id_len : 31);
            disc->device_id[id_len < 31 ? id_len : 31] = 0;
            offset += 1 + id_len;
        }

        /* name follows */
        if (offset < len && payload[offset] != 0) {
            size_t name_len = payload[offset];
            if (offset + 1 + name_len <= len) {
                strncpy(disc->name, (const char*)&payload[offset + 1],
                        name_len < 31 ? name_len : 31);
                disc->name[name_len < 31 ? name_len : 31] = 0;
            }
        }
    }

    ESP_LOGI(TAG, "Discovered device: %s (%s) RSSI=%d",
             disc->device_id[0] ? disc->device_id : "unknown",
             disc->name[0] ? disc->name : "no-name", rssi);

    /* Auto-pair: automatically add to peer list */
    espx_espnow_add_peer(src_addr, NULL);
}

/* Build and send announce message */
static void send_announce(void)
{
    /* Build announce payload:
     * [0] msg_type = ANNOUNCE_MSG
     * [1] ver_major
     * [2] ver_minor
     * [3] device_id_len + device_id
     * [n] name_len + name
     */
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

    /* Broadcast */
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

    /* Check for provisioning messages */
    if (payload[0] >= ESPX_ESPNOW_PROV_REQUEST && payload[0] <= ESPX_ESPNOW_PROV_STATUS) {
        process_prov_message(src_addr, payload, size, rssi);
        return ESP_OK;
    }

    /* Check for OTA messages */
    if (payload[0] >= ESPX_ESPNOW_OTA_START && payload[0] <= ESPX_ESPNOW_OTA_REQUEST) {
        process_ota_message(src_addr, payload, size, rssi);
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
            /* Add to peer list */
            esp_err_t err = espx_espnow_add_peer(mac, NULL);
            if (err == ESP_OK) {
                /* Copy device_id to peer */
                if (s_discovered[i].device_id[0] && s_peer_count > 0) {
                    strncpy(s_peers[s_peer_count - 1].id, s_discovered[i].device_id, 31);
                }
                ESP_LOGI(TAG, "Paired with discovered device " MACSTR, MAC2STR(mac));
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
                    /* Check for remove action */
                    if (cJSON_IsString(action_json) && strcmp(action_json->valuestring, "remove") == 0) {
                        espx_espnow_remove_group(gid);
                    } else {
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
                    /* Check for remove action */
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

                        /* Store peer ID if provided */
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
        /* Skip if already paired */
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

/* ========== ESP-NOW OTA Implementation ========== */

/* Send single OTA chunk */
static esp_err_t ota_send_chunk(void)
{
    if (!s_ota_send.active || !s_ota_send.firmware_data) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = s_ota_send.sent_bytes;
    size_t remaining = s_ota_send.firmware_size - offset;
    size_t chunk_size = remaining > ESPX_ESPNOW_OTA_CHUNK_SIZE ? ESPX_ESPNOW_OTA_CHUNK_SIZE : remaining;

    /* Build OTA DATA message */
    uint8_t msg[ESPX_ESPNOW_OTA_CHUNK_SIZE + 8];
    size_t msg_len = 0;

    /* Header */
    msg[msg_len++] = ESPX_ESPNOW_OTA_DATA;
    msg[msg_len++] = (s_ota_send.sent_bytes >> 24) & 0xFF;
    msg[msg_len++] = (s_ota_send.sent_bytes >> 16) & 0xFF;
    msg[msg_len++] = (s_ota_send.sent_bytes >> 8) & 0xFF;
    msg[msg_len++] = s_ota_send.sent_bytes & 0xFF;
    msg[msg_len++] = (s_ota_send.firmware_size >> 24) & 0xFF;
    msg[msg_len++] = (s_ota_send.firmware_size >> 16) & 0xFF;
    msg[msg_len++] = (s_ota_send.firmware_size >> 8) & 0xFF;
    msg[msg_len++] = s_ota_send.firmware_size & 0xFF;

    /* Data */
    memcpy(&msg[msg_len], s_ota_send.firmware_data + offset, chunk_size);
    msg_len += chunk_size;

    /* Send to target */
    esp_err_t err = espx_espnow_send(s_ota_send.target_mac, ESPX_ESPNOW_TYPE_OTA,
                                      msg, msg_len, pdMS_TO_TICKS(2000));

    if (err == ESP_OK) {
        s_ota_send.sent_bytes += chunk_size;

        /* Report progress */
        int percent = (s_ota_send.sent_bytes * 100) / s_ota_send.firmware_size;
        ESP_LOGI(TAG, "OTA sent %d/%d bytes (%d%%)",
                  s_ota_send.sent_bytes, s_ota_send.firmware_size, percent);

        if (s_ota_send.progress_cb) {
            s_ota_send.progress_cb(s_ota_send.sent_bytes, s_ota_send.firmware_size, percent);
        }

        /* Check if done */
        if (s_ota_send.sent_bytes >= s_ota_send.firmware_size) {
            /* Send OTA_END */
            uint8_t end_msg[8] = {
                ESPX_ESPNOW_OTA_END,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            };
            espx_espnow_send(s_ota_send.target_mac, ESPX_ESPNOW_TYPE_OTA,
                              end_msg, sizeof(end_msg), pdMS_TO_TICKS(1000));

            ESP_LOGI(TAG, "OTA transfer complete to " MACSTR, MAC2STR(s_ota_send.target_mac));

            /* Cleanup send state */
            s_ota_send.active = false;
            s_ota_send.firmware_data = NULL;

            if (s_ota_send.complete_cb) {
                s_ota_send.complete_cb(true, "OTA complete, waiting for device to reboot");
            }
        } else {
            /* Schedule next chunk */
            // In a real implementation, use a timer or queue to send next chunk
        }
    }

    return err;
}

esp_err_t espx_espnow_ota_start(const uint8_t *peer_mac, const uint8_t *firmware_data,
                                size_t size, const char *version,
                                espx_ota_progress_cb_t progress_cb,
                                espx_ota_complete_cb_t complete_cb)
{
    if (!s_running) {
        ESP_LOGE(TAG, "ESP-NOW not running");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ota_send.active) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (!peer_mac || !firmware_data || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize send state */
    memset(&s_ota_send, 0, sizeof(s_ota_send));
    s_ota_send.active = true;
    memcpy(s_ota_send.target_mac, peer_mac, 6);
    s_ota_send.firmware_data = firmware_data;
    s_ota_send.firmware_size = size;
    s_ota_send.sent_bytes = 0;
    s_ota_send.progress_cb = progress_cb;
    s_ota_send.complete_cb = complete_cb;
    if (version) {
        strncpy(s_ota_send.version, version, sizeof(s_ota_send.version) - 1);
    }

    /* Calculate chunks */
    s_ota_send.chunk_count = (size + ESPX_ESPNOW_OTA_CHUNK_SIZE - 1) / ESPX_ESPNOW_OTA_CHUNK_SIZE;

    /* Send OTA_START */
    uint8_t start_msg[32] = {0};
    start_msg[0] = ESPX_ESPNOW_OTA_START;
    /* Magic */
    start_msg[1] = (ESPX_OTA_MAGIC >> 24) & 0xFF;
    start_msg[2] = (ESPX_OTA_MAGIC >> 16) & 0xFF;
    start_msg[3] = (ESPX_OTA_MAGIC >> 8) & 0xFF;
    start_msg[4] = ESPX_OTA_MAGIC & 0xFF;
    /* Total size */
    start_msg[5] = (size >> 24) & 0xFF;
    start_msg[6] = (size >> 16) & 0xFF;
    start_msg[7] = (size >> 8) & 0xFF;
    start_msg[8] = size & 0xFF;
    /* Chunk count */
    start_msg[9] = (s_ota_send.chunk_count >> 8) & 0xFF;
    start_msg[10] = s_ota_send.chunk_count & 0xFF;
    /* Version length */
    size_t ver_len = version ? strlen(version) : 0;
    if (ver_len > 15) ver_len = 15;
    start_msg[11] = ver_len;
    /* Version string */
    if (version) {
        memcpy(&start_msg[12], version, ver_len);
    }

    ESP_LOGI(TAG, "Starting OTA to " MACSTR ", size=%d, chunks=%d",
             MAC2STR(peer_mac), size, s_ota_send.chunk_count);

    esp_err_t err = espx_espnow_send(peer_mac, ESPX_ESPNOW_TYPE_OTA,
                                      start_msg, 12 + ver_len + 1, pdMS_TO_TICKS(3000));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send OTA_START: %s", esp_err_to_name(err));
        s_ota_send.active = false;
        return err;
    }

    /* Start sending chunks */
    return ota_send_chunk();
}

esp_err_t espx_espnow_ota_request(const uint8_t *peer_mac, const char *current_version,
                                   espx_ota_progress_cb_t progress_cb,
                                   espx_ota_complete_cb_t complete_cb)
{
    if (!s_running || !peer_mac) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build OTA_REQUEST message */
    uint8_t req_msg[32] = {0};
    req_msg[0] = ESPX_ESPNOW_OTA_REQUEST;
    /* Current version length */
    size_t ver_len = current_version ? strlen(current_version) : 0;
    if (ver_len > 20) ver_len = 20;
    req_msg[1] = ver_len;
    /* Current version string */
    if (current_version) {
        memcpy(&req_msg[2], current_version, ver_len);
    }

    ESP_LOGI(TAG, "Requesting OTA from " MACSTR, MAC2STR(peer_mac));

    return espx_espnow_send(peer_mac, ESPX_ESPNOW_TYPE_OTA,
                             req_msg, 2 + ver_len, pdMS_TO_TICKS(1000));
}

esp_err_t espx_espnow_ota_cancel(void)
{
    if (s_ota_send.active) {
        ESP_LOGI(TAG, "Cancelling outgoing OTA");
        s_ota_send.active = false;
        s_ota_send.firmware_data = NULL;
        if (s_ota_send.complete_cb) {
            s_ota_send.complete_cb(false, "Cancelled");
        }
    }

    if (s_ota_recv.active) {
        ESP_LOGI(TAG, "Cancelling incoming OTA");
        s_ota_recv.active = false;
        if (s_ota_recv.firmware_data) {
            free(s_ota_recv.firmware_data);
            s_ota_recv.firmware_data = NULL;
        }
        if (s_ota_recv.complete_cb) {
            s_ota_recv.complete_cb(s_ota_recv.src_mac, "", 0, false, "Cancelled");
        }
    }

    return ESP_OK;
}

bool espx_espnow_ota_is_active(void)
{
    return s_ota_send.active || s_ota_recv.active;
}

void espx_espnow_ota_set_receive_callback(espx_ota_receive_cb_t progress_cb,
                                           espx_ota_receive_done_cb_t complete_cb)
{
    s_ota_recv.progress_cb = progress_cb;
    s_ota_recv.complete_cb = complete_cb;
}

/* Process incoming OTA message */
static void process_ota_message(uint8_t *src_addr, const uint8_t *payload, size_t len, int8_t rssi)
{
    if (len < 1) return;

    uint8_t msg_type = payload[0];

    switch (msg_type) {
        case ESPX_ESPNOW_OTA_START: {
            if (len < 12) return;

            /* Check magic */
            uint32_t magic = (payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4];
            if (magic != ESPX_OTA_MAGIC) {
                ESP_LOGW(TAG, "OTA_START with invalid magic: 0x%08X", magic);
                return;
            }

            /* Parse header */
            size_t total_size = (payload[5] << 24) | (payload[6] << 16) | (payload[7] << 8) | payload[8];
            uint16_t total_chunks = (payload[9] << 8) | payload[10];
            uint8_t ver_len = payload[11];
            char version[16] = {0};
            if (ver_len > 0 && ver_len < 16 && len >= 12 + ver_len) {
                memcpy(version, &payload[12], ver_len);
            }

            ESP_LOGI(TAG, "OTA_START from " MACSTR ": size=%d, chunks=%d, version=%s",
                     MAC2STR(src_addr), total_size, total_chunks, version);

            /* Allocate buffer */
            if (s_ota_recv.active) {
                ESP_LOGW(TAG, "OTA already in progress, ignoring");
                return;
            }

            memset(&s_ota_recv, 0, sizeof(s_ota_recv));
            s_ota_recv.active = true;
            memcpy(s_ota_recv.src_mac, src_addr, 6);
            s_ota_recv.firmware_data = (uint8_t*)malloc(total_size);
            if (!s_ota_recv.firmware_data) {
                ESP_LOGE(TAG, "Failed to allocate %d bytes for OTA", total_size);
                s_ota_recv.active = false;
                return;
            }

            s_ota_recv.firmware_size = total_size;
            s_ota_recv.received_bytes = 0;
            s_ota_recv.expected_chunks = total_chunks;
            strncpy(s_ota_recv.version, version, sizeof(s_ota_recv.version) - 1);

            ESP_LOGI(TAG, "OTA receive buffer allocated, waiting for %d chunks", total_chunks);
            break;
        }

        case ESPX_ESPNOW_OTA_DATA: {
            if (!s_ota_recv.active || !s_ota_recv.firmware_data) {
                ESP_LOGW(TAG, "OTA_DATA but no active OTA receive");
                return;
            }

            if (len < 9) return;

            /* Parse offset and size */
            size_t offset = (payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4];
            size_t total_size = (payload[5] << 24) | (payload[6] << 16) | (payload[7] << 8) | payload[8];
            size_t data_len = len - 9;

            /* Validate */
            if (offset + data_len > s_ota_recv.firmware_size) {
                ESP_LOGE(TAG, "OTA_DATA overflow: offset=%d, len=%d, size=%d",
                         offset, data_len, s_ota_recv.firmware_size);
                return;
            }

            /* Copy data */
            memcpy(s_ota_recv.firmware_data + offset, &payload[9], data_len);
            s_ota_recv.received_bytes = offset + data_len;

            int percent = (s_ota_recv.received_bytes * 100) / s_ota_recv.firmware_size;
            ESP_LOGD(TAG, "OTA chunk: offset=%d, len=%d, total=%d/%d (%d%%)",
                     offset, data_len, s_ota_recv.received_bytes, s_ota_recv.firmware_size, percent);

            if (s_ota_recv.progress_cb) {
                s_ota_recv.progress_cb(src_addr, s_ota_recv.version,
                                       s_ota_recv.firmware_size,
                                       s_ota_recv.received_bytes, percent);
            }
            break;
        }

        case ESPX_ESPNOW_OTA_END: {
            if (!s_ota_recv.active) {
                return;
            }

            ESP_LOGI(TAG, "OTA_END received from " MACSTR, MAC2STR(src_addr));

            bool success = false;
            const char *message = "Unknown error";

            /* Verify received data */
            if (s_ota_recv.received_bytes == s_ota_recv.firmware_size) {
                success = true;
                message = "OTA complete";
                ESP_LOGI(TAG, "OTA verified: %d bytes received", s_ota_recv.firmware_size);

                /* TODO: Apply firmware update */
                /* In a real implementation, you would:
                 * 1. Write firmware to flash
                 * 2. Verify checksum
                 * 3. Set boot partition
                 * 4. Reboot
                 */
                ESP_LOGW(TAG, "OTA firmware received but not yet applied (need ESP-IDF OTA support)");
            } else {
                message = "Incomplete transfer";
                ESP_LOGE(TAG, "OTA incomplete: %d/%d bytes",
                         s_ota_recv.received_bytes, s_ota_recv.firmware_size);
            }

            /* Send status */
            uint8_t status_msg[8] = {
                ESPX_ESPNOW_OTA_STATUS,
                success ? 1 : 0,
                0, 0, 0, 0, 0, 0
            };
            espx_espnow_send(src_addr, ESPX_ESPNOW_TYPE_OTA,
                              status_msg, sizeof(status_msg), pdMS_TO_TICKS(1000));

            /* Cleanup */
            if (s_ota_recv.complete_cb) {
                s_ota_recv.complete_cb(s_ota_recv.src_mac, s_ota_recv.version,
                                       s_ota_recv.firmware_size,
                                       success, message);
            }

            if (!success && s_ota_recv.firmware_data) {
                free(s_ota_recv.firmware_data);
            }
            s_ota_recv.active = false;
            s_ota_recv.firmware_data = NULL;
            break;
        }

        case ESPX_ESPNOW_OTA_STATUS: {
            if (len < 2) return;
            bool success = payload[1] != 0;
            ESP_LOGI(TAG, "OTA status from " MACSTR ": %s",
                     MAC2STR(src_addr), success ? "SUCCESS" : "FAILED");
            break;
        }

        case ESPX_ESPNOW_OTA_REQUEST: {
            ESP_LOGI(TAG, "OTA_REQUEST from " MACSTR, MAC2STR(src_addr));
            /* TODO: Respond with OTA_START if we have new firmware */
            /* This would typically check if we have newer version */
            break;
        }

        default:
            ESP_LOGD(TAG, "Unknown OTA message type: 0x%02X", msg_type);
            break;
    }
}

/* ========== ESP-NOW Provisioning Implementation ========== */

void espx_espnow_prov_set_request_callback(espx_prov_request_cb_t callback)
{
    s_prov_listener.request_cb = callback;
}

void espx_espnow_prov_set_complete_callback(espx_prov_complete_cb_t callback)
{
    s_prov_listener.complete_cb = callback;
    s_prov_device.complete_cb = callback;
}

esp_err_t espx_espnow_prov_send_credentials(const uint8_t *device_mac,
                                              const char *ssid, const char *password)
{
    if (!s_running || !device_mac || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build PROV_RESPONSE message */
    /* Format: [type, ssid_len, ssid, pass_len, password] */
    uint8_t msg[256] = {0};
    size_t len = 0;

    msg[len++] = ESPX_ESPNOW_PROV_RESPONSE;

    /* SSID */
    size_t ssid_len = strlen(ssid);
    if (ssid_len > 63) ssid_len = 63;
    msg[len++] = ssid_len;
    memcpy(&msg[len], ssid, ssid_len);
    len += ssid_len;

    /* Password (optional) */
    size_t pass_len = password ? strlen(password) : 0;
    if (pass_len > 63) pass_len = 63;
    msg[len++] = pass_len;
    if (password && pass_len > 0) {
        memcpy(&msg[len], password, pass_len);
        len += pass_len;
    }

    ESP_LOGI(TAG, "Sending Wi-Fi credentials to " MACSTR ": ssid=%s",
             MAC2STR(device_mac), ssid);

    memcpy(s_prov_listener.target_mac, device_mac, 6);
    s_prov_listener.waiting_response = true;

    return espx_espnow_send(device_mac, ESPX_ESPNOW_TYPE_OTA, msg, len, pdMS_TO_TICKS(3000));
}

bool espx_espnow_prov_is_active(void)
{
    return s_prov_listener.listening || s_prov_device.waiting_response;
}

espx_prov_state_t espx_espnow_prov_get_state(void)
{
    if (s_prov_device.waiting_response) {
        return ESPX_PROV_STATE_WAITING_CREDENTIALS;
    }
    if (s_prov_listener.waiting_response) {
        return ESPX_PROV_STATE_CONNECTING;
    }
    return ESPX_PROV_STATE_IDLE;
}

esp_err_t espx_espnow_prov_request(void)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Build PROV_REQUEST message */
    /* Format: [type, device_id_len, device_id, name_len, name] */
    uint8_t msg[96] = {0};
    size_t len = 0;

    msg[len++] = ESPX_ESPNOW_PROV_REQUEST;

    /* Device ID */
    size_t id_len = strlen(s_device_id);
    if (id_len > 31) id_len = 31;
    msg[len++] = id_len;
    memcpy(&msg[len], s_device_id, id_len);
    len += id_len;

    /* Device Name */
    size_t name_len = strlen(s_device_name);
    if (name_len > 31) name_len = 31;
    msg[len++] = name_len;
    if (name_len > 0) {
        memcpy(&msg[len], s_device_name, name_len);
        len += name_len;
    }

    ESP_LOGI(TAG, "Sending provisioning request: id=%s, name=%s",
              s_device_id, s_device_name);

    s_prov_device.waiting_response = true;

    return espx_espnow_broadcast(ESPX_ESPNOW_TYPE_OTA, msg, len);
}

esp_err_t espx_espnow_prov_start_listener(void)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_prov_listener.listening = true;
    s_prov_listener.waiting_response = false;
    ESP_LOGI(TAG, "Started provisioning listener");

    return ESP_OK;
}

void espx_espnow_prov_stop_listener(void)
{
    s_prov_listener.listening = false;
    s_prov_listener.waiting_response = false;
    ESP_LOGI(TAG, "Stopped provisioning listener");
}

/* Process provisioning messages */
static void process_prov_message(uint8_t *src_addr, const uint8_t *payload, size_t len, int8_t rssi)
{
    if (len < 1) return;

    uint8_t msg_type = payload[0];

    switch (msg_type) {
        case ESPX_ESPNOW_PROV_REQUEST: {
            /* New device requesting provisioning */
            if (!s_prov_listener.listening) {
                ESP_LOGD(TAG, "Provisioning listener not active, ignoring request");
                return;
            }

            if (len < 3) return;

            /* Parse device info */
            uint8_t id_len = payload[1];
            char device_id[32] = {0};
            uint8_t name_len = payload[2];
            char name[32] = {0};

            size_t offset = 3;
            if (id_len > 0 && offset + id_len <= len && id_len < 32) {
                memcpy(device_id, &payload[offset], id_len);
                offset += id_len;
            }
            if (offset < len && name_len > 0 && name_len < 32 && offset + name_len <= len) {
                memcpy(name, &payload[offset], name_len);
            }

            ESP_LOGI(TAG, "Provisioning request from " MACSTR ": id=%s, name=%s, rssi=%d",
                     MAC2STR(src_addr), device_id, name, rssi);

            if (s_prov_listener.request_cb) {
                s_prov_listener.request_cb(src_addr, device_id, name, rssi);
            }
            break;
        }

        case ESPX_ESPNOW_PROV_RESPONSE: {
            /* Wi-Fi credentials received (for new device) */
            if (!s_prov_device.waiting_response) {
                ESP_LOGD(TAG, "Not waiting for credentials, ignoring");
                return;
            }

            if (len < 3) return;

            size_t offset = 1;
            uint8_t ssid_len = payload[offset++];
            char ssid[64] = {0};
            if (ssid_len > 0 && offset + ssid_len <= len && ssid_len < 64) {
                memcpy(ssid, &payload[offset], ssid_len);
                offset += ssid_len;
            }

            uint8_t pass_len = payload[offset++];
            char password[64] = {0};
            if (pass_len > 0 && offset + pass_len <= len && pass_len < 64) {
                memcpy(password, &payload[offset], pass_len);
            }

            ESP_LOGI(TAG, "Received Wi-Fi credentials: ssid=%s", ssid);
            ESP_LOGI(TAG, "Wi-Fi password: %s", pass_len > 0 ? "<set>" : "<none>");

            s_prov_device.waiting_response = false;

            /* TODO: Connect to Wi-Fi with these credentials */
            /* This would typically:
             * 1. Store credentials in NVS
             * 2. Disconnect current Wi-Fi
             * 3. Connect to new network
             * 4. Report status via PROV_STATUS
             */
            ESP_LOGW(TAG, "Provisioning credentials received but Wi-Fi connection not implemented yet");

            if (s_prov_device.complete_cb) {
                s_prov_device.complete_cb(true, ssid, "", "Wi-Fi credentials received");
            }
            break;
        }

        case ESPX_ESPNOW_PROV_STATUS: {
            /* Device reports provisioning status (for provisioner) */
            if (!s_prov_listener.waiting_response) {
                return;
            }

            if (len < 2) return;

            uint8_t status = payload[1];
            char ip[16] = {0};
            char error[64] = {0};

            /* Parse IP if present */
            if (len > 2 && payload[2] != 0) {
                uint8_t ip_len = payload[2];
                if (ip_len > 0 && ip_len < 16 && len >= 3 + ip_len) {
                    memcpy(ip, &payload[3], ip_len);
                }
            }

            /* Parse error if present */
            size_t offset = 3 + (len > 2 ? payload[2] : 0);
            if (offset < len) {
                uint8_t err_len = payload[offset++];
                if (err_len > 0 && offset + err_len <= len && err_len < 64) {
                    memcpy(error, &payload[offset], err_len);
                }
            }

            ESP_LOGI(TAG, "Provisioning status from " MACSTR ": status=%d, ip=%s, error=%s",
                     MAC2STR(src_addr), status, ip, error);

            s_prov_listener.waiting_response = false;

            if (s_prov_listener.complete_cb) {
                bool success = (status == 0);
                s_prov_listener.complete_cb(success, "", ip, error);
            }
            break;
        }

        default:
            ESP_LOGD(TAG, "Unknown provisioning message type: 0x%02X", msg_type);
            break;
    }
}
