# ESPX 节点 MQTT 数据格式与 EMQX 规则参考

设备使用 **MQTT 5**。本文给出每个主题的精确报文结构，以及可直接使用的 EMQX
规则引擎 SQL。

> 前缀说明：下面的例子用 `<prefix>` 占位。设备默认前缀是 `espx/<device_id>`
> （如 `espx/84c7bb772e74`）——主题树里不含设备 ID 叶子，所以**每台必须前缀唯一**，
> 否则会互相覆盖。多节点的完整配置（设备默认值 + broker 侧按 `clientid` 的 ACL
> 隔离 + 运维账号）见 [DEPLOYMENT.md 3.3](DEPLOYMENT.md)。

---

## 1. 主题一览

| 主题 | 方向 | QoS | retain | 周期 | 说明 |
|---|---|---|---|---|---|
| `<prefix>/state` | 上行 | 1 | 是 | 30 s | 在线心跳 |
| `<prefix>/status` | 上行 | 1 | 是 | 30 s | 节点监控快照 |
| `<prefix>/sensors` | 上行 | 1 | 否 | 10 s | 周期传感器读数 |
| `<prefix>/attrs/<id>` | 上行 | 1 | 否 | 按需 | 查询响应 |
| `<prefix>/cmd/control/<id>` | 下行 | 1 | 否 | — | 控制命令 |
| `<prefix>/cmd/query/<id>` | 下行 | 1 | 否 | — | 查询请求 |
| `<prefix>/cmd/config` | 下行 | 1 | 否 | — | 下发 YAML 配置 |
| `<prefix>/cmd/reboot` | 下行 | 1 | 否 | — | 重启 |

所有上行消息都带 MQTT 5 **User Properties**（便于按固件/型号分组，无需解析 payload）：

| key | 示例 | 说明 |
|---|---|---|
| `fw` | `1.0.8` | 固件版本 |
| `model` | `esp32s3` | 芯片型号 |
| `dev` | `84c7bb772e74` | 设备 ID |

所有上行消息带 **Message Expiry Interval = 300 s**。

---

## 2. 上行报文结构

### 2.1 `<prefix>/state` — 心跳（retained）

```json
{
  "online": true,
  "device_id": "84c7bb772e74",
  "uptime": 545
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `online` | bool | 恒为 `true`；节点消失时 retained 消息会过期被清除 |
| `device_id` | string | 设备 ID |
| `uptime` | number | 上电秒数 |

### 2.2 `<prefix>/status` — 节点监控快照（retained）

```json
{
  "uptime": 545,
  "free_heap": 8466407,
  "min_free_heap": 8403308,
  "free_heap_internal": 174003,
  "min_free_heap_internal": 129428,
  "epoch": 1790520736,
  "time": "2026-09-27T22:52:16+0800",
  "time_synced": true,
  "ntp_server": "pool.ntp.org",
  "timezone": "CST-8",
  "mdns": "84c7bb772e74.local",
  "ws_clients": 1,
  "cpu": { "freq_mhz": 240, "cores": 2, "usage": 2.14, "tasks": 19 },
  "ram": {
    "internal_total": 350599,
    "internal_free": 174339,
    "internal_min_free": 129428,
    "internal_largest": 94208,
    "internal_used_pct": 50.27,
    "psram_total": 8388608,
    "psram_free": 8295628,
    "psram_used_pct": 1.11
  },
  "wifi_ssid": "HomeLab-SDWAN",
  "wifi_rssi": -40,
  "ip": "192.168.103.75"
}
```

要点：

- `free_heap` 含 PSRAM；**`free_heap_internal` 才是会耗尽的那个池**（Wi-Fi / TLS 走内部 RAM）。
- `ram.internal_min_free` 是上电以来的低水位，用于发现缓慢泄漏。
- `cpu.usage` 首次采样前为 `null`。
- NTP 同步前 `epoch`/`time` 为 `null`，`time_synced` 为 `false`。
- PSRAM 不存在时 `ram.psram_*` 字段缺失。

### 2.3 `<prefix>/sensors` — 周期传感器读数

payload 是以**设备 ID 为键**的对象，只包含带 `PERIODIC` 能力的设备（传感器）：

```json
{
  "id3": { "temperature": 27.6, "humidity": 65 }
}
```

各设备类型的字段：

| 设备类型 | 字段 |
|---|---|
| `dht11` | `temperature` (number, °C), `humidity` (number, %) |
| `ads1115` | `raw` (int), `mv` (number), `channel` (int), `gain` (int), `rate` (int) |
| `ina226` | `bus_voltage_mv`, `shunt_voltage_uv`, `current_ma`, `power_mw` (number) |

> 按键读取的 `button`、以及执行器（`relay`/`buzzer`/`ws2812` 等）不进 `sensors`，
> 用 `<prefix>/cmd/query/<id>` 主动查询拿 `<prefix>/attrs/<id>`。

### 2.4 `<prefix>/attrs/<id>` — 查询响应

`<id>` 为设备 ID，payload 是该设备的读值：

```json
// dht11
{ "temperature": 27.9, "humidity": 66 }

// relay
{ "state": true }

// button
{ "pressed": false }

// buzzer
{ "on": false, "frequency": 2000, "duty": 50, "remaining_ms": 0 }

// mcp4725
{ "value": 2048, "voltage_mv": 1650.0, "vref_mv": 3300 }

// shiftreg_595
{ "bytes": [1, 0] }

// ws2812
{ "pixels": [ {"r":255,"g":0,"b":0}, ... ], "count": 10, "brightness": 222 }

// tja1050 (CAN 帧)
{ "id": 291, "ext": false, "rtr": false, "dlc": 8, "timestamp": 123456, "data": [1,2,...] }
```

### 2.5 下发命令格式

```json
// <prefix>/cmd/control/<id>
{ "action": "set", "value": <见下表> }
{ "action": "get" }

// <prefix>/cmd/query/<id>
{ "action": "get" }
```

`value` 按设备类型：

| 设备类型 | `value` |
|---|---|
| `relay` | `true` / `false`，或 `{"state": true}` |
| `buzzer` | `{"on":true,"frequency":2000,"duty":50,"duration_ms":1000}` |
| `mcp4725` | `{"value": 2048}` 或 `{"voltage_mv": 1650}` |
| `shiftreg_595` | `{"bytes":[1,0]}` |
| `ws2812` | 见下（四种形式） |

ws2812 的四种写法：

```json
{"r":255,"g":0,"b":128,"brightness":128}                       // 全部同色
{"all":{"r":255,"g":0,"b":0},"brightness":64}                  // 全部同色（显式）
{"index":3,"r":160,"g":32,"b":240}                             // 单颗
{"pixels":[{"r":255,"g":0,"b":0},{"r":0,"g":255,"b":0}],"brightness":171}  // 逐颗
```

---

## 3. EMQX 规则引擎 SQL

下面的规则可以直接在 EMQX Dashboard → 集成 → 规则 里新建，或用
`POST /api/v5/rules` 创建。

> 多节点部署建议每台用唯一前缀（如 `espx/84c7bb772e74`），SQL 用
> `FROM "espx/+/sensors"`；`clientid` 就是设备 ID，可直接落库。

### 3.1 DHT11 温湿度入库

```sql
SELECT
    clientid                AS device_id,
    payload.id3.temperature AS temperature,
    payload.id3.humidity    AS humidity,
    user_properties.fw      AS fw,
    timestamp               AS ts
FROM
    "espx/sensors"
WHERE
    payload.id3.temperature != undefined
```

多节点（前缀含设备 ID）写法：

```sql
SELECT
    nth(2, tokens(topic, '/')) AS device_id,
    payload.id3.temperature    AS temperature,
    payload.id3.humidity       AS humidity,
    timestamp                  AS ts
FROM
    "espx/+/sensors"
WHERE
    payload.id3.temperature != undefined
```

> `nth(2, tokens(topic,'/'))` 取出 `espx/<device_id>/sensors` 里的 `<device_id>`；
> 若规则用 `clientid` 匹配（见下文），直接用 `clientid` 更直接。

落库动作（TimescaleDB / PostgreSQL 示例）：

```sql
INSERT INTO sensor_readings(device_id, temperature, humidity, ts)
VALUES (${device_id}, ${temperature}, ${humidity}, ${ts})
```

### 3.2 节点状态入库 / 大屏

```sql
SELECT
    clientid                      AS device_id,
    user_properties.fw            AS fw,
    user_properties.model         AS model,
    payload.uptime                AS uptime,
    payload.free_heap             AS free_heap,
    payload.free_heap_internal    AS free_heap_internal,
    payload.ram.internal_min_free AS internal_min_free,
    payload.ram.internal_used_pct AS internal_used_pct,
    payload.cpu.usage             AS cpu_usage,
    payload.wifi_rssi             AS wifi_rssi,
    payload.wifi_ssid             AS wifi_ssid,
    payload.ip                    AS ip,
    payload.time_synced           AS time_synced,
    timestamp                     AS ts
FROM
    "espx/status"
```

### 3.3 在线状态

```sql
SELECT
    clientid          AS device_id,
    payload.online    AS online,
    payload.uptime    AS uptime,
    timestamp         AS ts
FROM
    "espx/state"
```

> 节点掉电时 retained 的 `state`/`status` 会在 300 s 后过期，broker 将其清除。
> 需要「立刻知道掉线」应额外配置 EMQX 的**客户端上下线事件**规则（见 3.6）。

### 3.4 告警规则

**内部 RAM 偏低**（TLS 握手失败的前兆）：

```sql
SELECT
    clientid                   AS device_id,
    payload.free_heap_internal AS free_heap_internal,
    payload.ram.internal_used_pct AS used_pct,
    timestamp                  AS ts
FROM
    "espx/status"
WHERE
    payload.free_heap_internal < 60000
```

**Wi-Fi 信号弱**：

```sql
SELECT
    clientid        AS device_id,
    payload.wifi_rssi AS rssi,
    timestamp       AS ts
FROM
    "espx/status"
WHERE
    payload.wifi_rssi < -75
```

**温度超限**：

```sql
SELECT
    clientid                AS device_id,
    payload.id3.temperature AS temperature,
    timestamp               AS ts
FROM
    "espx/sensors"
WHERE
    payload.id3.temperature > 35
```

**时钟未同步超过 10 分钟**（`uptime > 600` 仍未同步）：

```sql
SELECT
    clientid           AS device_id,
    payload.uptime     AS uptime,
    payload.time_synced AS time_synced,
    timestamp          AS ts
FROM
    "espx/status"
WHERE
    payload.time_synced = false AND payload.uptime > 600
```

### 3.5 把告警转发到 Webhook

```sql
SELECT
    clientid                AS device_id,
    'temperature_high'      AS alert,
    payload.id3.temperature AS value,
    timestamp               AS ts
FROM
    "espx/sensors"
WHERE
    payload.id3.temperature > 35
```

动作选 **HTTP 服务**，body 模板：

```json
{"alert": "${alert}", "device": "${device_id}", "value": ${value}, "ts": ${ts}}
```

### 3.6 上下线事件（补足 retained 过期的盲区）

```sql
SELECT
    clientid      AS device_id,
    event         AS event,
    timestamp     AS ts
FROM
    "$events/client_connected"
```

```sql
SELECT
    clientid      AS device_id,
    event         AS event,
    reason        AS reason,
    timestamp     AS ts
FROM
    "$events/client_disconnected"
```

### 3.7 下发控制命令（规则反向发布）

例如「温度 > 35 自动开风扇（relay_a）」：

```sql
SELECT
    'espx/84c7bb772e74/cmd/control/relay_a'  AS topic,
    '{"action":"set","value":true}'       AS payload
FROM
    "espx/84c7bb772e74/sensors"
WHERE
    payload.id3.temperature > 35
```

动作选 **消息重发布（republish）**，目标主题用 `${topic}`，payload 用 `${payload}`。

> EMQX 的 SQL 词法不支持 `#{...}` 对象字面量。需要动态构造对象时用 `map_put`：
> `json_encode(map_put(map_put(map_new(), 'action', 'set'), 'value', true))`。

> 若希望这类自动命令「过期即丢」，在 republish 动作里设置 Message Expiry Interval
> （例如 60 s），这样设备离线时该命令不会在重连后被当作新鲜指令执行。

---

## 4. 用命令行验证规则输入

```bash
# 观察某节点全部上行
mosquitto_sub -h testing.homelab.lan -V 5 -u espx -P '<密码>' -t 'espx/#' -v

# 只观察传感器
mosquitto_sub -h testing.homelab.lan -V 5 -u espx -P '<密码>' -t 'espx/sensors' -v

# 主动查询 dht11，等响应
mosquitto_sub -h testing.homelab.lan -V 5 -u espx -P '<密码>' -t 'espx/attrs/id3' -C 1 &
mosquitto_pub -h testing.homelab.lan -V 5 -u espx -P '<密码>' \
  -t 'espx/cmd/query/id3' -m '{"action":"get"}'
```

带消息过期时间的控制命令（MQTT 5）：

```bash
mosquitto_pub -h testing.homelab.lan -V 5 -u espx -P '<密码>' \
  -t 'espx/cmd/control/ws2812_2' \
  -m '{"action":"set","value":{"all":{"r":255,"g":0,"b":0},"brightness":128}}' \
  -D publish message-expiry-interval 60
```
