/**
 * @file esp_now_service.h
 * @brief ESP-NOW peer-to-peer communication service
 *
 * Uses espressif/esp-now component for advanced features:
 * - ACK and retransmission
 * - Forwarding and mesh-like capabilities
 * - Groups support
 * - Security encryption
 * - Multiple data types
 */

#ifndef ESP_NOW_SERVICE_H
#define ESP_NOW_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of ESP-NOW peers (ESP32 hardware limit) */
#define ESPX_ESPNOW_MAX_PEERS 20

/** Maximum number of groups */
#define ESPX_ESPNOW_MAX_GROUPS 10

/** Maximum number of discovered (unpaired) devices */
#define ESPX_ESPNOW_MAX_DISCOVERED 20

/** Discovery message types */
#define ESPX_ESPNOW_DISCOVER_MSG       0x01
#define ESPX_ESPNOW_ANNOUNCE_MSG       0x02

/** Discovery broadcast interval (30 seconds) */
#define ESPX_ESPNOW_ANNOUNCE_INTERVAL_MS  30000

/** Peer info (for paired peers) */
typedef struct {
    char id[32];              /**< Peer identifier */
    uint8_t mac[6];          /**< MAC address */
    bool paired;              /**< Whether peer is registered */
    int64_t last_seen_ms;    /**< Last activity timestamp (ms) */
    int8_t rssi;             /**< Last RSSI */
} espx_espnow_peer_info_t;

/** Discovered device info (unpaired) */
typedef struct {
    uint8_t mac[6];          /**< MAC address */
    char device_id[32];       /**< Device ID */
    char name[32];           /**< Device name */
    char version[16];         /**< Firmware version */
    int64_t last_seen_ms;    /**< Last announcement timestamp */
    int8_t rssi;             /**< Last RSSI */
    bool pending_pair;        /**< Waiting for user confirmation */
} espx_espnow_discovered_t;

/** ESP-NOW group info */
typedef struct {
    uint8_t id[6];           /**< Group ID */
    char name[32];           /**< Group name (optional) */
    bool active;             /**< Whether group is active */
} espx_espnow_group_info_t;

/** Data type for ESP-NOW messages */
typedef enum {
    ESPX_ESPNOW_TYPE_DATA = 0,      /**< User-defined data */
    ESPX_ESPNOW_TYPE_CMD,           /**< Command message */
    ESPX_ESPNOW_TYPE_STATE,         /**< State report */
    ESPX_ESPNOW_TYPE_DISCOVER,      /**< Discovery message */
    ESPX_ESPNOW_TYPE_ANNOUNCE,      /**< Announcement/broadcast */
    ESPX_ESPNOW_TYPE_OTA,          /**< OTA update message */
    ESPX_ESPNOW_TYPE_MAX
} espx_espnow_data_type_t;

/** ESP-NOW OTA message types */
#define ESPX_ESPNOW_OTA_START       0x01
#define ESPX_ESPNOW_OTA_DATA        0x02
#define ESPX_ESPNOW_OTA_END         0x03
#define ESPX_ESPNOW_OTA_STATUS      0x04
#define ESPX_ESPNOW_OTA_REQUEST     0x05

/** OTA chunk size (max payload per ESP-NOW frame) */
#define ESPX_ESPNOW_OTA_CHUNK_SIZE  200

/** OTA state */
typedef enum {
    ESPX_OTA_STATE_IDLE = 0,
    ESPX_OTA_STATE_RECEIVING,
    ESPX_OTA_STATE_VALIDATING,
    ESPX_OTA_STATE_APPLYING,
    ESPX_OTA_STATE_SUCCESS,
    ESPX_OTA_STATE_FAILED
} espx_ota_state_t;

/* ========== ESP-NOW Provisioning ========== */

/** Provisioning message types */
#define ESPX_ESPNOW_PROV_REQUEST    0x20
#define ESPX_ESPNOW_PROV_RESPONSE   0x21
#define ESPX_ESPNOW_PROV_STATUS     0x22

/** Provisioning state */
typedef enum {
    ESPX_PROV_STATE_IDLE = 0,
    ESPX_PROV_STATE_WAITING_CREDENTIALS,
    ESPX_PROV_STATE_CONNECTING,
    ESPX_PROV_STATE_CONNECTED,
    ESPX_PROV_STATE_FAILED
} espx_prov_state_t;

/** Provisioning callback */
typedef void (*espx_prov_request_cb_t)(const uint8_t *mac, const char *device_id,
                                        const char *name, int8_t rssi);
typedef void (*espx_prov_complete_cb_t)(bool success, const char *ssid,
                                         const char *ip, const char *error);

/**
 * @brief Register callback for provisioning requests
 *
 * Called when a new device requests provisioning.
 */
void espx_espnow_prov_set_request_callback(espx_prov_request_cb_t callback);

/**
 * @brief Register callback for provisioning completion
 */
void espx_espnow_prov_set_complete_callback(espx_prov_complete_cb_t callback);

/**
 * @brief Send provisioning response to a device
 *
 * @param device_mac Target device MAC address
 * @param ssid Wi-Fi SSID
 * @param password Wi-Fi password (can be NULL for open networks)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_prov_send_credentials(const uint8_t *device_mac,
                                              const char *ssid, const char *password);

/**
 * @brief Check if provisioning is in progress
 */
bool espx_espnow_prov_is_active(void);

/**
 * @brief Get current provisioning state
 */
espx_prov_state_t espx_espnow_prov_get_state(void);

/**
 * @brief Send provisioning request (for new devices)
 *
 * Called by a new device that hasn't been configured yet.
 */
esp_err_t espx_espnow_prov_request(void);

/**
 * @brief Start listening for provisioning requests (for provisioner)
 *
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_prov_start_listener(void);

/**
 * @brief Stop provisioning listener
 */
void espx_espnow_prov_stop_listener(void);

/** Message structure (packed) */
typedef struct __attribute__((packed)) {
    uint16_t magic;                  /**< Unique identifier for deduplication */
    uint8_t type;                    /**< Message type */
    uint8_t ttl;                     /**< Time to live */
    uint8_t src_mac[6];              /**< Source MAC */
    uint8_t seq;                     /**< Sequence number */
    uint8_t payload[];               /**< Variable length payload */
} espx_espnow_msg_t;

/**
 * @brief Initialize ESP-NOW service
 *
 * Must be called after Wi-Fi is initialized.
 *
 * @param enable_security Enable AES-128 encryption
 * @param enable_forward Enable packet forwarding
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_init(bool enable_security, bool enable_forward);

/**
 * @brief Start ESP-NOW service
 *
 * Begins ESP-NOW operation. Wi-Fi must be connected.
 */
esp_err_t espx_espnow_start(void);

/**
 * @brief Stop ESP-NOW service
 */
esp_err_t espx_espnow_stop(void);

/**
 * @brief Deinitialize ESP-NOW service
 */
esp_err_t espx_espnow_deinit(void);

/**
 * @brief Add a peer to ESP-NOW
 *
 * @param mac MAC address of the peer (6 bytes)
 * @param lmk Local Master Key for encryption (16 bytes, NULL for unencrypted)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_add_peer(const uint8_t *mac, const uint8_t *lmk);

/**
 * @brief Remove a peer from ESP-NOW
 *
 * @param mac MAC address of the peer (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_remove_peer(const uint8_t *mac);

/**
 * @brief Add a group
 *
 * @param group_id Group ID (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_add_group(const uint8_t *group_id);

/**
 * @brief Remove a group
 *
 * @param group_id Group ID (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_remove_group(const uint8_t *group_id);

/**
 * @brief Send data to a peer
 *
 * @param mac Destination MAC address (NULL for broadcast)
 * @param type Message type
 * @param data Data payload
 * @param len Payload length (max 200 bytes for payload)
 * @param wait_ticks Max wait time in RTOS ticks
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_send(const uint8_t *mac, espx_espnow_data_type_t type,
                           const uint8_t *data, size_t len, uint32_t wait_ticks);

/**
 * @brief Broadcast data to all peers
 *
 * @param type Message type
 * @param data Data payload
 * @param len Payload length
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_broadcast(espx_espnow_data_type_t type,
                                const uint8_t *data, size_t len);

/**
 * @brief Send data to a group
 *
 * @param group_id Group ID (6 bytes, NULL for all registered groups)
 * @param type Message type
 * @param data Data payload
 * @param len Payload length
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_send_group(const uint8_t *group_id, espx_espnow_data_type_t type,
                                  const uint8_t *data, size_t len);

/**
 * @brief Get local MAC address
 *
 * @param mac Buffer to store MAC address (6 bytes)
 */
void espx_espnow_get_local_mac(uint8_t *mac);

/**
 * @brief Check if service is running
 */
bool espx_espnow_is_running(void);

/**
 * @brief Get peer count
 */
int espx_espnow_get_peer_count(void);

/**
 * @brief Get group count
 */
int espx_espnow_get_group_count(void);

/**
 * @brief Get peer by index
 */
const espx_espnow_peer_info_t* espx_espnow_get_peer(int index);

/**
 * @brief Get group by index
 */
const espx_espnow_group_info_t* espx_espnow_get_group(int index);

/**
 * @brief Register receive callback
 *
 * @param callback Callback function (src_mac, type, data, len, rssi)
 */
typedef void (*espx_espnow_recv_callback_t)(const uint8_t *src_mac, uint8_t type,
                                            const uint8_t *data, size_t len, int8_t rssi);
void espx_espnow_set_recv_callback(espx_espnow_recv_callback_t callback);

/**
 * @brief Trigger manual discovery scan
 *
 * Broadcasts a DISCOVER message to find nearby devices.
 * Discovered devices will respond with ANNOUNCE.
 *
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_discover(void);

/**
 * @brief Get discovered devices count
 */
int espx_espnow_get_discovered_count(void);

/**
 * @brief Get discovered device by index
 */
const espx_espnow_discovered_t* espx_espnow_get_discovered(int index);

/**
 * @brief Pair with a discovered device
 */
esp_err_t espx_espnow_pair_discovered(const uint8_t *mac);

/**
 * @brief Clear discovered devices list
 */
void espx_espnow_clear_discovered(void);

/**
 * @brief Configure ESP-NOW from JSON
 *
 * Config format:
 * {
 *   "enabled": true,
 *   "pmk": "0123456789abcdef",
 *   "security": true,
 *   "forward": true,
 *   "groups": [{"id": "01:02:03:04:05:06", "name": "lights"}],
 *   "peers": [{"id": "relay1", "mac": "AA:BB:CC:DD:EE:FF", "key": "..."}]
 * }
 */
esp_err_t espx_espnow_configure(const cJSON *config);

/**
 * @brief Export current configuration
 */
cJSON* espx_espnow_config_export(void);

/**
 * @brief Get version string
 */
const char* espx_espnow_version(void);

/* ========== ESP-NOW OTA ========== */

/**
 * @brief OTA progress callback
 */
typedef void (*espx_ota_progress_cb_t)(size_t received, size_t total, int percent);

/**
 * @brief OTA complete callback
 */
typedef void (*espx_ota_complete_cb_t)(bool success, const char *message);

/**
 * @brief Start OTA update to a peer device
 *
 * @param peer_mac Target device MAC address
 * @param firmware_data Firmware binary data
 * @param size Firmware size in bytes
 * @param version Firmware version string
 * @param progress_cb Progress callback (can be NULL)
 * @param complete_cb Complete callback (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_ota_start(const uint8_t *peer_mac, const uint8_t *firmware_data,
                                  size_t size, const char *version,
                                  espx_ota_progress_cb_t progress_cb,
                                  espx_ota_complete_cb_t complete_cb);

/**
 * @brief Request OTA from a peer device
 *
 * Sends OTA request to peer, which will respond with firmware data.
 *
 * @param peer_mac Target device MAC address
 * @param current_version Current firmware version
 * @param progress_cb Progress callback (can be NULL)
 * @param complete_cb Complete callback (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_ota_request(const uint8_t *peer_mac, const char *current_version,
                                    espx_ota_progress_cb_t progress_cb,
                                    espx_ota_complete_cb_t complete_cb);

/**
 * @brief Cancel ongoing OTA
 *
 * @return ESP_OK on success
 */
esp_err_t espx_espnow_ota_cancel(void);

/**
 * @brief Check if OTA is in progress
 */
bool espx_espnow_ota_is_active(void);

/**
 * @brief Register callback for receiving OTA from peer
 *
 * @param firmware_data Buffer to store received firmware
 * @param max_size Maximum buffer size
 * @param progress_cb Progress callback (can be NULL)
 * @param complete_cb Complete callback (can be NULL)
 */
typedef void (*espx_ota_receive_cb_t)(const uint8_t *src_mac, const char *version,
                                        size_t size, size_t received, int percent);
typedef void (*espx_ota_receive_done_cb_t)(const uint8_t *src_mac, const char *version,
                                           size_t size, bool success, const char *message);

void espx_espnow_ota_set_receive_callback(espx_ota_receive_cb_t progress_cb,
                                           espx_ota_receive_done_cb_t complete_cb);

#ifdef __cplusplus
}
#endif

#endif /* ESP_NOW_SERVICE_H */
