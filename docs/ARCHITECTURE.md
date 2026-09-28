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
   Web browser ──┤  WebSocket  /ws  (live state, no polling)    │
                 ├──────────────────────────────────────────────┤
   LAN ──────────┤  mDNS  <prefix><mac>.local                   │
                 │  NTP   clock sync                            │
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
                 │  ws2812 · tja1050 (CAN) · mcp4725 (DAC)     │
                 │  ads1115 (ADC) · ina226 (power) · buzzer   │
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

### 2.5 Module layering — dependency direction

Dependencies point strictly downward; a lower module never includes a business
module above it.

```
config_apply → yaml | node_config | device_manager | event_bus
device_manager → device_type | event_bus | node_config
peripherals/* → device_type | device_manager
utils/* → (libc, cJSON, NVS)    # no reverse dependency on business modules
```

* `config/yaml.c` depends only on cJSON and libc — it must stay host-testable,
  with no ESP-IDF dependency.
* The channel layer (MQTT / HTTPS / console) holds no config
  semantics: it parses, calls `config_apply()`, and reports the result.
* Drivers never depend back on `device_manager`; the manager calls them through
  function pointers and only needs the `device_t` definition.
* `utils/` depends on nothing above it — only libc, cJSON and NVS.

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

### 4.3 WebSocket

`/ws` (under the same HTTPS server) pushes state so a UI does not poll.

Server → client: `hello` (identity, time, mDNS name), `state` (all devices with
values), `result`, `config_result`, `error`, `pong`.

Client → server: `ping`, `refresh`, `read`, `write`, `config` (YAML document).

A push is triggered immediately by `EVENT_DEVICE_VALUE_CHANGED` and the other
device events; otherwise a 1 s tick re-evaluates. An identical payload is
skipped, so an idle node produces no traffic.

Requires `CONFIG_HTTPD_WS_SUPPORT`. HTTP requests and WebSocket clients share
the server's socket pool (`max_open_sockets`, default 7).

### 4.4 Serial test console

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

## 6.1 Wi-Fi provisioning

Default transport is **BLE** (`CONFIG_ESPX_PROV_TRANSPORT_BLE`):

* No access point is created, so the device exposes no open network and has no
  IP until it joins the configured Wi-Fi.
* Service name is `ESPX_<last 3 MAC bytes>`; security is X25519 + PoP
  (`CONFIG_ESPX_PROV_POP`).
* The console prints a QR payload:
  `{"ver":"v1","name":"ESPX_772E74","username":"","pop":"abcd1234","transport":"ble"}`

SoftAP transport remains selectable (`CONFIG_ESPX_PROV_TRANSPORT_SOFTAP`) for
situations with no BLE-capable client.

Once it has an address, the node is reachable over mDNS under its device id
(lower-cased, `[a-z0-9-]` only — e.g. `espx-84c7bb772e74.local`), which is the
same string as the MQTT topic prefix and the device id, advertising `_https._tcp` with TXT records
`id`/`model`/`version`, so a client learns the identity before the first request.

**Consequence for bring-up:** an unprovisioned BLE-only device is unreachable
over the network — neither the HTTPS API, MQTT, mDNS nor NTP is available until
it joins Wi-Fi. For bench testing without a phone app, either provision over BLE
or use the pre-provisioned path in §7.1.

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
 ├─ event_bus_publish(EVENT_NODE_READY) → idle loop
 └─ (background) wifi_status_task:
        wifi_prov_wait_for_connection()  → logs, sets the status LED
        net_services_start()             → NTP + mDNS (need an IP)
```

Two ordering constraints matter and both were bugs during development:

1. **NVS before the test-mode check.** `test_mode_check_trigger()` reads an NVS
   flag; before `nvs_flash_init()` that read fails silently and the long-press
   trigger does nothing.
2. **The watchdog starts before the Wi-Fi wait.** `wifi_prov_start()` leads into a
   blocking wait, so a watchdog started after it never runs on an unprovisioned
   device — exactly the factory case.

The HTTPS server starts **before** the station connects so the configuration UI is
reachable through the provisioning SoftAP (`https://192.168.4.1/`, SoftAP
transport only). It keeps running afterwards. See limitation #1 about the
missing authentication.

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
| 9 | With the **SoftAP** transport the config UI is reachable over the open provisioning AP | anyone in radio range can reconfigure the device during provisioning | default is BLE (no AP); set `wifi_ssid` via factory data so no provisioning AP is ever raised |
| 10 | The BLE PoP is compiled in and defaults to `abcd1234` | the shared secret is the same on every unit | set `CONFIG_ESPX_PROV_POP` per production batch, and `CONFIG_ESPX_PROV_SHOW_POP_IN_QR=n` so printed QR codes omit it |
| 11 | Internal RAM is only 345 KB and concurrent TLS sessions consume it | exhausting it makes the HTTPS server refuse every connection until reboot | see §12: 4 sockets, 4 KB TLS buffers, mbedTLS buffers in PSRAM |

---

## 12. Memory and TLS sizing

The figures below are measured on the target and are the reason for several
non-default settings.

| Quantity | Value |
|---|---|
| Internal RAM (total) | **345 KB** — the pool that runs out |
| Internal RAM free after boot | **170 KB (51% used)** |
| Internal RAM per TLS session | ~25 KB |
| Lowest internal free under 16 concurrent requests | **~123 KB** |
| Largest allocatable internal block | 88–92 KB |
| PSRAM in use | ~1% (task stacks, Wi-Fi/LWIP buffers) |
| TLS handshake, ECDSA P-256 @240 MHz | ~0.5 s |
| Subsequent request on the same connection | 12–30 ms |
| TLS handshake with RSA-2048 | ~1.5 s |

Three deliberate choices follow from this:

1. **ECDSA P-256 certificate** (`tools/gen_certs.sh` default). An RSA-2048
   private-key operation is orders of magnitude slower; it is the difference
   between a responsive UI and a sluggish one.
2. **TLS buffers in PSRAM** (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`) with
   `MBEDTLS_SSL_IN_CONTENT_LEN` reduced to 4 KB. The 16 KB default is sized for
   bulk transfer; this node serves small JSON.
3. **`max_open_sockets = 4`.** Every socket is a whole TLS session. When internal
   RAM runs out the failure is not graceful: `mbedtls_ssl_setup` returns
   `PSA_ERROR_INSUFFICIENT_MEMORY` and the server refuses *every* subsequent
   connection until reboot.

Besides those, internal RAM is reclaimed by moving Wi-Fi/LWIP buffers to PSRAM
(`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), moving the stacks of **flash-free**
tasks to PSRAM (`common/task_util.h`), trimming Wi-Fi buffer counts and LwIP
windows, and enabling `MBEDTLS_DYNAMIC_BUFFER`. Two constraints are easy to get
wrong and are documented in `docs/DEVELOPMENT.md` §5: a task with a PSRAM stack
must never perform a flash operation, and
`MBEDTLS_DYNAMIC_FREE_CONFIG_DATA` must not be enabled on a TLS **server**.

`/api/system/info` and the WebSocket `state` message both report
`ram.internal_free`, `ram.internal_min_free` (low-water mark) and `cpu.usage`.
Only the internal figure is meaningful for capacity planning: the combined
`free_heap` includes 8 MB of PSRAM and stays near-empty regardless.

## 13. Source map

```
main/
├── app_main.c                 boot sequence
├── CMakeLists.txt             source list + EMBED_FILES
├── Kconfig.projbuild          ESPX configuration
├── utils/
│   ├── json_utils.{c,h}       cJSON helpers
│   ├── str_utils.{c,h}        string helpers
│   └── nvs_utils.{c,h}        NVS read/write helpers
├── config/
│   ├── node_config.{c,h}      node identity + network config
│   ├── config_apply.{c,h}     config semantics (single entry point)
│   └── yaml.{c,h}             YAML subset → cJSON (no ESP-IDF)
├── device/
│   ├── device_type.{c,h}      driver registry
│   ├── device_manager.{c,h}   runtime device instances + NVS persistence
│   └── event_bus.{c,h}        pub/sub
├── common/
│   ├── app_info.h             build identity (single version source)
│   ├── defaults.{c,h}         seed factory defaults once
│   ├── sys_stats.{c,h}        CPU load + RAM sampling
│   ├── sys_info.{c,h}         node status snapshot (HTTP + MQTT)
│   └── task_util.h            PSRAM task stack helpers
├── peripherals/
│   ├── peripherals.c          registers every driver
│   ├── dht11.{c,h}
│   ├── button.{c,h}
│   ├── relay.{c,h}
│   ├── shiftreg_595.{c,h}
│   ├── ws2812.{c,h}
│   ├── tja1050.{c,h}
│   ├── mcp4725.{c,h}
│   ├── ads1115.{c,h}
│   ├── ina226.{c,h}
│   ├── buzzer.{c,h}
│   └── esp_idf_i2c.{c,h}
├── mqtt_client/
│   ├── espx_mqtt_client.{c,h} connect, topic prefix, subscribe
│   ├── mqtt_commander.{c,h}   inbound commands
│   └── mqtt_publisher.{c,h}   heartbeat + periodic sensors
├── web_server/
│   ├── web_server.c           HTTPS server lifecycle + REST dispatch
│   ├── ws_server.{c,h}        WebSocket /ws (live push + commands)
│   ├── handlers/              one module per API area
│   │   ├── handlers.h         register API + shared response helpers
│   │   ├── handlers_common.c  shared response helpers
│   │   ├── node_handler.c     node endpoints
│   │   ├── network_handler.c  network endpoints
│   │   ├── config_handler.c   config endpoints
│   │   ├── device_handler.c   device/peripheral endpoints (wildcard last)
│   │   ├── system_handler.c   system endpoints
│   │   ├── ota_handler.c      OTA endpoints
│   │   ├── cert_handler.c     certificate endpoints
│   │   └── wifi_handler.c     Wi-Fi endpoints
│   └── web_files/index.html   single-page UI (embedded)
├── cert_manager/
│   ├── cert_manager.{c,h}
│   └── certs/server.{crt,key} build-time embedded PEM files
├── net_services/
│   ├── net_services.{c,h}     starts the IP-dependent services
│   ├── time_sync.{c,h}        NTP
│   └── mdns_service.{c,h}     mDNS / DNS-SD
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
├── emqx_init.py               EMQX authenticator/user/ACL setup + MQTT verify
├── network_tests.py           HTTPS/MQTT/YAML/OTA integration tests
└── verify_device.sh           flash / monitor / log capture helpers
```
