# ESPX Test Plan

Ordered by priority. Each case states **what it proves**, **how to run it**, and
**what to see**. Run them in order: later cases assume earlier ones pass.

Automated cases are driven by `tools/espx_test.py`; manual cases list the exact
commands.

---

## 0. Prerequisites

```bash
# 1. ESP-IDF environment
. <idf-path>/export.sh

# 2. Python tooling (once)
pip install pyserial 'detools>=0.49.0'

# 3. Find the port
ls /dev/cu.usb* /dev/tty.usb*        # macOS
# export ESPX_PORT=/dev/cu.usbserial-XXXX   to override the default
```

Baseline assumption for every case below:

| Item | Value |
|---|---|
| Board | ESP32-S3 R16N8 (16 MB flash, 8 MB PSRAM) |
| Console | UART0 @ 115200 on GPIO43/44 |
| BOOT button | GPIO0 |
| On-board RGB LED | WS2812 on GPIO48 |

---

## P0 — must pass before anything else

### T0.1 Build is clean

**Proves:** the tree compiles for the right target with no errors and no warnings.

```bash
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build 2>&1 | tee /tmp/build.log
```

**Expect**

```
espx.bin binary size 0x…… bytes. Smallest app partition is 0x400000 bytes. (7x% free)
Project build complete.
```

and

```bash
grep -i "warning:" /tmp/build.log | grep -v FATFS_PRINT_FLOAT   # → no output
python3 -c "print(open('sdkconfig').read().count('CONFIG_IDF_TARGET=\"esp32s3\"'))"  # → 1
```

**Fail if:** any `error:`, any `warning:` from our sources, or `CONFIG_IDF_TARGET`
is not `esp32s3` (this silently happens if `sdkconfig` is deleted and the target
is not pinned — it must be pinned in `sdkconfig.defaults`).

---

### T0.2 Flash and boot

**Proves:** the image is valid, the partition table is right, PSRAM works.

```bash
idf.py -p $ESPX_PORT -b 460800 flash
python3 tools/espx_test.py --port $ESPX_PORT info
```

**Expect** `RESULT` equivalent (all PASS):

```
  [PASS] chip is ESP32-S3
  [PASS] flash detected as 16MB
  [PASS] 8MB PSRAM detected
  [PASS] partition table has otadata
  [PASS] partition table has factory slot
  [PASS] application banner
```

**Fail if:** no boot banner (wrong baud / held in reset), `Found 8MB PSRAM` missing
(PSRAM mode wrong — should be octal), or `otadata` absent (OTA cannot work).

---

### T0.3 No errors or warnings in the boot log

**Proves:** every subsystem initialises cleanly.

```bash
python3 tools/espx_test.py --port $ESPX_PORT console &   # Ctrl-C to stop
# or capture a bounded log:
./tools/verify_device.sh listen 20
```

**Expect** the boot log to contain, in order, and no `E (` lines:

```
I (…) app_main: Initializing NVS...
I (…) app_main: Initializing Wi-Fi provisioning...
I (…) wifi_prov: Provisioning service started. Connect and configure Wi-Fi.
```

**Fail if:** any `E (…)` line, a `rst:` reboot loop, or a `Guru Meditation`.

---

## P1 — core behaviour (no network needed)

### T1.1 Test mode is reachable by long-press

**Proves:** the manufacturing entry path works, including on an **unprovisioned**
device (the factory case). This regressed twice during development: the watchdog
must start *before* the blocking Wi-Fi wait, and NVS must be initialised *before*
the request flag is read.

```bash
python3 tools/espx_test.py --port $ESPX_PORT enter
```

**Expect**

```
  [PASS] test-mode banner
  [PASS] console prompt
```

**Fail if:** the device stays in the application, or you see
`Test-mode trigger GPIO disabled` followed by no test-mode banner (means the
watchdog started too late), or repeated `BOOT held … -> entering test mode` lines
without a reboot (means the trigger is not latched).

**Note:** boot-time GPIO triggering is disabled by default
(`CONFIG_MFG_TEST_GPIO=-1`) because GPIO0 is a strapping pin — holding it low
across reset puts the chip into the ROM UART download mode and the application
never runs. Use a free GPIO if you want a hardware trigger.

---

### T1.2 Device registry and hardware self-test

**Proves:** drivers load, configs validate, and every bound peripheral can be
read and actuated.

```bash
python3 tools/espx_test.py --port $ESPX_PORT selftest
```

**Expect**

```
Device registry
  [PASS] add ws_led (ws2812)
  [PASS] add relay1 (relay)
  [PASS] add sr1 (shiftreg_595)
  [PASS] add btn1 (button)
  [PASS] listed ws_led … sr1 … btn1
Hardware self-test
  ws_led     ws2812         -> PASS
  relay1     relay          -> PASS
  sr1        shiftreg_595   -> PASS
  btn1       button         -> PASS
```

**Also verify physically**

* `ws_led` — the RGB LED on GPIO48 flashes red briefly during the test.
* `relay1` — GPIO5 toggles (a meter/LED on GPIO5 shows a pulse).

**Fail if:** a device is `not initialized`, a write returns non-`ESP_OK`, or the
summary reports failures. Note that `dht11` legitimately reports `FAIL` when no
sensor is wired to its GPIO — that is the test working correctly.

---

### T1.3 Device configuration persists across reboot

**Proves:** NVS persistence and reload.

In the test console (or via the Web API):

```
add relay_x relay {"gpio":5,"active_level":1}
exit                      # reboots into the application
```

Then re-enter test mode and:

```
list
```

**Expect** `relay_x` to be listed with `INIT = yes` — it was restored from NVS and
re-initialised automatically.

**Fail if:** the list is empty (save failed), or `INIT = no` (re-init failed).

---

### T1.4 Invalid configuration is rejected

**Proves:** `validate_config` runs before the hardware is touched.

```
add bad ws2812 {"data_gpio":99,"count":1}
add bad2 relay {}
```

**Expect**

```
Failed: ESP_ERR_INVALID_ARG
```

and **no** `Relay initialized on GPIO…` line for `bad2`.

---

### T1.5 Factory provisioning applies once

**Proves:** factory data is applied, persisted, and consumed exactly once.

```
mfg set {"node":{"device_id":"espx-0001","name":"Line-1"},"network":{"mqtt_broker":"mqtt://192.168.1.10:1883"},"devices":[{"id":"temp_in","type":"dht11","config":{"gpio":4,"interval_ms":5000}}]}
mfg show          → Factory data present: yes
mfg apply         → Apply: OK
mfg show          → Factory data present: no
list              → temp_in listed
```

Then reboot and `mfg show` again — it must still be `no` (applied only once).

---

## P1b — new peripheral drivers

### T1.6 CAN (TJA1050) frame send and receive

**Proves:** TWAI controller initialises and can transmit/receive frames.

```bash
# Add CAN device via REST
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @-
<<'EOF'
devices:
  - id: can1
    type: can
    config: {tx_gpio: 6, rx_gpio: 7, bitrate: 500000}
EOF

# Send a frame
curl -k -X POST https://$HOST/api/devices/can1/write -d '{"id":123,"data":[1,2,3,4]}'
# Expect: 200 OK

# Read back (queue may be empty if no peer device)
curl -k https://$HOST/api/devices/can1
# If a frame was received: {"id":..., "data":[...], "ext":false, "rtr":false}
```

**Expect:** device added, TX returns 200 OK, log shows `TX id=0x7B dlc=4`.

---

### T1.7 MCP4725 DAC write and read

**Proves:** I2C DAC sets voltage correctly.

```bash
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @-
<<'EOF'
devices:
  - id: dac1
    type: mcp4725
    config: {sda_gpio: 10, scl_gpio: 11, vref_mv: 3300}
EOF

curl -k -X POST https://$HOST/api/devices/dac1/write -d '2048'
curl -k https://$HOST/api/devices/dac1
# Expect: {"value":2048,"voltage_mv":1650.00,"vref_mv":3300}
```

---

### T1.8 ADS1115 ADC periodic reading

**Proves:** I2C ADC reads voltage and publishes periodically.

```bash
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @-
<<'EOF'
devices:
  - id: adc1
    type: ads1115
    config: {sda_gpio: 10, scl_gpio: 11, channel: 0, gain: 1, interval_ms: 500}
EOF

curl -k https://$HOST/api/devices/adc1
# Expect: {"raw":..., "mv":...}
```

**Expect:** raw value is non-zero when analog input is connected.

---

### T1.9 INA226 power monitor reading

**Proves:** I2C power monitor reports V, I, P.

```bash
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @-
<<'EOF'
devices:
  - id: pwr1
    type: ina226
    config: {sda_gpio: 10, scl_gpio: 11, r_shunt: 10, max_current_ma: 1000, interval_ms: 500}
EOF

curl -k https://$HOST/api/devices/pwr1
# Expect: {"bus_voltage_mv":..., "shunt_voltage_uv":..., "current_ma":..., "power_mw":...}
```

---

### T1.10 Buzzer on/off and auto-off

**Proves:** LEDC PWM buzzer turns on, off, and auto-off timer works.

```bash
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @-
<<'EOF'
devices:
  - id: buzzer1
    type: buzzer
    config: {gpio: 21, frequency: 2000, duty: 50}
EOF

# Immediate on/off
curl -k -X POST https://$HOST/api/devices/buzzer1/write -d '{"on":true}'
curl -k https://$HOST/api/devices/buzzer1
# Expect: {"on":true, "frequency":2000, "duty":50, "remaining_ms":0}

curl -k -X POST https://$HOST/api/devices/buzzer1/write -d '{"on":false}'
curl -k https://$HOST/api/devices/buzzer1
# Expect: {"on":false,...}

# Auto-off after 200ms
curl -k -X POST https://$HOST/api/devices/buzzer1/write -d '{"on":true,"duration_ms":200}'
# Wait 300ms
curl -k https://$HOST/api/devices/buzzer1
# Expect: {"on":false,...}
```

---

## P2 — network

### T2.1 Wi-Fi provisioning (BLE, default)

**Proves:** the BLE provisioning path and credential storage.

1. Reboot the device. The log must show BLE advertising and **no** SoftAP:

```
I (…) BLE_INIT: Bluetooth MAC: 84:c7:bb:77:2e:76
I (…) NimBLE: GAP procedure initiated: advertise;
I (…) network_prov_mgr: Provisioning started with service name : ESPX_772E74
I (…) QRCODE: {"ver":"v1","name":"ESPX_772E74","username":"","pop":"abcd1234","transport":"ble"}
```

   There must be **no** `mode : sta (…) + softAP (…)` and no
   `DHCP server started on interface WIFI_AP_DEF`.

2. Provision with the **ESP BLE Prov** app: scan for `ESPX_772E74`, enter the PoP
   (`abcd1234`), then the Wi-Fi SSID and password. (Or scan the printed QR code
   with the ESP SoftAP/BLE Prov app.)
3. **Expect** the serial log:

```
I (…) wifi_prov: Received credentials: SSID=…
I (…) wifi_prov: Provisioning successful
I (…) wifi_prov: Got IP: 192.168.x.y
I (…) app_main: Wi-Fi connected
```

4. Reboot: it must reconnect **without** re-provisioning (`Already provisioned,
   starting Wi-Fi STA`).

**Fail if:** no IP, provisioning restarts on every boot, or a SoftAP is created
(that means the transport is still SoftAP).

> **Bench note:** an unprovisioned BLE-only device has no IP, so the HTTPS and
> MQTT cases below cannot run until Wi-Fi is joined. Either provision over BLE,
> or set the credentials as factory data (see T2.1b), which is the scriptable
> path.

### T2.1b Pre-provisioned Wi-Fi (scriptable, no phone needed)

**Proves:** a factory-programmed unit joins the plant network unattended, and it
gives the test harness a device with an IP.

Enter test mode (T1.1) and set the credentials, then reboot into the app:

```
cfg network:
  wifi_ssid: <your-ssid>
  wifi_password: <your-password>
  mqtt_broker: mqtt://<your-host-ip>:1883
  mqtt_topic_prefix: plant/line1
exit
```

**Expect**

```
I (…) wifi_prov: Using pre-provisioned Wi-Fi credentials for SSID '<your-ssid>'
I (…) wifi_prov: Got IP: 192.168.x.y
I (…) app_main: Wi-Fi connected
I (…) mqtt_client: MQTT connected
```

and **no** provisioning service start.

---

### T2.2 HTTPS server and Web UI

**Proves:** the embedded certificate loads and the UI/API are served.

```bash
IP=192.168.x.y

# certificate is served and valid
curl -k -s https://$IP/api/node | python3 -m json.tool

# no auth: open the UI in a browser and accept the self-signed warning
open https://$IP/
```

**Expect** `/api/node`:

```json
{ "device_id": "espx-84C7BB772E74", "name": "ESPX",
  "version": "1.0.0", "device_count": 1 }
```

and the UI to show the **Dashboard / Devices / Network / Firmware / System** tabs.

**Fail if:** connection refused (web server failed to start — check for
`Failed to start HTTPS` in the log), or a TLS handshake failure (bad embedded key;
regenerate with `tools/gen_certs.sh` and rebuild).

---

### T2.3 REST API: device CRUD and actuation

**Proves:** the wildcard dispatcher and the device manager agree.

```bash
IP=192.168.x.y

# add
curl -k -s -X POST https://$IP/api/devices -H 'Content-Type: application/json' \
     -d '{"id":"r1","type":"relay","config":{"gpio":5,"active_level":1}}'

# list with live value
curl -k -s https://$IP/api/devices | python3 -m json.tool

# write / read
curl -k -s -X POST https://$IP/api/devices/r1/write -H 'Content-Type: application/json' -d 'true'
curl -k -s -X POST https://$IP/api/devices/r1/read

# disable / enable
curl -k -s -X POST https://$IP/api/devices/r1/enable -H 'Content-Type: application/json' -d '{"enabled":false}'

# delete
curl -k -s -X DELETE https://$IP/api/devices/r1
```

**Expect** each call to return `"success": true` (or the requested value), and
GPIO5 to actually change when writing.

**Fail if:** `404` for `/read` or `/write` — that means the wildcard dispatcher is
missing or a mid-path wildcard was used (the ESP-IDF server only supports a
trailing `*`).

---

### T2.4 MQTT publish and command handling

**Proves:** both directions of the MQTT integration.

Use any broker client (e.g. `mosquitto_sub`/`mosquitto_pub`) against the broker
configured in `/api/network`.

```bash
PREFIX=espx/84C7BB772E74          # value of mqtt_topic_prefix

# observe
mosquitto_sub -h <broker> -t "$PREFIX/#" -v
```

**Expect on connect**

```
<prefix>/state      {"online":true,"device_id":"espx-…"}
```

then every 30 s a heartbeat, and every 10 s

```
<prefix>/sensors    {"temp_in":{"temperature":23.5,"humidity":41}}
```

**Send a command**

```bash
mosquitto_pub -h <broker> -t "$PREFIX/cmd/control/r1" -m '{"action":"set","value":true}'
mosquitto_pub -h <broker> -t "$PREFIX/cmd/query/r1"   -m '{"action":"get"}'
mosquitto_pub -h <broker> -t "$PREFIX/cmd/config"     -m '{"action":"get_devices"}'
```

**Expect** the relay to switch, and `attrs/…` messages in the `mosquitto_sub`
window.

**Fail if:** no `state` message (broker unreachable — check the log for
`MQTT connected`), or commands are ignored (subscription/topic-prefix mismatch).

---

### T2.5 mDNS discovery

**Proves:** the node is reachable by name and advertises what it offers.

```bash
# macOS / Linux with avahi
ping -c 3 espx-<mac>.local
curl -k https://espx-<mac>.local/api/node | python3 -m json.tool

# browse the advertised services
dns-sd -B _https._tcp local.        # macOS
avahi-browse -rt _https._tcp        # Linux
```

**Expect** the name to resolve and `_https._tcp` to be listed with the TXT
records `id`, `model`, `version`.

**Fail if:** the name does not resolve (`I (…) mdns: mDNS started: …` must be in
the log), or the node has no IP (mDNS starts only after the connection).

### T2.6 NTP clock

**Proves:** the clock is set, which TLS certificate validation depends on.

```bash
curl -k https://$IP/api/system/info | python3 -c '
import json,sys; d=json.load(sys.stdin)
print("time_synced", d["time_synced"], "time", d["time"], "epoch", d["epoch"])
print("ntp", d["ntp_server"], "tz", d["timezone"])'
```

**Expect**

```
time_synced True time 2026-09-26T18:09:00+0800 epoch 1790419739
ntp pool.ntp.org tz CST-8
```

and in the log `I (…) time_sync: Time synchronised: …`.

**Fail if:** `time` is `null` after ~60 s (check the node can reach the NTP
server; some networks block outbound UDP 123).

### T2.7 WebSocket live push

**Proves:** the UI receives state without polling, and commands work over the
socket.

```bash
python3 - <<'EOF'
import json, ssl, websocket          # pip install websocket-client
ctx = ssl.create_default_context(); ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
ws = websocket.create_connection("wss://<device-ip>/ws", ssl=ctx)
for _ in range(3):
    print(json.loads(ws.recv())["type"])
ws.send(json.dumps({"type":"read","id":"relay_a"}))
print(ws.recv())
ws.send(json.dumps({"type":"write","id":"relay_a","value":True}))
print(ws.recv())
ws.close()
EOF
```

**Expect** `hello` then `state`, and a `result` after each command.

Also confirm the push is **event driven**: with the socket open, flip a relay
through the REST API and the `state` message must arrive immediately (not after
the 1 s tick).

**Fail if:** the handshake fails (check `CONFIG_HTTPD_WS_SUPPORT=y`), or the UI
shows `poll` instead of `live`.

---

## P3 — delta OTA

### T3.1 Patch generation and format

**Proves:** the generator produces the exact header the firmware expects, and the
patch reconstructs the new image.

```bash
cp build/espx.bin dist/base.bin
# make a "new" firmware: rebuild after any change, or for a quick check:
python3 - <<'EOF'
b=bytearray(open('dist/base.bin','rb').read())
for i in range(0x20000,0x20100): b[i]^=0x5A
open('dist/new.bin','wb').write(b)
EOF

python3 tools/make_delta_patch.py --base dist/base.bin --new dist/new.bin --out dist/t.patch
```

**Expect**

```
base digest : f19d8f70…                        (matches esptool "Validation hash")
patch       : dist/t.patch (19xxx bytes, 98.4% smaller)
verify      : OK (patch reconstructs the new image exactly)
```

**Fail if:** `verify` is not OK, or the digest does not match
`python3 -m esptool --chip esp32s3 image_info dist/base.bin | grep Validation`.

---

### T3.2 A patch for the wrong base is rejected

**Proves:** the safety property — a mismatched patch cannot brick the device.

```bash
# build a patch from an unrelated base
python3 tools/make_delta_patch.py --base dist/new.bin --new dist/base.bin --out dist/wrong.patch
python3 -m http.server 8000 --directory dist &
curl -k -s -X POST https://$IP/api/ota/start -H 'Content-Type: application/json' \
     -d '{"url":"http://<your-ip>:8000/wrong.patch"}'
```

**Expect** `GET /api/ota/status` to show

```json
{ "state": "FAILED", "error": "patch was built for a different firmware version" }
```

and the device to keep running the current firmware. **No reboot, no brick.**

---

### T3.3 Successful delta OTA

**Proves:** the full update path, including the boot-slot switch and rollback
cancellation.

```bash
# 1. record the running version and the server-side patch
curl -k -s https://$IP/api/ota/status | grep running_version

# 2. make a visible change so the new firmware is identifiable, e.g.
#    bump CONFIG_FIRMWARE_VERSION to 1.0.1, rebuild, and keep BOTH binaries:
#      dist/base.bin  = firmware currently on the device
#      dist/new.bin   = the rebuilt 1.0.1 image
python3 tools/make_delta_patch.py --base dist/base.bin --new dist/new.bin --out dist/up.patch

# 3. serve and apply
python3 -m http.server 8000 --directory dist &
curl -k -s -X POST https://$IP/api/ota/start -H 'Content-Type: application/json' \
     -d '{"url":"http://<your-ip>:8000/up.patch"}'

# 4. watch progress
watch -n1 "curl -k -s https://$IP/api/ota/status"
```

**Expect**

```
CONNECTING → VERIFYING → DOWNLOADING (progress rising) → APPLYING → REBOOTING
```

the device reboots, and after the reboot

```
I (…) boot: Loaded app from partition at offset 0x420000      ← ota_0
I (…) app_init: App version: 1.0.1
```

`/api/ota/status` then reports `running_version: 1.0.1`, and the log shows the
firmware being marked valid (`Bootloader rollback … mark_app_valid` /
`Firmware marked valid, rollback cancelled`).

**Fail if:** it reboots back into the old version (rollback fired → the new
firmware crashed before `ota_service_mark_valid()`), or `esp_ota_set_boot_partition`
fails (missing `otadata` partition).

---

### T3.4 Settings survive the OTA

**Proves:** NVS is not touched by an application update.

Before T3.3, note `/api/devices`. After the OTA, re-check it — devices, node
identity and MQTT settings must be unchanged.

---

## P4 — robustness

### T4.1 Crash recovery / rollback

**Proves:** rollback works when the new firmware is defective.

1. Build a deliberately broken image (e.g. `abort()` early in `app_main` **before**
   `ota_service_mark_valid()`), generate the patch, apply it.
2. **Expect** a reboot loop for the configured attempts, then the bootloader to
   fall back to the previous slot and the device to come back up on the old
   version.

### T4.2 Power loss during OTA

**Proves:** a partial write never activates.

Interrupt power mid-`DOWNLOADING`, restore it.

**Expect** the device boots the old firmware; `otadata` was never switched.
Re-running the OTA succeeds.

### T4.3 Wi-Fi loss and reconnect

**Proves:** the MQTT client recovers.

Power off the AP for ~1 minute.

**Expect** `MQTT disconnected`, then after the AP returns
`MQTT connected` and subscriptions restored, without a manual reboot.

### T4.4 Unknown device type / bad JSON

```
add x nosuchtype {}
add y relay {bad json}
```

**Expect** `Failed: ESP_ERR_NOT_FOUND` and `Invalid JSON config`; the console stays
usable (no crash).

---

## P5 — production

### T5.1 Factory flow end-to-end

1. `idf.py erase-flash` (blank device).
2. `idf.py flash`.
3. Hold BOOT 3 s → test mode.
4. `mfg set {…}` with this unit's configuration.
5. `test all` → all PASS.
6. `mfg apply`, then `exit`.
7. Device reboots, applies factory data, connects to the configured Wi-Fi/MQTT,
   and appears in the broker under its assigned topic prefix.

**This is the acceptance test for a production unit.**

### T5.2 Test-mode entry also works from the network

With the device in the application and on the network:

```bash
curl -k -s -X POST https://$IP/api/system/testmode
```

**Expect** the serial console to drop into `MANUFACTURING TEST MODE`.

---

## Quick regression script

Everything that does not need a network, in one command:

```bash
# offline: boot, test mode, device registry, hardware self-test
python3 tools/espx_test.py --port $ESPX_PORT all

# host unit tests for the YAML parser
tests/run_yaml_tests.sh

# network: HTTPS API, YAML configuration, MQTT, delta OTA
#   requires the device to have an IP (T2.1b) and to reach this host
python3 tools/network_tests.py --device-ip <device-ip> --local-ip <host-ip> \
                               --patch dist/<base>-to-<new>.patch
```

Expected tail for each: `RESULT: PASS` / `ALL YAML TESTS PASSED`.
