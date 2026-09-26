# ESPX — Architecture & Technical Reference

Software-defined IoT node for ESP32-S3 (R16N8: 16 MB flash, 8 MB octal PSRAM).

---

## 1. What it is

A generic node firmware whose hardware behaviour is **defined by configuration,
not by code**. You bind peripherals at runtime (Web UI, MQTT, factory data),
the binding is persisted in NVS, and the device exposes whatever it was bound to
over HTTPS and MQTT.

```
                 ┌──────────────────────────────────────────────┐
   Web browser ──┤  HTTPS server (self-signed cert)             │
   (self-signed) │    static UI + REST API                      │
                 ├──────────────────────────────────────────────┤
   MQTT broker ──┤  MQTT client                                 │
                 │    state / sensors / attrs  →  publish       │
                 │    cmd/…                    ←  subscribe     │
                 ├──────────────────────────────────────────────┤
   UART console ─┤  Manufacturing test mode (self-test CLI)     │
                 ├──────────────────────────────────────────────┤
   BOOT button ──┤  long-press 3 s → reboot into test mode      │
                 ├──────────────────────────────────────────────┤
                 │            core                              │
                 │  device_type registry  (driver catalogue)    │
                 │  device_manager        (runtime instances)   │
                 │  event_bus             (pub/sub)             │
                 │  node_config           (identity + network)  │
                 │  mfg_provision         (factory defaults)    │
                 │  ota_service           (delta OTA)           │
                 ├──────────────────────────────────────────────┤
                 │  peripherals (device_type drivers)           │
                 │  dht11 · button · relay · shiftreg_595       │
                 │  · ws2812                                    │
                 └──────────────────────────────────────────────┘
```

---

## 2. Core concepts

### 2.1 device_type — the driver catalogue

A `device_type_t` describes what a class of hardware can do and how to drive it.

```c
typedef struct device_type {
    const char *name;                  // "relay", "dht11", ...
    const char *description;
    uint32_t capabilities;             // READ | WRITE | NOTIFY | PERIODIC

    esp_err_t (*init)(device_t *dev, const cJSON *config);
    esp_err_t (*deinit)(device_t *dev);
    esp_err_t (*read)(device_t *dev, cJSON *value);
    esp_err_t (*write)(device_t *dev, const cJSON *value);
    esp_err_t (*get_default_config)(cJSON *config);
    esp_err_t (*validate_config)(const cJSON *config);
    esp_err_t (*tick)(device_t *dev);
} device_type_t;
```

Capability bits drive behaviour automatically:

| Capability | Meaning | Effect |
|---|---|---|
| `READ` | can report a value | exposed via `GET /api/devices`, `read` in self-test |
| `WRITE` | can be actuated | exposed via MQTT `cmd/control`, `POST …/write`, toggled in self-test |
| `NOTIFY` | emits change events | published by the device on change |
| `PERIODIC` | sampled on a timer | grouped into the MQTT `sensors` topic |

Adding new hardware = write one file implementing this struct and register it in
`peripherals_register_all()`. Nothing else changes.

### 2.2 device — a runtime instance

```c
struct device {
    char id[32];                 // "relay1", "temp_indoor", ...
    const device_type_t *type;   // driver
    bool enabled;
    cJSON *config;               // type-specific, e.g. {"gpio":5,"active_level":1}
    cJSON *state;                // last written value
    void *driver_data;           // private driver state
    bool initialized;
};
```

Instances are created by `device_add(id, type_name, config)`, which validates the
config, calls the driver's `init`, and publishes `EVENT_DEVICE_ADDED`.

The whole set is persisted as JSON in NVS namespace `espx_devices`, key `config`.

### 2.3 event_bus

Synchronous publish/subscribe, 32 handler slots, no allocation.

```
EVENT_DEVICE_ADDED / REMOVED / CHANGED / VALUE_CHANGED
EVENT_NODE_READY / RESET
EVENT_WIFI_CONNECTED / DISCONNECTED
EVENT_MQTT_CONNECTED / DISCONNECTED
EVENT_OTA_START / PROGRESS / COMPLETE / FAILED
```

Handlers run in the publisher's task, so they must not block.

### 2.4 node_config

Node identity plus the network block, stored as one JSON document in NVS
namespace `espx_node`, key `config`.

```json
{
  "node":    { "device_id": "espx-84C7BB772E74", "name": "ESPX", "fw_version": "1.0.0" },
  "network": { "mqtt_broker": "…", "mqtt_username": "…",
               "mqtt_password": "…", "mqtt_topic_prefix": "espx/84C7BB772E74" }
}
```

`device_id` defaults to the STA MAC and doubles as the MQTT client id and the
default topic prefix.

---

## 3. Peripheral drivers

| Type | Capabilities | Config keys | Notes |
|---|---|---|---|
| `dht11` | R, P | `gpio`, `interval_ms` | bit-banged single-wire; checksum verified |
| `button` | R, N | `gpio`, `active_level`, `pullup` | polled debounce (50 ms), no ISR |
| `relay` | R, W, N | `gpio`, `active_level` | OFF at init |
| `shiftreg_595` | R, W, N | `data_gpio`, `clock_gpio`, `latch_gpio`, `oe_gpio`, `count` | `count` cascades 1–8 chips (8–64 outputs) |
| `ws2812` | R, W, N | `data_gpio`, `count`, `brightness` | RMT-driven; `count` is the LED count, 1–300 |

Write payload shapes:

```jsonc
// relay
true                                   // or {"state": true}
// shiftreg_595  (count=2 → 16 outputs; index 0 is the first chip)
[1, 255]                               // or {"bytes": [1, 255]}
// ws2812
{"all":   {"r":255,"g":0,"b":0}}       // whole strip
{"index": 3, "r":0,"g":0,"b":255}      // one pixel
{"pixels":[{"r":255,"g":0,"b":0}, …]}  // per-pixel
```

---

## 4. Interfaces

### 4.1 MQTT

Topic prefix defaults to `espx/<device_id>` and is configurable.

| Direction | Topic | Payload |
|---|---|---|
| pub | `<prefix>/state` | `{"online":true,"uptime":…}` (retained) |
| pub | `<prefix>/sensors` | periodic `PERIODIC` device values |
| pub | `<prefix>/attrs/…` | values / device list / device types |
| pub | `<prefix>/ota/status` | OTA state and progress |
| sub | `<prefix>/cmd/query/<id>` | `{"action":"get"}` |
| sub | `<prefix>/cmd/control/<id>` | `{"action":"set","value":…}` |
| sub | `<prefix>/cmd/config` | `{"action":"get_devices"｜"get_device_types"｜"reboot"｜"testmode"}` |
| sub | `<prefix>/cmd/ota` | `{"action":"start","url":"…"｜"status"｜"cancel"}` |
| sub | `<prefix>/cmd/reboot` | `{}` |

### 4.2 HTTPS / REST

Self-signed certificate (see §6). Handlers are registered in this order so exact
paths win over the wildcard dispatcher.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | embedded single-page UI |
| GET/PUT | `/api/node` | node identity |
| GET | `/api/system/info` | uptime, heap, Wi-Fi SSID/RSSI, IP |
| GET/PUT | `/api/network` | MQTT broker, credentials, topic prefix |
| GET | `/api/devices` | device list **with live values** |
| POST | `/api/devices` | add device `{id,type,config}` |
| POST | `/api/devices/reload` | deinit + re-init all devices |
| GET | `/api/device-types` | driver catalogue + default configs |
| GET | `/api/ota/status` | OTA state, progress, bytes |
| POST | `/api/ota/start` | `{"url":"http://…/fw.patch"}` |
| POST | `/api/ota/cancel` | cancel a running update |
| GET | `/api/certs/info` | certificate metadata |
| POST | `/api/system/reboot` | reboot |
| POST | `/api/system/testmode` | reboot into manufacturing test mode |
| GET | `/api/devices/*` | device detail |
| POST | `/api/devices/*` | `<id>` update config, `<id>/read`, `<id>/write`, `<id>/enable` |
| DELETE | `/api/devices/*` | remove device |

`/api/devices/*` is a single dispatcher because the ESP-IDF HTTP server only
supports a trailing `*` wildcard — `/api/devices/*/read` would never match.

### 4.3 Serial test console

Reached by holding BOOT for 3 s, via `POST /api/system/testmode`, or MQTT
`cmd/config {"action":"testmode"}`.

```
help · types · list · add <id> <type> [json] · del <id> · read <id>
write <id> <json> · test [id|all] · report · reset
mfg show|set <json>|apply|clear · exit
```

---

## 5. Delta OTA

### 5.1 Why delta

| | size |
|---|---|
| full image | ~1.25 MB |
| heatshrink delta patch | ~19 KB (**98.4 % smaller**) |

### 5.2 Patch format

```
offset  0 ..  3   magic 0xfccdde10 (LE)
offset  4 .. 35   SHA-256 of the BASE app image
offset 36 .. 63   reserved (28 bytes, zero)
offset 64 ..      detools patch, heatshrink compressed
```

### 5.3 Verification (this is the safety property)

The digest is the app image's own SHA-256 — the same value esptool prints as
*Validation hash*, and the same value `esp_partition_get_sha256()` returns for an
app partition. Before writing anything, the device compares it against the
**currently running** firmware and rejects a mismatch:

```
I (…) ota_service: Patch header verified against running firmware
E (…) ota_service: OTA failed: patch was built for a different firmware version
```

So a patch built against the wrong base can never brick a device.

### 5.4 Flow

```
GET patch ─► read 64-byte header ─► verify magic + SHA-256
   │
   ├─ esp_ota_begin(next partition)
   ├─ stream patch → esp_delta_ota_feed_patch
   │      ├─ read_cb  : esp_partition_read(running partition)
   │      └─ write_cb : esp_ota_write(next partition)   [chip-id checked]
   ├─ esp_delta_ota_finalize → esp_ota_end
   ├─ esp_ota_set_boot_partition(next)     (needs the otadata partition)
   └─ reboot
```

Rollback is enabled (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). On a successful
boot the app calls `ota_service_mark_valid()`, which cancels rollback.

### 5.5 Building a patch

```bash
tools/make_delta_patch.py --base build/espx-v1.0.0.bin \
                          --new  build/espx-v1.0.1.bin \
                          --out  dist/v1.0.0_to_v1.0.1.patch
```

Requires `detools >= 0.49.0`. The tool also verifies by applying the patch and
comparing the reconstruction byte-for-byte with the new image.

---

## 6. Certificates and build-time files

The certificate and key are **project files**, not code:

```
main/cert_manager/certs/server.crt
main/cert_manager/certs/server.key
main/web_server/web_files/index.html
```

They are embedded at build time:

```cmake
idf_component_register(
    …
    EMBED_FILES
        "web_server/web_files/index.html"
        "cert_manager/certs/server.crt"
        "cert_manager/certs/server.key"
)
```

`cert_manager.c` refers to them only through the generated
`_binary_<file>_start/_end` symbols, so replacing a certificate is a file edit
plus `idf.py build` — no C changes.

Regenerate with:

```bash
tools/gen_certs.sh [CN] [DAYS]      # default CN=espx.local, 3650 days
```

> The bundled key is a **development** key and is committed to the repository so
> a fresh checkout builds. Replace it before shipping, or provision per-device
> certificates.

---

## 7. Factory provisioning

Factory data is a JSON document written to the dedicated `mfg_data` NVS
partition (namespace `factory`, key `config`). On boot it is applied once and
then erased, so a re-flash of the main firmware does not re-apply stale data.

```json
{
  "node":    { "device_id": "espx-0001", "name": "Line-1" },
  "network": { "wifi_ssid": "PlantNet", "wifi_password": "…",
               "mqtt_broker": "mqtt://broker.plant:1883",
               "mqtt_username": "line1", "mqtt_password": "…",
               "mqtt_topic_prefix": "plant/line1" },
  "devices": [
    { "id": "temp_in", "type": "dht11", "enabled": true,
      "config": { "gpio": 4, "interval_ms": 5000 } },
    { "id": "relay_a", "type": "relay",
      "config": { "gpio": 5, "active_level": 1 } }
  ]
}
```

### 7.1 Pre-provisioned Wi-Fi (no operator needed)

If `network.wifi_ssid` is present, the device connects to it **directly** and never
starts the SoftAP provisioning service. A factory-programmed unit therefore comes
up on the plant network unattended:

```
I (…) wifi_prov: Using pre-provisioned Wi-Fi credentials for SSID 'PlantNet'
I (…) wifi_prov: Got IP: 192.168.1.57
```

Without it, the device falls back to the normal SoftAP/BLE provisioning flow.

> The credentials are stored in plaintext NVS (`espx_node/config`). That is
> normal for factory provisioning, but it means physical flash access reveals the
> Wi-Fi password. Use per-device PSK or encrypted NVS if that matters.


Written at the factory with the test console:

```
mfg set {"node":{…},"devices":[…]}
mfg show          # present?
mfg apply         # apply now
mfg clear         # erase
```

---

## 8. Partition layout (16 MB)

| Label | Type | Offset | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data/nvs | 0x009000 | 24 K | provisioning credentials, node + device config |
| `otadata` | data/ota | 0x00f000 | 8 K | **required** for `esp_ota_set_boot_partition()` |
| `phy_init` | data/phy | 0x011000 | 4 K | RF calibration |
| `factory` | app/factory | 0x020000 | 4 M | factory firmware |
| `ota_0` | app/ota_0 | 0x420000 | 4 M | OTA slot A |
| `ota_1` | app/ota_1 | 0x820000 | 4 M | OTA slot B |
| `certs` | data/fat | 0xC20000 | 16 K | reserved for runtime certificates |
| `mfg_data` | data/nvs | 0xC24000 | 16 K | factory data |

The `otadata` partition is not optional: without it OTA cannot record which slot
to boot.

---

## 9. Boot sequence

```
app_main()
 ├─ nvs_flash_init()                      // must precede the test-mode check
 ├─ test_mode_check_trigger()             // NVS request flag, or TEST_MODE_GPIO
 │     └─ test_mode_enter()  → never returns
 ├─ test_mode_start_longpress_watchdog()  // BOOT 3 s → set flag → reboot
 ├─ node_config_init() / node_config_load()
 ├─ event_bus_init()
 ├─ device_type_registry_init() + peripherals_register_all()
 ├─ device_manager_init() / device_manager_load()
 ├─ mfg_provision_has_data() → mfg_provision_load()
 ├─ wifi_prov_init() + wifi_prov_start()
 │     ├─ pre-provisioned Wi-Fi?  → join directly, skip provisioning
 │     └─ otherwise                → SoftAP/BLE provisioning service
 ├─ cert_manager_init() + web_server_start()   ← UI reachable on the SoftAP
 ├─ wifi_prov_wait_for_connection()
 ├─ led_driver_init()
 ├─ mqtt_client_init() + mqtt_commander_init() + mqtt_publisher_init()
 ├─ mqtt_client_start() + mqtt_publisher_start()
 ├─ ota_service_init() + ota_service_mark_valid()
 └─ event_bus_publish(EVENT_NODE_READY) → idle loop
```

Two ordering constraints matter and both were bugs during development:

1. **NVS before the test-mode check.** `test_mode_check_trigger()` reads an NVS
   flag; before `nvs_flash_init()` that read fails silently and the long-press
   trigger does nothing.
2. **The watchdog starts before the Wi-Fi wait.** `wifi_prov_start()` leads into a
   blocking wait, so a watchdog started after it never runs on an unprovisioned
   device — exactly the factory case.

The HTTPS server starts **before** the station connects so the configuration UI is
reachable through the provisioning SoftAP (`https://192.168.4.1/`). It keeps
running afterwards. See limitation #1 about the missing authentication.

---

## 10. Known limitations

| # | Limitation | Impact | Mitigation |
|---|---|---|---|
| 1 | **No authentication on the REST API** | anyone who can reach the device over the network can actuate outputs, change MQTT config, or trigger OTA | put it on a trusted segment; add HTTP auth before exposing. Recommended: an HTTP Basic / bearer token check in `web_server.c` |
| 2 | Self-signed certificate | browser warnings; no server identity | expected; replace with a real certificate via `tools/gen_certs.sh` and the `certs` partition for a PKI |
| 3 | OTA status is read without a lock | a torn read can briefly show an inconsistent progress value | cosmetic only |
| 4 | MQTT broker defaults to a public test broker | the device will connect there if no broker is configured | always set `mqtt_broker` via Web/factory data |
| 5 | `dht11` blocks ~20 ms with interrupts disabled while reading | a slight jitter on other peripherals | acceptable at a 5 s interval |
| 6 | `shiftreg_595` uses bare GPIO toggling | no explicit setup/hold delays; verified working on this board, but long wires/many chips may need SPI or added delays | use SPI if you cascade beyond 4 chips |
| 7 | Only one I²C/SPI bus abstraction | new buses need a driver | — |
| 8 | Test console assumes the auto-reset USB-serial wiring | `tools/espx_test.py` drives DTR/RTS | use a manual reset if your adapter differs |
| 9 | The config UI is reachable over the open provisioning SoftAP | anyone in radio range can reconfigure the device during provisioning | provision at a controlled location, or set `wifi_ssid` via factory data so the SoftAP never starts |

---

## 11. Source map

```
main/
├── app_main.c                 boot sequence
├── CMakeLists.txt             source list + EMBED_FILES
├── Kconfig.projbuild          ESPX configuration
├── core/
│   ├── device_type.{c,h}      driver registry
│   ├── device_manager.{c,h}   runtime device instances + NVS persistence
│   ├── event_bus.{c,h}        pub/sub
│   └── node_config.{c,h}      node identity + network config
├── peripherals/
│   ├── peripherals.c          registers every driver
│   ├── dht11.{c,h}
│   ├── button.{c,h}
│   ├── relay.{c,h}
│   ├── shiftreg_595.{c,h}
│   └── ws2812.{c,h}
├── mqtt_client/
│   ├── espx_mqtt_client.{c,h} connect, topic prefix, subscribe
│   ├── mqtt_commander.{c,h}   inbound commands
│   └── mqtt_publisher.{c,h}   heartbeat + periodic sensors
├── web_server/
│   ├── web_server.c           HTTPS + REST
│   └── web_files/index.html   single-page UI (embedded)
├── cert_manager/
│   ├── cert_manager.{c,h}
│   └── certs/server.{crt,key} build-time embedded PEM files
├── wifi_prov/                 SoftAP/BLE provisioning
├── ota_service/               delta OTA
├── mfg_provision/             factory data
└── test_mode/                 self-test console

components/led_driver/         optional single status LED (off by default)
tools/
├── gen_certs.sh               regenerate server certificate
├── make_delta_patch.py        build a delta OTA patch
├── espx_test.py               automated serial test harness
├── mini_mqtt_broker.py        dependency-free MQTT broker for local testing
└── verify_device.sh           flash / monitor / log capture helpers
```
