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
    ESPX_ESPNOW_TYPE_MAX
} espx_espnow_data_type_t;

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

#ifdef __cplusplus
}
#endif

#endif /* ESP_NOW_SERVICE_H */
