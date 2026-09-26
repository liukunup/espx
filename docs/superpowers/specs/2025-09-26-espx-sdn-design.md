# ESPX SDN (Software Defined Node) Design Specification

**Version:** 1.0.0
**Date:** 2025-09-26
**Hardware:** ESP32-S3 R16N8

---

## 1. Overview

ESPX is a generic IoT node firmware that allows runtime configuration of hardware devices through:
- Web UI (HTTPS)
- MQTT commands
- NVS persistence
- Factory provisioning

The firmware implements a Software-Defined Node (SDN) pattern where device bindings are configured at runtime rather than hardcoded.

---

## 2. Architecture

### 2.1 Module Structure

```
┌────────────────────────────────────────────────────────┐
│ Application Layer                                      │
│  - MQTT Command Handler                                │
│  - Web REST API                                        │
│  - Test Mode CLI                                       │
├────────────────────────────────────────────────────────┤
│ Core Services                                          │
│  - device_manager: runtime device registry            │
│  - device_type: driver type registry                  │
│  - event_bus: pub/sub event dispatcher                │
│  - param_store: NVS-backed configuration              │
│  - node_config: top-level node configuration          │
├────────────────────────────────────────────────────────┤
│ Device Drivers                                         │
│  - dht11: temperature/humidity sensor                 │
│  - button: input button with debounce                 │
│  - relay: GPIO-controlled relay                       │
│  - shiftreg_595: cascaded shift register              │
├────────────────────────────────────────────────────────┤
│ ESP-IDF HAL                                            │
└────────────────────────────────────────────────────────┘
```

### 2.2 File Structure

```
main/
├── app_main.c
├── app/
│   ├── app.c
│   └── app.h
├── core/                       # Core services
│   ├── device_manager.c/.h
│   ├── device_type.c/.h
│   ├── event_bus.c/.h
│   └── node_config.c/.h
├── peripherals/               # Device drivers
│   ├── dht11.c/.h
│   ├── button.c/.h
│   ├── relay.c/.h
│   └── shiftreg_595.c/.h
├── services/                  # High-level services
│   ├── wifi_prov/
│   ├── mqtt_client/
│   ├── web_server/
│   ├── ota_service/
│   ├── mfg_provision/
│   └── test_mode/
└── param_store/
```

---

## 3. Core Data Structures

### 3.1 Device Type (driver registration)

```c
typedef enum {
    DEVICE_VALUE_TYPE_BOOL,
    DEVICE_VALUE_TYPE_INT,
    DEVICE_VALUE_TYPE_FLOAT,
    DEVICE_VALUE_TYPE_STRING,
    DEVICE_VALUE_TYPE_BLOB,
} device_value_type_t;

typedef enum {
    DEVICE_CAPABILITY_READ    = (1 << 0),
    DEVICE_CAPABILITY_WRITE   = (1 << 1),
    DEVICE_CAPABILITY_NOTIFY  = (1 << 2),
} device_capability_t;

typedef struct device device_t;

typedef struct device_type {
    const char *name;
    const char *description;
    uint32_t capabilities;  // bitmask of device_capability_t

    // Lifecycle
    esp_err_t (*init)(device_t *dev, const cJSON *config);
    esp_err_t (*deinit)(device_t *dev);

    // Operations (may be NULL if not supported)
    esp_err_t (*read)(device_t *dev, cJSON *value);
    esp_err_t (*write)(device_t *dev, const cJSON *value);

    // Config
    esp_err_t (*get_default_config)(cJSON *config);
    esp_err_t (*validate_config)(const cJSON *config);
} device_type_t;
```

### 3.2 Device Instance

```c
struct device {
    char id[32];              // unique id
    char name[64];            // display name
    const device_type_t *type;
    bool enabled;
    cJSON *config;            // type-specific config
    cJSON *state;             // last known state
    void *driver_data;        // private driver data
    bool initialized;
};
```

### 3.3 Event Bus

```c
typedef enum {
    EVENT_DEVICE_ADDED,
    EVENT_DEVICE_REMOVED,
    EVENT_DEVICE_CHANGED,    // config or state changed
    EVENT_DEVICE_VALUE_CHANGED,
    EVENT_NODE_READY,
    EVENT_NODE_RESET,
} event_type_t;

typedef struct event {
    event_type_t type;
    const char *topic;        // optional topic string (e.g., device id)
    cJSON *data;
} event_t;

typedef void (*event_handler_t)(const event_t *event, void *user_data);
```

### 3.4 Node Configuration (stored in NVS)

```json
{
  "node": {
    "device_id": "espx-A4CF12",
    "name": "ESPX Node",
    "fw_version": "1.0.0"
  },
  "network": {
    "wifi_ssid": "...",
    "wifi_password": "...",
    "mqtt_broker": "mqtt://...",
    "mqtt_username": "...",
    "mqtt_password": "...",
    "mqtt_topic_prefix": "espx/{device_id}"
  },
  "devices": [
    {
      "id": "dht11_indoor",
      "type": "dht11",
      "enabled": true,
      "config": { "gpio": 4, "interval_ms": 5000 }
    }
  ]
}
```

---

## 4. Public API

### 4.1 Device Manager

```c
// Type registry
esp_err_t device_type_register(const device_type_t *type);
const device_type_t* device_type_get(const char *name);
const device_type_t* device_type_get_all(size_t *count);

// Device lifecycle
esp_err_t device_manager_init(void);
esp_err_t device_manager_load_from_nvs(void);
esp_err_t device_manager_save_to_nvs(void);

esp_err_t device_add(const char *id, const char *type_name, const cJSON *config);
esp_err_t device_remove(const char *id);
esp_err_t device_set_enabled(const char *id, bool enabled);
esp_err_t device_update_config(const char *id, const cJSON *config);
device_t* device_get(const char *id);
size_t device_get_count(void);
device_t* device_get_by_index(size_t index);

// Operations
esp_err_t device_read_all(cJSON *result);
esp_err_t device_read(const char *id, cJSON *result);
esp_err_t device_write(const char *id, const cJSON *value);
```

### 4.2 Event Bus

```c
esp_err_t event_bus_init(void);
esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void *user_data);
esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler);
esp_err_t event_bus_publish(event_type_t type, const char *topic, cJSON *data);
```

### 4.3 Node Config

```c
esp_err_t node_config_init(void);
esp_err_t node_config_save(void);
esp_err_t node_config_get(cJSON *config);
esp_err_t node_config_set(const cJSON *config);
const char* node_config_get_device_id(void);
const char* node_config_get_name(void);
```

---

## 5. Device Driver Examples

### 5.1 Relay (write-only)

```c
static esp_err_t relay_init(device_t *dev, const cJSON *config) {
    int gpio = cJSON_GetObjectItem(config, "gpio")->valueint;
    int active_level = cJSON_GetObjectItem(config, "active_level")->valueint;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(gpio, !active_level);  // default off

    relay_data_t *data = malloc(sizeof(relay_data_t));
    data->gpio = gpio;
    data->active_level = active_level;
    dev->driver_data = data;
    return ESP_OK;
}

static esp_err_t relay_write(device_t *dev, const cJSON *value) {
    relay_data_t *data = dev->driver_data;
    bool on = cJSON_IsTrue(value);
    gpio_set_level(data->gpio, on ? data->active_level : !data->active_level);
    event_bus_publish(EVENT_DEVICE_VALUE_CHANGED, dev->id, value);
    return ESP_OK;
}

static const device_type_t relay_driver = {
    .name = "relay",
    .description = "GPIO-controlled relay",
    .capabilities = DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = relay_init,
    .write = relay_write,
};
```

### 5.2 74HC595 Shift Register

```c
// Config: data_gpio, clock_gpio, latch_gpio, count, oe_gpio
// Write: array of bytes (uint8 array)
// State: array of current output bytes
```

---

## 6. Implementation Phases

- **P1**: Core (device_manager, device_type, event_bus, node_config)
- **P2**: Drivers (dht11, button, relay, shiftreg_595)
- **P3**: MQTT integration (command handler, auto-publish state)
- **P4**: Web UI (Dashboard, Devices page)
- **P5**: Test mode (CLI for hardware self-test)

---

## 7. NVS Layout

```
Namespace "espx_node":
  Key "config"      -> JSON string (entire node config)
  Key "device_id"   -> string

Namespace "espx_params":
  ... existing parameters ...
```

---

## 8. MQTT Topic Schema

```
espx/{device_id}/
  state                 online | offline | heartbeat
  sensors               {device_id: {value}}
  attrs                 {device_id: {value}}
  cmd/query/{device}    {"action": "get"}
  cmd/control/{device}  {"action": "set", "value": ...}
  cmd/config/devices    {"action": "set", "devices": [...]}
  cmd/reboot            {}
```

---

## 9. Web REST API

```
GET  /api/node                  - node info
PUT  /api/node                  - update node config
GET  /api/devices               - list devices
POST /api/devices               - add device
GET  /api/devices/{id}          - get device
PUT  /api/devices/{id}          - update device
DELETE /api/devices/{id}        - remove device
POST /api/devices/{id}/read     - read device value
POST /api/devices/{id}/write    - write device value
POST /api/devices/{id}/enable   - enable/disable device
POST /api/devices/reload        - reload all devices
GET  /api/device-types          - list available device types
GET  /api/device-types/{name}/default-config - get default config
```
