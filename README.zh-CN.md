# ESPX — 软件定义 IoT 节点

[English](README.md) | 中文

面向 ESP32-S3（R16N8：16MB Flash / 8MB PSRAM）的通用节点固件。

**核心思路：配置即节点。** 固件本身与具体硬件无关，节点"是什么"完全由配置决定 ——
外设类型、数量、引脚、参数都可以在运行时配置，掉电保存。

```
        固件（不变）                      配置（可变）
  ┌──────────────────────┐        ┌──────────────────────────┐
  │  设备驱动目录         │        │  devices:                │
  │   dht11 / button     │        │    - id: temp_in         │
  │   relay / 74hc595    │   +    │      type: dht11         │
  │   ws2812             │        │      config: {gpio: 4}   │
  │                      │        │    - id: relay_a         │
  │  MQTT / HTTPS / OTA  │        │      type: relay         │
  │  AT / WS / mDNS/NTP  │        │      config: {gpio: 5}   │
  └──────────────────────┘        └──────────────────────────┘
              └──────────────►  一个具体的节点
```

设计约束的权威声明见 [AGENT.md](AGENT.md)。

---

## 特性

| 能力 | 说明 |
|---|---|
| **软件定义外设** | 外设按配置绑定，`id` 唯一，改配置即改节点；无需为不同外形编译不同固件 |
| **四种配置通道** | MQTT 下发 YAML、HTTPS `POST /api/config`、串口 AT 指令、工厂预置数据，全部走同一个 `config_apply()` |
| **MQTT** | 状态/传感器周期上报，命令下发（查询/控制/配置/重启/OTA/测试模式） |
| **HTTPS 管理界面** | 自签名证书（项目文件，构建时嵌入）、单页 Web UI、REST API、**WebSocket 实时推送** |
| **差分 OTA** | `esp_delta_ota` + heatshrink，补丁通常比整包小 95% 以上；带基线校验，错版本补丁无法刷入 |
| **BLE 配网** | 默认 BLE（不暴露开放 AP）；也支持 SoftAP；支持工厂预置凭据直连 |
| **mDNS** | 通过 `espx-<mac>.local` 直接访问，无需查 IP |
| **NTP** | 时钟同步（证书有效期校验、日志时间戳） |
| **串口 AT 指令** | 参考 ESP-AT 的指令集与应答格式，宿主 MCU 可直接驱动 |
| **产线测试模式** | 长按 BOOT 3 秒进入，交互式硬件自检 |
| **工厂预置** | `mfg_data` 分区一次性写入：设备身份、网络、MQTT、外设绑定 |

---

## 内置外设驱动

| 类型 | 能力 | 配置项 |
|---|---|---|
| `dht11` | 读 / 周期采样 | `gpio`, `interval_ms` |
| `button` | 读 / 变化通知 | `gpio`, `active_level`, `pullup` |
| `relay` | 读 / 写 / 通知 | `gpio`, `active_level` |
| `shiftreg_595` | 读 / 写 / 通知 | `data_gpio`, `clock_gpio`, `latch_gpio`, `oe_gpio`, `count`（1–8 片级联） |
| `ws2812` | 读 / 写 / 通知 | `data_gpio`, `count`（1–300 灯珠）, `brightness` |

新增硬件只需实现一个 `device_type_t` 驱动并注册，不改动核心代码。

---

## 快速开始

```bash
# 1. 环境
. <esp-idf路径>/export.sh
pip install pyserial 'detools>=0.49.0'

# 2. 构建
idf.py set-target esp32s3
idf.py build

# 3. 烧录
idf.py -p /dev/cu.usbserial-XXXX -b 460800 flash monitor
```

首次上电进入 **BLE 配网**：控制台会打印设备名与二维码，用 ESP BLE Prov App 连接
（默认 PoP 见 `CONFIG_ESPX_PROV_POP`）并填写 Wi-Fi 账号密码。

配网后设备会：

```
I (…) wifi_prov: Got IP: 192.168.1.57
I (…) mdns: mDNS started: espx-84c7bb772e74.local  (https://espx-84c7bb772e74.local/)
I (…) time_sync: Time synchronised: 2026-09-26 18:04:05 +0800
I (…) mqtt_client: MQTT connected
```

然后浏览器打开 `https://espx-84c7bb772e74.local/`（自签名证书，需点信任一次）。

---

## 配置一个节点（YAML）

通过 MQTT 下发：

```bash
mosquitto_pub -h <broker> -t 'espx-84c7bb772e74/cmd/config' -f node.yaml
mosquitto_sub -h <broker> -t 'espx-84c7bb772e74/config/result' -C 1
```

`node.yaml`：

```yaml
node:
  name: 车间一号节点

network:
  mqtt_broker: mqtt://192.168.1.10:1883
  mqtt_topic_prefix: plant/line1
  # 也可预置 Wi-Fi，设备将直连而不进入配网
  wifi_ssid: PlantNet
  wifi_password: secret

devices:
  - id: temp_in                  # 温湿度
    type: dht11
    config: {gpio: 4, interval_ms: 5000}

  - id: relay_a                  # 继电器
    type: relay
    config: {gpio: 5, active_level: 1}

  - id: out16                    # 74HC595 级联 2 片 = 16 路输出
    type: shiftreg_595
    config: {data_gpio: 16, clock_gpio: 17, latch_gpio: 18, count: 2}

  - id: strip                    # WS2812 灯带，8 灯珠
    type: ws2812
    config: {data_gpio: 48, count: 8, brightness: 128}

remove_devices: [old_sensor]     # 显式删除
replace_devices: false           # 设为 true 则删除未列出的设备
```

同样这段 YAML 也可用于：

```bash
# HTTPS
curl -k -X POST https://espx-84c7bb772e74.local/api/config \
     -H 'Content-Type: text/yaml' --data-binary @node.yaml

# 串口 AT
AT+CFG=network: {mqtt_broker: "mqtt://192.168.1.10:1883"}
```

---

## 常用操作

```bash
HOST=espx-84c7bb772e74.local

# 设备列表（含实时值）
curl -k https://$HOST/api/devices

# 控制继电器
curl -k -X POST https://$HOST/api/devices/relay_a/write -d 'true'

# 导出当前配置
curl -k https://$HOST/api/config

# 系统信息（时间、堆、Wi-Fi、mDNS、WS 客户端数）
curl -k https://$HOST/api/system/info
```

MQTT 控制：

```bash
PREFIX=plant/line1
mosquitto_pub -h <broker> -t "$PREFIX/cmd/control/relay_a" -m '{"action":"set","value":true}'
mosquitto_pub -h <broker> -t "$PREFIX/cmd/query/temp_in"   -m '{"action":"get"}'
```

串口 AT（默认 UART1，TX=GPIO4 / RX=GPIO5，115200）：

```
AT+GMR                  版本信息
AT+ID                   设备 ID 与名称
AT+CWJAP="ssid","pass"  连接 Wi-Fi 并重启
AT+CIFSR                查询 IP
AT+CFG?                 导出当前配置
AT+CFG=<yaml|json>      下发配置
AT+DEV?                 设备列表
AT+DEV="relay_a",true   控制设备
AT+SYSTIME?             NTP 时间
AT+MQTTCONN="host",1883,"user","pass"
AT+OTASTART="http://…/fw.patch"
AT+TESTMODE             重启进入产线自检
AT+HELP?                完整指令表
```

---

## 差分 OTA

```bash
# 用设备当前运行的固件作为基线生成补丁（关键：基线必须匹配）
tools/make_delta_patch.py --base build/espx-v1.0.0.bin \
                          --new  build/espx-v1.0.1.bin \
                          --out  dist/v1.0.0_to_v1.0.1.patch

python3 -m http.server 8000 --directory dist
curl -k -X POST https://$HOST/api/ota/start \
     -H 'Content-Type: application/json' \
     -d '{"url":"http://192.168.1.20:8000/v1.0.0_to_v1.0.1.patch"}'
```

补丁头部含基线固件的 SHA-256，设备会与**当前运行固件**比对，不匹配直接拒绝 ——
错版本补丁无法把设备刷坏。

实测：1.25MB 固件 → 约 20–75KB 补丁（压缩率 95%–98%）。

---

## 产线流程

```bash
idf.py erase-flash && idf.py flash

# 长按 BOOT 3 秒进入测试模式
cfg {network: {wifi_ssid: PlantNet, mqtt_broker: "mqtt://broker.plant:1883",
               mqtt_topic_prefix: plant/line1}}
mfg set {"node":{"device_id":"espx-0001"},"devices":[{"id":"temp_in","type":"dht11",
         "config":{"gpio":4}}]}
test all        # 硬件自检，逐项 PASS/FAIL
exit            # 重启并应用
```

---

## 文档

| 文档 | 内容 |
|---|---|
| [AGENT.md](AGENT.md) | 工程方向与硬性设计约束（**权威**） |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构、数据模型、接口、OTA、分区表、已知限制 |
| [docs/USAGE.md](docs/USAGE.md) | 四种配置通道的完整用法与示例 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 环境搭建、新增外设驱动、代码规范、调试 |
| [docs/TESTING.md](docs/TESTING.md) | 按优先级排序的测试用例与逐步验证方法 |
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | 产线烧录、EMQX 初始化、安全加固、升级策略 |

---

## 工具

| 工具 | 用途 |
|---|---|
| `tools/espx_test.py` | 串口自动化测试（启动信息 / 进入测试模式 / 硬件自检） |
| `tools/network_tests.py` | 网络回归（HTTPS API、YAML 配置、MQTT、差分 OTA） |
| `tools/make_delta_patch.py` | 生成差分 OTA 补丁并自校验 |
| `tools/emqx_init.py` | 初始化 EMQX（认证器、设备用户、ACL 规则）并做真实 MQTT 往返验证 |
| `tools/mini_mqtt_broker.py` | 无依赖的最小 MQTT broker，用于本地联调 |
| `tools/gen_certs.sh` | 重新生成 HTTPS 自签名证书 |
| `tools/verify_device.sh` | 烧录 / 监视 / 日志抓取 |
| `tests/run_yaml_tests.sh` | YAML 解析器主机单元测试 |

---

## 已知限制

1. **REST API 无鉴权** —— 能访问到设备即可控制外设、改 MQTT 配置、触发 OTA。
   生产网络暴露前必须补认证。
2. 自签名证书为**开发默认值**，量产必须替换。
3. BLE 配网 PoP 默认 `abcd1234`，且默认打印在二维码里；量产需按批次更换并关闭二维码内嵌。
4. 若选择 SoftAP 配网，配置界面会暴露在开放的配网 AP 上。
5. Wi-Fi 凭据以明文存于 NVS。
6. AT 指令集是 ESP-AT 的**子集**，不是完整替代。

完整列表与缓解措施见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 第 10 节。

---

## 许可证

示例代码属公有领域（或 CC0），可按需使用。
