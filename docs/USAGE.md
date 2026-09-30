# ESPX 使用手册

三种配置通道、MQTT 主题、REST API、WebSocket 的完整用法。

**前置**：`HOST` 用 mDNS 名（`espx-84c7bb772e74.local`）或设备 IP 均可；
HTTPS 用自签名证书，`curl` 需加 `-k`。

```bash
HOST=espx-84c7bb772e74.local
BROKER=192.168.1.10
PREFIX=plant/line1

# 重新生成证书（根据设备 ID）
python3 tools/gen_cert.py --id espx-84c7bb772e74
idf.py build   # 重新编译固件以嵌入新证书
```

---

## 1. 配置模型

节点状态 = NVS 中的配置 + 该配置在当前硬件上的实例化结果。

配置文档由若干小节组成，**只处理出现的小节**（局部下发不会清空其他配置）：

| 小节 | 作用 |
|---|---|
| `node` | 设备身份：`device_id`（同时是 MQTT client id）、`name` |
| `network` | `wifi_ssid`/`wifi_password`（预置则直连不配网）、`mqtt_broker`、`mqtt_username`、`mqtt_password`、`mqtt_topic_prefix` |
| `devices` | 设备绑定列表，按 `id` **upsert**（不存在则新增，存在则改配置并重新初始化） |
| `remove_devices` | 显式删除的 id 列表 |
| `replace_devices` | `true` 则同时删除未在 `devices` 中列出的设备（默认 `false`） |

语义保证：

| 行为 | 规则 | 原因 |
|---|---|---|
| 小节缺省 | 不触碰 | 局部下发不能意外清空其他配置 |
| `enabled` 缺省 | 视为 `true` | 只有显式 `false` 才禁用 |
| `replace_devices` 缺省 | `false` | 批量解绑必须显式声明 |
| 未知键 | 警告并忽略，不报错 | 向前兼容：新固件可接受含未来字段的文档 |
| 敏感值 | 日志只打印 `<set>` | 不把密码写进串口日志 |
| 网络变更 | 回执 `reboot_required: true` | MQTT/Wi-Fi 参数在连接建立时读取 |
| 校验 | 先校验后落盘 | 坏配置不改变设备状态 |

YAML 支持子集：块映射、缩进嵌套、块序列、**流式集合**（`{a: 1}` / `[1, 2]`）、
注释、单双引号、CRLF。**不支持**锚点/别名、多行块标量（`|` `>`）、文档分隔符（`---`）。
超出子集会**明确报错**（含行号），不会猜测。

JSON 与 YAML 均可：解析顺序为「先试 JSON，失败再试 YAML」，所以流式 YAML 以 `{`
开头也不会被误判。

---

## 2. 通道一：MQTT 下发 YAML（主通道）

### 主题

| 方向 | 主题 | 载荷 |
|---|---|---|
| 下发 | `<prefix>/cmd/config` | **YAML 文档**（无 `action` 字段） |
| 回执 | `<prefix>/config/result` | 应用结果 JSON |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_config"}` |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_devices"}` |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_device_types"}` |
| 控制 | `<prefix>/cmd/control/<id>` | `{"action":"set","value":…}` |
| 查询 | `<prefix>/cmd/query/<id>` | `{"action":"get"}` |
| 动作 | `<prefix>/cmd/config` | `{"action":"reboot"}` / `{"action":"testmode"}` |
| 动作 | `<prefix>/cmd/config` | `{"action":"apply","config":{…}}`（内嵌文档） |
| 上报 | `<prefix>/state` | 在线状态 + 心跳（retained） |
| 上报 | `<prefix>/sensors` | 周期采样类设备的值 |
| 上报 | `<prefix>/attrs/…` | 值、设备列表、设备类型 |
| 上报 | `<prefix>/ota/status` | OTA 状态与进度 |

**路由规则**：载荷是 JSON 对象且含 `action` → 走动作语义；否则 → 作为配置文档直接应用。
判据是**内容**而非首字符（流式 YAML 同样以 `{` 开头）。

### 示例

```bash
cat > node.yaml <<'YAML'
node:
  name: 车间一号节点

network:
  mqtt_broker: mqtt://192.168.1.10:1883
  mqtt_topic_prefix: plant/line1

devices:
  - id: temp_in
    type: dht11
    config: {gpio: 4, interval_ms: 5000}

  - id: relay_a
    type: relay
    config: {gpio: 5, active_level: 1}

  - id: out16
    type: shiftreg_595
    config: {din: 16, clock_gpio: 17, latch_gpio: 18, count: 2}

  - id: strip
    type: ws2812
    config: {din: 48, count: 8, brightness: 128}

remove_devices: [old_sensor]
YAML

# 下发
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/config" -f node.yaml

# 读回执
mosquitto_sub -h $BROKER -t "$PREFIX/config/result" -C 1 | python3 -m json.tool
```

回执：

```json
{
  "ok": true,
  "applied": { "added": 4, "updated": 0, "removed": 1, "failed": 0 },
  "node_changed": true,
  "network_changed": true,
  "reboot_required": true
}
```

失败时附 `error`，例如配置写错引脚：

```json
{ "ok": false, "applied": {"added":0,"updated":0,"removed":0,"failed":1},
  "error": "device 'out16': ESP_ERR_INVALID_ARG" }
```

### 控制与查询

```bash
# 继电器吸合
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/relay_a" -m '{"action":"set","value":true}'

# 读回
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/query/relay_a" -m '{"action":"get"}'
# → <prefix>/attrs/relay_a  {"state":true}

# 74HC595 两片 = 16 路，第 0 片输出 0x01
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/out16" -m '{"action":"set","value":[1,0]}'

# WS2812 控制（支持 4 种写法）
# 1. 全部同色
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/strip" -m '{"action":"set","value":{"r":255,"g":0,"b":0}}'
# 2. 全部同色（显式 all）
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/strip" -m '{"action":"set","value":{"all":{"r":255,"g":0,"b":0}}}'
# 3. 指定序号（index 从 0 开始）
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/strip" -m '{"action":"set","value":{"index":0,"r":0,"g":255,"b":0}}'
# 4. 逐颗设置
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/strip" -m '{"action":"set","value":{"pixels":[{"r":255,"g":0,"b":0},{"r":0,"g":255,"b":0},{"r":0,"g":0,"b":255}]}}'

# WS2812 带亮度（brightness: 0-255）
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/control/strip" -m '{"action":"set","value":{"r":255,"g":255,"b":255,"brightness":128}}'

# 订阅上报数据
mosquitto_sub -h $BROKER -t "$PREFIX/#" -v                    # 全部
mosquitto_sub -h $BROKER -t "$PREFIX/state" -v                # 在线状态
mosquitto_sub -h $BROKER -t "$PREFIX/sensors" -v              # 传感器数据
mosquitto_sub -h $BROKER -t "$PREFIX/attrs/#" -v             # 查询响应


---

## 3. 通道二：HTTPS REST API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | Web 管理界面 |
| GET/PUT | `/api/node` | 设备身份 |
| GET | `/api/system/info` | 运行时间、堆、Wi-Fi、**时间**、**mDNS**、WS 客户端数 |
| GET/PUT | `/api/network` | MQTT 与网络配置 |
| GET | `/api/config` | 导出完整配置 |
| **POST** | **`/api/config`** | **应用配置文档（YAML 或 JSON）** |
| GET | `/api/peripherals` | 设备列表（含实时值） |
| POST | `/api/peripherals` | 新增设备 `{id,type,config}` |
| POST | `/api/peripherals/reload` | 重新初始化所有设备 |
| GET | `/api/peripheral/options` | 驱动目录与默认配置 |
| GET | `/api/peripherals/*` | 单个设备详情 |
| POST | `/api/peripherals/*` | `<id>` 改配置、`<id>/read`、`<id>/write`、`<id>/enable` |
| DELETE | `/api/peripherals/*` | 删除设备 |
| GET | `/api/ota/status` | OTA 状态、进度、字节数 |
| POST | `/api/ota/start` | `{"url":"http://…/fw.patch"}` |
| POST | `/api/ota/cancel` | 取消升级 |
| GET | `/api/certs/info` | 证书信息 |
| POST | `/api/system/reboot` | 重启 |
| POST | `/api/system/testmode` | 重启进入产线自检 |
| WS | `/ws` | 实时状态推送与命令（见第 5 节） |

`/api/peripherals/*` 是单一分发器：ESP-IDF 的 HTTP 服务只支持**末尾**通配符，
`/api/peripherals/*/read` 这类中间通配永远不会匹配，因此改为在处理器内解析路径。

### 示例

```bash
# 应用 YAML
curl -k -X POST https://$HOST/api/config \
     -H 'Content-Type: text/yaml' --data-binary @node.yaml

# 导出配置
curl -k https://$HOST/api/config | python3 -m json.tool

# 设备列表
curl -k https://$HOST/api/peripherals | python3 -m json.tool

# 写 / 读
curl -k -X POST https://$HOST/api/peripherals/relay_a/write -d 'true'
curl -k -X POST https://$HOST/api/peripherals/relay_a/read

# 启用 / 禁用
curl -k -X POST https://$HOST/api/peripherals/relay_a/enable -d '{"enabled":false}'

# 删除
curl -k -X DELETE https://$HOST/api/peripherals/relay_a

# 系统信息
curl -k https://$HOST/api/system/info | python3 -m json.tool
```

`/api/system/info` 返回：

```json
{
  "uptime": 1234, "free_heap": 210000, "min_free_heap": 190000,
  "epoch": 1790419739, "time": "2026-09-26T18:09:00+0800",
  "time_synced": true, "ntp_server": "pool.ntp.org", "timezone": "CST-8",
  "mdns": "espx-84c7bb772e74.local", "ws_clients": 1,
  "wifi_ssid": "HomeLab", "wifi_rssi": -55, "ip": "192.168.1.57"
}
```

`time` 为 `null` 表示 NTP 尚未同步（与"1970 年"明确区分）。

---

## 4. 通道三：产线串口控制台

产线自检控制台（UART0）是唯一的串口配置通道。长按 BOOT 3 秒进入，也可经
`POST /api/system/testmode` 或 MQTT `cmd/config {"action":"testmode"}` 触发。

控制台命令：

```
help · types · list · add <id> <type> [json] · del <id> · read <id>
write <id> <json> · test [id|all] · report · reset
mfg show|set <json>|apply|clear · cfg <json|yaml>|show · exit
```

其中 `cfg <json|yaml>` 用于在产线直接下发配置（等价于 REST `POST /api/config`），
`mfg set` / `mfg apply` 用于写入并应用工厂预置数据（`mfg_data` 分区）。

完整用法与逐步验证见 [TESTING.md](TESTING.md)（P1 用例与产线流程）。

---

## 5. WebSocket（`/ws`）

浏览器与 `wss://$HOST/ws` 建立连接后，服务端主动推送状态，避免轮询。

**服务端 → 客户端**

```jsonc
{"type":"hello","id":"espx-84c7bb772e74","name":"ESPX","version":"1.0.2","time":"…","mdns":"….local"}
{"type":"state","devices":[{"id":"relay_a","type":"relay","enabled":true,
                            "initialized":true,"value":{"state":true},"config":{…}}],
                 "uptime":123,"epoch":1790419739,"time":"…"}
{"type":"result","id":"relay_a","value":{"state":true}}
{"type":"config_result","ok":true,"added":1,"updated":0,"removed":0,"reboot_required":false}
{"type":"error","error":"…"}
{"type":"pong"}
```

**客户端 → 服务端**

```jsonc
{"type":"ping"}
{"type":"refresh"}                                 // 立即推送一次状态
{"type":"read","id":"relay_a"}
{"type":"write","id":"relay_a","value":true}
{"type":"config","yaml":"devices:\n  - id: r1\n    type: relay\n    config: {gpio: 5}"}
```

状态在**变化时**立即推送（订阅 `EVENT_DEVICE_VALUE_CHANGED` 等事件），
否则按 1 秒节拍检查；内容与上次相同则跳过，不产生冗余流量。

用 `websocat` 手工验证：

```bash
websocat -k --ws-c-uri=wss://$HOST/ws - wss://$HOST/ws
{"type":"refresh"}
# → 收到 state 消息
```

---

## 6. mDNS 与 NTP

```
# 同一局域网内直接用名字访问，无需查 IP
ping espx-84c7bb772e74.local
curl -k https://espx-84c7bb772e74.local/api/node
```

mDNS 名**就是设备 ID**（转小写并只保留 `[a-z0-9-]`），因此与 MQTT 主题前缀、
设备 ID 完全一致 —— 只有一个身份需要记，不会出现
`espx-espx-…` 这类重复前缀。广播 `_https._tcp`（端口 = `CONFIG_ESPX_HTTPS_PORT`）。
TXT 记录含 `id` / `model` / `version`，客户端在首次请求前就能拿到身份信息。

NTP 服务器与时区由 `CONFIG_ESPX_NTP_SERVER`（默认 `pool.ntp.org`）和
`CONFIG_ESPX_TZ`（默认 `CST-8`）配置。时钟正确的重要性：

- 客户端校验证书有效期时，设备时钟若早于证书 `notBefore`，**每次 TLS 都会被拒**
- 日志时间戳可跨设备对齐，便于排障

NTP 与 mDNS 都在拿到 IP 之后才启动（在 Wi-Fi 连接任务里），不影响开机流程。

---

## 7. ESP-NOW 对等通信

ESP-NOW 是一种**无需 IP** 的直接 Wi-Fi 通信协议，适合 ESP 设备之间的低延迟控制。

### 7.1 自动发现机制

设备启动后会自动广播 **ANNOUNCE** 消息（每 30 秒），其他设备收到后会自动将发送者加入对等设备列表。

```
┌──────────┐  ANNOUNCE (30s)  ┌──────────┐
│  ESPX-1  │ ───────────────► │  ESPX-2  │
│          │ ◄─────────────── │          │
└──────────┘  ANNOUNCE (30s)  └──────────┘
     │                                 │
     └───── DISCOVER (手动触发) ───────┘
```

**发现协议：**
- **ANNOUNCE**: 自动广播，包含设备 ID、名称、版本
- **DISCOVER**: 手动触发，收到后设备会响应 ANNOUNCE
- **自动配对**: 收到陌生设备的广播后，自动加入对等列表

### 7.2 ESP-NOW OTA 空中升级

使用 esp-now 组件内置的 OTA 功能，通过 ESP-NOW 传输固件升级。

**OTA 流程：**
1. Responder 连接到 WiFi，创建 OTA 任务等待升级
2. Initiator 从 HTTP 服务器下载固件
3. Initiator 通过 ESP-NOW 发送固件到 Responders
4. Responder 接收并写入 flash，重启后运行新固件

```
┌──────────────┐  下载固件   ┌──────────────┐
│  Initiator   │ ◄─────────── │ HTTP Server │
│  (发起升级)   │              └──────────────┘
└──────────────┘
       │
       │  ESP-NOW OTA_DATA
       ▼
┌──────────────┐  接收固件   ┌──────────────┐
│  Responder    │ ──────────► │  写入 Flash   │
│  (待升级设备)  │            └──────────────┘
└──────────────┘
```

**OTA API：**
```bash
# 初始化 OTA 响应者（等待升级）
curl -X POST /api/esp_now -d '{"action":"ota_init"}'

# 扫描可升级设备
curl -X POST /api/esp_now -d '{"action":"ota_scan","timeout_ms":30000}'

# 发送固件到目标设备
curl -X POST /api/esp_now -d '{"action":"ota_send","mac":"AA:BB:CC:DD:EE:FF"}'
```

### 7.3 ESP-NOW 配网

使用 esp-now 组件内置的配网功能，通过 ESP-NOW 为新设备配置 Wi-Fi 凭证。

**Responder 模式：** 已连接 WiFi 的设备广播配网信标
**Initiator 模式：** 新设备扫描并请求 WiFi 凭证

```
┌─────────────┐  PROV_BEACON (30s)  ┌─────────────┐
│  Responder  │ ◄────────────────── │  已配网设备  │
│ (广播信标)   │                      │             │
└─────────────┘                      └─────────────┘
      ▲                                     │
      │         PROV_REQUEST                │
      └─────────────────────────────────────┘
      │         PROV_RESPONSE (WiFi凭证)    │
      ▼                                     │
┌─────────────┐                             │
│  Initiator  │ ───────────────────────────►│
│  (新设备)    │  连接 WiFi                  │
└─────────────┘
```

**配网 API：**
```bash
# 主设备：开始配网（广播信标 30 秒）
curl -X POST /api/esp_now -d '{"action":"prov_start","ssid":"HomeLab","password":"12345678","duration_s":30}'


# 主设备：停止配网
curl -X POST /api/esp_now -d '{"action":"prov_stop"}'
```

### 7.4 Web 界面

在浏览器中访问 `https://<设备IP>/`，点击顶部导航的 **ESP-NOW** 标签：

```
┌─────────────────────────────────────────────────────────┐
│ Network | MQTT | ESP-NOW | OTA | System                │
├─────────────────────────────────────────────────────────┤
│ ESP-NOW Status                            [Refresh]    │
│ ┌─────────────────────────────────────────────────────┐│
│ │ Status: Active │ MAC: 84:C7:BB:77:2E:74           ││
│ │ Version: 2.5.3 │ Security: None │ Forward: No       ││
│ └─────────────────────────────────────────────────────┘│
│                                                         │
│ Discovered Devices (0)                     [🔍 Scan]   │
│ Click "Scan" to discover nearby devices.               │
│                                                         │
│ Peers (0)                                 [+ Add Peer]  │
│ No peers configured.                                     │
│                                                         │
│ Groups (0)                                [+ Add Group] │
│ No groups configured.                                    │
└─────────────────────────────────────────────────────────┘
```

功能说明：
- **Status**: 显示 ESP-NOW 运行状态
- **MAC**: 本设备 MAC 地址（可点击复制）
- **Discovered**: 发现的其他 ESPX 设备，点击 **Pair** 配对
- **Scan**: 发送 DISCOVER 广播，主动发现附近设备
- **Peers**: 已配对的对等设备列表
- **Groups**: 组播组列表

### 7.5 查询状态

```bash
curl -k https://$HOST/api/esp_now | python3 -m json.tool
```

返回：
```json
{
  "enabled": true,
  "security": false,
  "forward": false,
  "peer_count": 0,
  "group_count": 0,
  "discovered_count": 0,
  "version": "2.5.3",
  "mac": "84:C7:BB:77:2E:74",
  "peers": [],
  "discovered": [],
  "groups": []
}
```

### 7.6 手动发现与配对

```bash
# 触发 DISCOVER 广播（主动发现设备）
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{"action": "discover"}'

# 配对已发现的设备
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{"action": "pair", "mac": "AA:BB:CC:DD:EE:FF"}'

# 清空发现列表
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{"action": "clear_discovered"}'
```

### 7.7 添加组播组

```bash
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{
    "groups": [
      {"id": "01:02:03:04:05:06", "name": "sensors"},
      {"id": "AA:BB:CC:DD:EE:FF", "name": "actuators"}
    ]
  }'
```

### 7.8 启用加密

```bash
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{
    "security": true,
    "forward": true
  }'
```

### 7.9 禁用 ESP-NOW

```bash
curl -k -X POST https://$HOST/api/esp_now \
  -H 'Content-Type: application/json' \
  -d '{"enabled": false}'
```

### 7.10 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `enabled` | bool | 启用/禁用 ESP-NOW |
| `security` | bool | 启用 AES-128 加密 |
| `forward` | bool | 启用数据包转发（mesh 特性）|
| `peers` | array | 对等设备列表 |
| `groups` | array | 组播组列表 |
| `action` | string | 特殊操作：`discover`、`pair`、`prov_start`、`prov_send`、`prov_request` |

### 7.11 特性

| 特性 | 规格 |
|------|------|
| 对等设备数 | 最多 20 个 |
| 组播组数 | 最多 10 个 |
| 加密 | AES-128-CCM |
| 有效载荷 | 250 字节（明文）/ 218 字节（加密）|
| 重传 | 自动 ACK + 重试（默认 10 次）|
| 自动发现 | 每 30 秒广播 ANNOUNCE |
| OTA 升级 | 空中传输固件 |
| 配网 | 通过 ESP-NOW 发送 Wi-Fi 凭证 |

### 7.12 使用场景

```
场景 1: 多设备同步控制
┌─────────┐         ┌─────────┐
│ ESPX-1  │─────────│ ESPX-2  │
│ Relay1  │◄────────│ Relay2  │
└─────────┘  ESP-NOW └─────────┘
    │                       │
    └──────── MQTT ─────────┘
              │
        ┌─────────┐
        │  Broker │
        └─────────┘

场景 2: 传感器数据收集
┌─────────┐         ┌─────────┐
│ Temp1   │─────────│ ESPX    │
│ Sensor  │◄────────│ Gateway │──► MQTT
└─────────┘  ESP-NOW └─────────┘
```

---

## 8. 设备写值格式速查

```jsonc
// relay
true                                  // 或 {"state": true}

// shiftreg_595  count=N，索引 0 是第一片
[1, 255]                              // 或 {"bytes": [1, 255]}

// ws2812
{"all":   {"r":255,"g":0,"b":0}}      // 整条
{"index": 3, "r":0,"g":0,"b":255}     // 单颗
{"pixels":[{"r":255,"g":0,"b":0}, …]} // 逐颗
```

能力位含义（`GET /api/peripheral/options` 可见）：

| 位 | 含义 | 影响 |
|---|---|---|
| `R` READ | 可读值 | 出现在 `/api/peripherals` 与自检的 read 项 |
| `W` WRITE | 可控制 | 支持 `cmd/control`、`/write`、自检的 toggle 项 |
| `N` NOTIFY | 变化通知 | 值变化时发布事件（WS/MQTT 立即推送） |
| `P` PERIODIC | 周期采样 | 归入 MQTT `sensors` 主题 |

---

## 8. 常见问题

**改了 MQTT 配置不生效？**
网络类参数在连接建立时读取，看回执里的 `reboot_required`，重启设备即可。

**`cmd/config` 下发后收到 `ok:false`？**
看 `error`：设备级失败会写明是哪个 `id`、什么原因（如 `ESP_ERR_INVALID_ARG`
表示引脚或参数非法）。也可以看串口日志，会打印 `config_apply: device 'x': …`。

**YAML 报语法错？**
错误信息带行号。注意本实现只支持文档化的子集；`|` / `>` 多行标量会明确报错。

**浏览器打不开 `https://….local`？**
Windows 需装 Bonjour，Android 支持不稳定。用 IP 访问，或用 `curl -k https://<ip>/api/system/info` 查地址即可。

**WebSocket 显示 `poll`？**
说明 WS 未连上，界面自动退化为轮询（功能不受影响）。检查 `/api/system/info`
里的 `ws_clients`，并留意 `CONFIG_HTTPD_WS_MAX_CLIENTS` 与
`max_open_sockets`（默认 7）是否被占满。
