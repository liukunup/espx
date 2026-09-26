# ESPX — a software-defined IoT node

English | [中文](README.zh-CN.md)

Generic node firmware for the ESP32-S3 (R16N8: 16 MB flash, 8 MB PSRAM).

**The node *is* its configuration.** The firmware has no knowledge of any
particular product; what the node does is decided entirely at runtime by its
configuration — peripheral types, counts, pins and parameters are all
configurable and persisted in NVS.

```
        firmware (fixed)                 configuration (variable)
  ┌──────────────────────┐        ┌──────────────────────────┐
  │  device driver       │        │  devices:                │
  │  catalogue           │        │    - id: temp_in         │
  │   dht11 / button     │        │      type: dht11         │
  │   relay / 74hc595    │   +    │      config: {gpio: 4}   │
  │   ws2812             │        │    - id: relay_a         │
  │                      │        │      type: relay         │
  │  MQTT / HTTPS / OTA  │        │      config: {gpio: 5}   │
  │  AT / WS / mDNS/NTP  │        │  network: {…}            │
  └──────────────────────┘        └──────────────────────────┘
              └──────────────►  one concrete node
```

The binding statement of intent lives in [AGENT.md](AGENT.md).

---

## Features

| Capability | Notes |
|---|---|
| **Software-defined peripherals** | Bindings are configuration; changing the hardware means changing a document, not compiling a different firmware |
| **Four configuration channels** | MQTT (YAML), HTTPS `POST /api/config`, serial AT commands, factory data — all through one `config_apply()` path |
| **MQTT** | Periodic state/sensor publishing; commands for query, control, config, reboot, OTA, test mode |
| **HTTPS management UI** | Self-signed certificate kept as project files and embedded at build time; single-page UI, REST API, **WebSocket live push** |
| **Delta OTA** | `esp_delta_ota` + heatshrink; typically 95 %+ smaller than a full image, with base-image verification |
| **BLE provisioning** | BLE by default (no open AP exposed); SoftAP available; pre-provisioned credentials supported |
| **mDNS** | Reachable as `espx-<mac>.local`, no IP hunting |
| **NTP** | Clock sync — needed for certificate validity and correlatable logs |
| **Serial AT commands** | ESP-AT style command set so a host MCU can drive the node |
| **Manufacturing test mode** | Hold BOOT for 3 s; interactive hardware self-test |
| **Factory provisioning** | One-shot `mfg_data` partition: identity, network, MQTT, peripheral bindings |
| **Default binding** | A fresh node binds its on-board WS2812 (GPIO48) as device `led`, controllable from the UI/MQTT/AT; seeded exactly once, so deleting it sticks |
| **System monitoring** | Dashboard shows CPU load, internal RAM / PSRAM usage (pressure-coloured bars) and task count |

---

## Peripheral drivers

| Type | Capabilities | Config keys |
|---|---|---|
| `dht11` | read, periodic | `gpio`, `interval_ms` |
| `button` | read, notify | `gpio`, `active_level`, `pullup` |
| `relay` | read, write, notify | `gpio`, `active_level` |
| `shiftreg_595` | read, write, notify | `data_gpio`, `clock_gpio`, `latch_gpio`, `oe_gpio`, `count` (1–8 cascaded) |
| `ws2812` | read, write, notify | `data_gpio`, `count` (1–300), `brightness` |
| `can` | read, write, notify | `tx_gpio`, `rx_gpio`, `bitrate` (125k/250k/500k/1M), `tx_queue_size`, `rx_queue_size` |
| `mcp4725` | write, read | `sda_gpio`, `scl_gpio`, `i2c_addr`, `vref_mv`, `scl_freq` |
| `ads1115` | read, periodic, notify | `sda_gpio`, `scl_gpio`, `i2c_addr`, `channel` (0–3), `gain`, `rate` (SPS), `interval_ms` |
| `ina226` | read, periodic, notify | `sda_gpio`, `scl_gpio`, `i2c_addr`, `r_shunt` (mΩ), `max_current_ma`, `interval_ms` |
| `buzzer` | write, read | `gpio`, `frequency` (Hz), `duty` (%), `auto_off_ms` |

Adding hardware means writing one `device_type_t` driver and registering it —
the core is untouched.

---

## Quick start

```bash
# 1. environment
. <esp-idf>/export.sh
pip install pyserial 'detools>=0.49.0'

# 2. build
idf.py set-target esp32s3
idf.py build

# 3. flash
idf.py -p /dev/cu.usbserial-XXXX -b 460800 flash monitor
```

First boot enters **BLE provisioning**: the console prints the device name and a
QR code. Connect with the ESP BLE Prov app (default PoP is
`CONFIG_ESPX_PROV_POP`) and enter the Wi-Fi credentials.

Once connected:

```
I (…) wifi_prov: Got IP: 192.168.1.57
I (…) mdns: mDNS started: espx-84c7bb772e74.local  (https://espx-84c7bb772e74.local/)
I (…) time_sync: Time synchronised: 2026-09-26 18:04:05 +0800
I (…) mqtt_client: MQTT connected
```

Then open `https://espx-84c7bb772e74.local/` (accept the self-signed warning).

---

## Configure a node (YAML)

Over MQTT:

```bash
mosquitto_pub -h <broker> -t 'espx-84c7bb772e74/cmd/config' -f node.yaml
mosquitto_sub -h <broker> -t 'espx-84c7bb772e74/config/result' -C 1
```

`node.yaml`:

```yaml
node:
  name: Line-1 node

network:
  mqtt_broker: mqtt://192.168.1.10:1883
  mqtt_topic_prefix: plant/line1
  # optional: pre-provision Wi-Fi and skip the provisioning service entirely
  wifi_ssid: PlantNet
  wifi_password: secret

devices:
  - id: temp_in                 # temperature / humidity
    type: dht11
    config: {gpio: 4, interval_ms: 5000}

  - id: relay_a                 # relay
    type: relay
    config: {gpio: 5, active_level: 1}

  - id: out16                   # two cascaded 74HC595 = 16 outputs
    type: shiftreg_595
    config: {data_gpio: 16, clock_gpio: 17, latch_gpio: 18, count: 2}

  - id: strip                   # WS2812 strip, 8 pixels
    type: ws2812
    config: {data_gpio: 48, count: 8, brightness: 128}

  - id: can_bus                  # TJA1050 CAN bus
    type: can
    config: {tx_gpio: 6, rx_gpio: 7, bitrate: 500000}

  - id: dac_out                  # MCP4725 DAC
    type: mcp4725
    config: {sda_gpio: 10, scl_gpio: 11, vref_mv: 3300}

  - id: adc_ch0                   # ADS1115 ADC
    type: ads1115
    config: {sda_gpio: 10, scl_gpio: 11, channel: 0, gain: 1, rate: 4, interval_ms: 1000}

  - id: power_meter               # INA226 power monitor
    type: ina226
    config: {sda_gpio: 10, scl_gpio: 11, r_shunt: 10, max_current_ma: 1000, interval_ms: 1000}

  - id: buzzer1                  # Passive buzzer
    type: buzzer
    config: {gpio: 21, frequency: 2000, duty: 50}

remove_devices: [old_sensor]
replace_devices: false          # true also removes devices not listed above
```

The same document also works over HTTPS and AT:

```bash
curl -k -X POST https://espx-84c7bb772e74.local/api/config \
     -H 'Content-Type: text/yaml' --data-binary @node.yaml

AT+CFG=network: {mqtt_broker: "mqtt://192.168.1.10:1883"}
```

---

## Common operations

```bash
HOST=espx-84c7bb772e74.local

curl -k https://$HOST/api/devices                                    # list + live values
curl -k -X POST https://$HOST/api/devices/relay_a/write -d 'true'   # actuate
curl -k https://$HOST/api/config                                    # export config
curl -k https://$HOST/api/system/info                               # time, heap, Wi-Fi, mDNS
```

```bash
PREFIX=plant/line1
mosquitto_pub -h <broker> -t "$PREFIX/cmd/control/relay_a" -m '{"action":"set","value":true}'
mosquitto_pub -h <broker> -t "$PREFIX/cmd/query/temp_in"   -m '{"action":"get"}'
```

Serial AT (UART1, TX=GPIO17, RX=GPIO18, 115200):

```
AT+GMR                  version information
AT+ID                   device id and name
AT+CWJAP="ssid","pass"  join Wi-Fi and reboot
AT+CIFSR                query IP
AT+CFG?                 export the configuration
AT+CFG=<yaml|json>      apply a configuration document
AT+DEV?                 list devices
AT+DEV="relay_a",true   actuate a device
AT+SYSTIME?             NTP time
AT+MQTTCONN="host",1883,"user","pass"
AT+OTASTART="http://…/fw.patch"
AT+TESTMODE             reboot into the manufacturing self-test
AT+HELP?                full command list
```

---

## Delta OTA

```bash
# The base MUST be the firmware the device is currently running.
tools/make_delta_patch.py --base build/espx-v1.0.0.bin \
                          --new  build/espx-v1.0.1.bin \
                          --out  dist/v1.0.0_to_v1.0.1.patch

python3 -m http.server 8000 --directory dist
curl -k -X POST https://$HOST/api/ota/start \
     -H 'Content-Type: application/json' \
     -d '{"url":"http://192.168.1.20:8000/v1.0.0_to_v1.0.1.patch"}'
```

The patch header carries the base image's SHA-256; the device compares it
against the firmware it is currently running and rejects a mismatch, so a patch
built against the wrong base can never brick a node.

Measured: a 1.25 MB image yields a 20–75 KB patch (95–98 % smaller).

---

## Factory flow

```bash
idf.py erase-flash && idf.py flash

# hold BOOT for 3 s to enter test mode
cfg {network: {wifi_ssid: PlantNet, mqtt_broker: "mqtt://broker.plant:1883",
               mqtt_topic_prefix: plant/line1}}
mfg set {"node":{"device_id":"espx-0001"},
         "devices":[{"id":"temp_in","type":"dht11","config":{"gpio":4}}]}
test all        # hardware self-test, per-item PASS/FAIL
exit            # reboot and apply
```

---

## Documentation

| Document | Contents |
|---|---|
| [AGENT.md](AGENT.md) | Project direction and hard design constraints (**authoritative**) |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Architecture, data model, interfaces, OTA, partitions, limitations |
| [docs/USAGE.md](docs/USAGE.md) | Every configuration channel in detail, with examples |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Environment, adding a driver, conventions, debugging |
| [docs/TESTING.md](docs/TESTING.md) | Prioritised test cases with step-by-step verification |
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | Production flashing, EMQX setup, hardening, upgrades |

---

## Tools

| Tool | Purpose |
|---|---|
| `tools/espx_test.py` | Serial regression: boot info, test mode, hardware self-test |
| `tools/network_tests.py` | Network regression: HTTPS API, YAML config, MQTT, delta OTA |
| `tools/make_delta_patch.py` | Build a delta OTA patch and self-verify it |
| `tools/emqx_init.py` | EMQX authenticator/user/ACL setup with a real MQTT round-trip check |
| `tools/mini_mqtt_broker.py` | Dependency-free MQTT broker for local testing |
| `tools/gen_cert.py` | Generate HTTPS certificate from device ID |
| `tools/gen_certs.sh` | Regenerate the HTTPS certificate (manual CN) |
| `tools/verify_device.sh` | Flash / monitor / capture helpers |
| `tests/run_yaml_tests.sh` | Host unit tests for the YAML parser |

---

## Known limitations

1. **No authentication on the REST API** — anyone who can reach the node can
   actuate outputs, change MQTT settings or trigger an OTA. Add authentication
   before exposing it on an untrusted network.
2. Self-signed certificate is a **development default**; replace it in production.
3. The BLE PoP defaults to `abcd1234` and is printed in the QR code; rotate it per
   batch and disable QR embedding for production.
4. With the SoftAP transport the config UI is reachable over the open
   provisioning AP.
5. Wi-Fi credentials are stored in plaintext NVS.
6. The AT command set is a **subset** of ESP-AT, not a drop-in replacement.

The full list with mitigations is in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §10.

---

## License

Example code is in the Public Domain (or CC0 licensed, at your option).
