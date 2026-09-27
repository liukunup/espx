# 数据管道：设备 → EMQX → Kafka → TDengine

```
┌────────┐  MQTT 5   ┌──────┐  Kafka   ┌──────────────────┐  REST  ┌──────────┐
│ 设备群  │ ────────▶ │ EMQX │ ───────▶ │ kafka_to_tdengine│ ─────▶ │ TDengine │
└────────┘  espx/#   └──────┘  topic   │    (bridge)      │  6041  └──────────┘
                                  espx-telemetry                        │
                                                                        ▼
                                                                    Grafana
```

各段职责：

| 段 | 作用 |
|---|---|
| 设备 → EMQX | 每台上报 `espx/<device_id>/{state,status,sensors}`，ACL 按 clientid 隔离 |
| EMQX → Kafka | 规则把 MQTT 归一化成统一信封，Kafka 缓冲/解耦，broker 或 DB 抖动不丢数据 |
| Kafka → TDengine | bridge 消费、按 `kind` 落三张超表，offset checkpoint 支持重启续传 |
| TDengine | 时序存储，`device_id` 为 TAG，天然支持多节点聚合并发查询 |

> **为什么需要 bridge**：EMQX 没有 TDengine sink；TDengine 自带的 Kafka 连接器属于
> 企业版 taosX。一个几十行的消费者进程既解耦又便于重启。

---

## 1. 一键初始化

```bash
# 建 Kafka topic + EMQX action/规则 + TDengine 库/超表（幂等）
tools/setup_pipeline.py --apply

# 预览将要做的改动
tools/setup_pipeline.py --dry-run
```

前置条件：EMQX 里已存在名为 **`Kafka`** 的 `kafka_producer` 连接器
（Dashboard → 集成 → 连接器）。脚本只引用它，不创建。

> ⚠️ **脚本里的 TDengine DDL 仍是旧设计**（通用表 `espx_env`）。第 3 节的「每传感器一张表」
> 尚未定稿实施，先不要用它建表 —— 等第 4 节的方案选定后同步更新。

---

## 2. Kafka 消息信封

三条 EMQX 规则把三种上报归一成同一格式，`kind` 区分来源：

```json
{
  "kind": "env",                                  // env | node | state
  "device_id": "84c7bb772e74",                    // = MQTT clientid，多节点主键
  "fw": "1.0.8",
  "model": "esp32s3",
  "ts": 1790522489698,                            // epoch ms
  "topic": "espx/84c7bb772e74/sensors",
  "body": { "id3": { "temperature": 25.7, "humidity": 61 } }
}
```

| kind | 来源主题 | body 内容 |
|---|---|---|
| `env` | `espx/+/sensors` | `{"<sensor_id>": {"temperature":…, "humidity":…}}` |
| `node` | `espx/+/status` | 节点状态快照（heap / CPU / RAM / Wi-Fi） |
| `state` | `espx/+/state` | `{"online":true,"device_id":…,"uptime":…}` |

Kafka key = `device_id`，`partition_strategy=key_dispatch`，同一节点的数据落在同一分区，保持时序。

> ⚠️ EMQX 6.3 的规则 SQL 里 `map_put()` / `concat()` 会抛异常，所以**信封由 action 模板拼**，
> 不要在 SELECT 里用 `json_encode(map_put(...))` 构造消息。

---

## 3. TDengine 表结构：每传感器一张表

数据库 `espx`（`KEEP 365 DURATION 10 WAL_LEVEL 1`）。

### 设计原则（已确认）

1. **每种传感器类型一张超表**，不把不同传感器塞进一张通用表。
2. **TAG 用 `device_id` + `sensor_id`**：前者区分节点，后者区分同一台设备上的多个同类传感器
   （例如一台机器挂两个 DHT11：`id3`、`id4`）。缺 `sensor_id` 这两个传感器会写进同一张子表混在一起。
3. 子表由 TAG 组合自动生成（一个 `(device_id, sensor_id)` 一个子表），不需手工建。

### 超表清单

| 超表 | 来源 | FIELD（度量） | TAG |
|---|---|---|---|
| `espx_dht11` | `espx/sensors` 的 `dht11` | `temperature`, `humidity` | `device_id`, `sensor_id`, `fw`, `model` |
| `espx_ads1115` | `espx/sensors` 的 `ads1115` | `raw`, `mv`, `channel`, `gain`, `rate` | `device_id`, `sensor_id`, `fw`, `model` |
| `espx_ina226` | `espx/sensors` 的 `ina226` | `bus_voltage_mv`, `shunt_voltage_uv`, `current_ma`, `power_mw` | `device_id`, `sensor_id`, `fw`, `model` |
| `espx_node` | `espx/status` | `uptime`, `free_heap`, `min_free_heap`, `free_heap_internal`, `min_free_heap_internal`, `cpu_usage`, `cpu_tasks`, `ram_internal_used_pct`, `ram_psram_used_pct`, `wifi_rssi`, `ws_clients`, `time_synced` | `device_id`, `wifi_ssid`, `ip`, `fw`, `model` |
| `espx_state` | `espx/state` | `online`, `uptime` | `device_id`, `fw` |

> 只有带 **PERIODIC** 能力的外设会进 `espx/sensors` —— 当前是 `dht11`、`ads1115`、`ina226`。
> 执行器（relay / buzzer / ws2812 / mcp4725 …）与按键（button）不进时序表，需要时用
> `espx/attrs/<id>` 按需查询。

### 映射规则

```
espx/sensors   {"id3": {"temperature": 25.7, "humidity": 61}}
                    │              │
   payload 的键 = sensor_id          └─── 字段名 → FIELD
device_id = MQTT clientid
table     = espx_<外设类型>          → espx_dht11
ts        = 消息时间戳（ms）
```

### 子表命名（⚠️ 写错会静默错数据）

子表名 **必须同时包含 device_id 和 sensor_id**：

```
<stable>_<device_id>_<sensor_id>     →  espx_dht11_84c7bb772e74_id3
```

**为什么不能只用 device_id** —— 已实测：

```sql
-- 子表名相同、TAGS 不同，两次插入都返回 code=0，无任何报错
INSERT INTO dht11_dev USING espx_dht11 TAGS ('dev1','id3') VALUES (T1, 25.7, 61);
INSERT INTO dht11_dev USING espx_dht11 TAGS ('dev1','id4') VALUES (T2, 22.2, 55);

-- 实际落表：id4 的数据被归到了 id3 名下
```

| sensor_id | temperature | humidity |
|---|---|---|
| `id3` | 25.7 | 61 |
| `id3` | **22.2** | **55** | ← 本应是 id4 |

TDengine 对已存在的子表 **不会更新 TAGS**，而是沿用第一次的标签并静默写入。
所以命名一旦不区分 sensor_id，同一台设备上的第二个同类传感器会把数据混进第一个。
（子表名含 sensor_id 时实测正确分离。）

### DDL

```sql
CREATE DATABASE IF NOT EXISTS espx KEEP 365 DURATION 10 WAL_LEVEL 1;

CREATE STABLE IF NOT EXISTS espx.espx_dht11 (
  ts TIMESTAMP, temperature FLOAT, humidity FLOAT
) TAGS (device_id NCHAR(32), sensor_id NCHAR(32), fw NCHAR(16), model NCHAR(16));

CREATE STABLE IF NOT EXISTS espx.espx_ads1115 (
  ts TIMESTAMP, raw INT, mv FLOAT, channel INT, gain INT, rate INT
) TAGS (device_id NCHAR(32), sensor_id NCHAR(32), fw NCHAR(16), model NCHAR(16));

CREATE STABLE IF NOT EXISTS espx.espx_ina226 (
  ts TIMESTAMP, bus_voltage_mv FLOAT, shunt_voltage_uv FLOAT,
  current_ma FLOAT, power_mw FLOAT
) TAGS (device_id NCHAR(32), sensor_id NCHAR(32), fw NCHAR(16), model NCHAR(16));

CREATE STABLE IF NOT EXISTS espx.espx_node (
  ts TIMESTAMP, uptime BIGINT, free_heap BIGINT, min_free_heap BIGINT,
  free_heap_internal BIGINT, min_free_heap_internal BIGINT,
  cpu_usage FLOAT, cpu_tasks INT, ram_internal_used_pct FLOAT,
  ram_psram_used_pct FLOAT, wifi_rssi INT, ws_clients INT, time_synced BOOL
) TAGS (device_id NCHAR(32), wifi_ssid NCHAR(64), ip NCHAR(16), fw NCHAR(16), model NCHAR(16));

CREATE STABLE IF NOT EXISTS espx.espx_state (
  ts TIMESTAMP, online BOOL, uptime BIGINT
) TAGS (device_id NCHAR(32), fw NCHAR(16));
```

### 查询示例

```sql
-- 每个节点上都有哪些传感器、各多少条
SELECT device_id, sensor_id, COUNT(*) FROM espx.espx_dht11
  GROUP BY device_id, sensor_id;

-- 分节点、分传感器的 10 分钟均温
SELECT _wstart, device_id, sensor_id, AVG(temperature) FROM espx.espx_dht11
  PARTITION BY device_id, sensor_id INTERVAL(10m);

-- 全网平均温湿度
SELECT AVG(temperature), AVG(humidity) FROM espx.espx_dht11 WHERE ts > now-1h;

-- 各节点最新内存/信号
SELECT LAST_ROW(device_id), LAST_ROW(free_heap_internal), LAST_ROW(wifi_rssi)
  FROM espx.espx_node GROUP BY device_id;

-- 内存吃紧告警（近 5 分钟任一节点内部 RAM < 60KB）
SELECT device_id, free_heap_internal, ts FROM espx.espx_node
  WHERE ts > now-5m AND free_heap_internal < 60000;
```

> ⚠️ **迁移状态**：早期实现建的是通用表 `espx_env`（DHT11 温湿度混在一起）。新设计以
> `espx_dht11` 取代它。**旧表与旧数据暂未清理**，等方案定稿后一并处理。
> `tools/setup_pipeline.py` 里的 DDL 目前仍是旧设计，实施时需同步更新。

---

## 4. 实施状态与方案选择（待定）

### 4.1 现状

| 环节 | 状态 |
|---|---|
| 设备 → EMQX | ✅ 已通（MQTT 5，按 clientid 隔离） |
| EMQX → Kafka | ✅ 已通（`espx_kafka_env/node/state` 三条规则 + `espx_to_kafka` action） |
| **Kafka → TDengine** | ⏳ **待实施 —— 当前没有执行者** |
| TDengine 表 | ⏳ 需按第 3 节新设计建表 |

Kafka 里已有数据缓冲（topic 保留），因此补齐这一环后可以回填，不会丢历史。

### 4.2 为什么这一环必须有个进程

- EMQX 没有 TDengine sink；
- TDengine **community 没有 Kafka source** —— `taosX` 属企业版，且实测本机 taosX 默认端口
  `6050` 关闭、6060 的 Explorer 也无 Data-In 入口。

所以要么自己写一个消费者，要么上 Kafka Connect。

### 4.3 方案 A：自研 bridge（代码已就绪）

`tools/kafka_to_tdengine.py` —— 直接消费 Kafka，用 TDengine REST 写入。

- **改动量**：小。只需把表映射改成 `espx_<type>` 并给 TAG 加 `sensor_id`。
- **新增依赖**：`kafka-python`。部署到任意常开主机即可。
- **优点**：表结构完全按第 3 节可控；不受 schemaless 格式限制。
- **缺点**：DLQ、重试编排、水平扩展需自己实现。

### 4.4 方案 B：TDengine Kafka Sink Connector

官方 `taosdata/kafka-connect-tdengine`。标准做法，但有三个硬约束。

**前置条件（homelab 目前均不满足）**

| 需求 | 现状 |
|---|---|
| Kafka Connect 集群 | ❌ 无（`8083` 是 Cowboy/Erlang 服务，`/connector-plugins` 404，也无 `connect-*` 内部 topic） |
| connector JAR | ❌ 未安装 |
| **每个 worker 装 libtaos 原生库** | ❌ 未安装 |

**为何必须装原生库** —— 连接器源码里只允许原生连接：

```java
public enum ConnectionTypeEnum {
    TAOS("jdbc:TAOS"),
//  TAOS_RS("jdbc:TAOS-RS"),   ← REST 连接被注释掉了
}
```

**必须改消息格式** —— 连接器内部走 TDengine 的 **schemaless 写入**，只认 InfluxDB 行协议 /
OpenTSDB Telnet / OpenTSDB JSON 三种；当前的自定义 JSON 信封它无法映射。EMQX 需直接产出：

```
espx_dht11,device_id=84c7bb772e74,sensor_id=id3,fw=1.0.8,model=esp32s3 temperature=25.7,humidity=61 1790522489698
```

这样 metric 名 = `espx_dht11`，自动建的超表与第 3 节设计一致。

**修正后的 connector 配置**：

```json
{
  "name": "espx-tdengine-sink",
  "config": {
    "connector.class": "com.taosdata.kafka.connect.sink.TDengineSinkConnector",
    "tasks.max": "3",
    "topics": "espx-telemetry",
    "connection.url": "jdbc:TAOS://testing.homelab.lan:6030",
    "connection.user": "root",
    "connection.password": "taosdata",
    "connection.database": "espx",
    "db.schemaless": "line",
    "data.precision": "ms",
    "batch.size": "1000",
    "max.retries": "3",
    "retry.backoff.ms": "3000",
    "key.converter": "org.apache.kafka.connect.storage.StringConverter",
    "value.converter": "org.apache.kafka.connect.storage.StringConverter",
    "errors.tolerance": "all",
    "errors.deadletterqueue.topic.name": "espx-telemetry-dlq",
    "errors.deadletterqueue.topic.replication.factor": "1"
  }
}
```

常见写错对照（依据官方 `config/sink-quickstart.properties` 与源码）：

| 容易写成 | 正确 | 说明 |
|---|---|---|
| `com.taosdata.kafka.TDengineSinkConnector` | `com.taosdata.kafka.connect.sink.TDengineSinkConnector` | 完整类名 |
| `"db.schemaless": "false"` | `"db.schemaless": "line"` | 取值是格式名（`line`/`json`/`telnet`），**不是布尔** |
| `"database": "..."` | `"connection.database": "..."` | 参数名 |
| `value.converter: JsonConverter` | `StringConverter` | 数据本身已是行协议 |
| `tasks.max` > 分区数 | `= 分区数` | 多出的 task 空转 |
| 缺 `data.precision` | `"ms"` | 本设计用毫秒时间戳 |

> ⚠️ **未验证风险**：EMQX 6.3 的 SQL 无法遍历 map，把「一台设备多个传感器」的 payload 拆成
> 多行行协议受限（且 `concat()`/`map_put()` 会抛异常，已实测）。当前单传感器可行，
> **多传感器场景必须实测后再决定**。

### 4.5 对比

| | 方案 A 自研 bridge | 方案 B Kafka Connect |
|---|---|---|
| 额外常驻组件 | 1 个 Python 进程 | Connect 集群 + JVM |
| 额外依赖 | `kafka-python` | connector JAR + **libtaos 原生库** |
| 消息格式约束 | 无（当前 JSON 信封可用） | 必须 InfluxDB 行协议等三种 |
| 表结构控制 | 完全可控 | 由行协议文本决定 |
| 运维能力 | 自己实现 | ✅ DLQ / 重试 / 扩展标准化 |
| 与 Kafka 共址 | 不必 | 需要（本地连 9092） |

两种都能实现第 3 节的表设计。**选 B** 需先解决 Connect 集群 + 原生库 + 行协议改造，
并验证多传感器拆分；**选 A** 只需一台常开主机。**待确认后再实施。**

---

## 5. 运行 bridge（方案 A）

> **bridge 必须跑在常开的主机上**（homelab 服务器 / NAS / 容器平台），不能放工作站：
> 笔记本休眠、合盖或离开该网段，管道就停止写入 TDengine —— Kafka 能兜住一时，
> 但超过 retention 的数据就真丢了。
>
> **部署在 homelab，两种方式：**
>
> ```bash
> # ① systemd（裸机）
> sudo useradd -r -s /usr/sbin/nologin espx
> sudo mkdir -p /opt/espx/bin /var/lib/espx
> sudo cp tools/kafka_to_tdengine.py /opt/espx/bin/
> sudo cp deploy/espx-bridge.env.example /etc/espx-bridge.env   # 填凭据
> sudo cp deploy/espx-bridge.service /etc/systemd/system/
> sudo systemctl enable --now espx-bridge
>
> # ② 容器
> docker build -f deploy/Dockerfile.bridge -t espx-bridge .
> docker run -d --name espx-bridge --restart unless-stopped \
>   --env-file deploy/espx-bridge.env.example \
>   -v espx-bridge-state:/var/lib/espx espx-bridge
> ```
>
> 命令行的 `--offsets` 默认值指向 `/tmp`，仅供手工调试；服务化时务必指到持久目录
> （systemd 用 `--offsets /var/lib/espx/offsets.json`，容器用 `BRIDGE_OFFSETS` 环境变量），
> 否则重启会从 Kafka 末尾重新开始。
>

```bash
# 处理完积压就退出（首次回填 / 调试）
tools/kafka_to_tdengine.py --from-beginning --reset-offsets --once

# 常驻（前台）
tools/kafka_to_tdengine.py

# 常驻（后台 + 日志）
nohup python3 -u tools/kafka_to_tdengine.py > /tmp/espx-bridge.log 2>&1 &
```

参数（也可用环境变量 `KAFKA_BOOTSTRAP` / `KAFKA_TOPIC` / `TDENGINE_URL` /
`TDENGINE_USER` / `TDENGINE_PASS` / `TDENGINE_DB` / `BRIDGE_OFFSETS`）：

| 参数 | 默认 | 说明 |
|---|---|---|
| `--bootstrap` | `192.168.100.101:9092` | Kafka |
| `--topic` | `espx-telemetry` | 消费主题 |
| `--td-url` | `http://testing.homelab.lan:6041` | TDengine REST（**6041**，6060 是 Explorer 界面） |
| `--db` | `espx` | 目标库 |
| `--offsets` | `/tmp/espx-bridge-offsets.json` | offset checkpoint |
| `--batch-max` / `--flush-sec` | 200 / 1.0 | 批量阈值 |
| `--once` | — | 处理完退出 |

重启后从 checkpoint 续传；删掉 checkpoint 文件（或 `--reset-offsets`）则默认从**末尾**开始，
加 `--from-beginning` 从最早开始回填。

> Kafka broker 在 metadata 里广告的是容器名 `1Panel-kafka-cq1x`，homelab 外无法解析，
> 脚本在 socket 层把该名字重写到可达 IP。详见 `kafka_to_tdengine.py` 顶部注释。

---

## 6. 验证

```bash
# Kafka 里有没有数据（各自独立 client，无需 group）
tools/kafka_to_tdengine.py --once --verbose

# TDengine 直接查
curl -s -u root:taosdata \
  -d "SELECT device_id, temperature, ts FROM espx.espx_env ORDER BY ts DESC LIMIT 5" \
  http://testing.homelab.lan:6041/rest/sql
```

EMQX 侧看规则是否命中：Dashboard → 集成 → 规则，关注 `matched` / `passed` / `failed`；
`failed.exception > 0` 说明 SQL 有问题（见第 2 节注意事项）。

---

## 7. 故障排查

| 现象 | 原因 | 处理 |
|---|---|---|
| Kafka 无数据，规则 `failed.exception>0` | 规则 SQL 用了 `map_put`/`concat` | 按第 2 节用 action 模板拼信封 |
| Kafka 有数据，TDengine 空 | bridge 未运行 / offset 已到末尾 | 起 bridge；回填用 `--from-beginning --reset-offsets` |
| bridge 报 `UnknownError` on join group | 显式 group_id 与该 broker 不兼容 | 已改为手动 `assign()`，不要传 group_id |
| `body` 是字符串而非对象 | 规则改成了 `json_encode(payload)` | 用 `payload AS body`；bridge 两种都兼容 |
| 同设备的两个同类传感器数据混在一起 | 子表名未含 `sensor_id`，TAGS 被沿用 | 子表名改为 `<stable>_<device_id>_<sensor_id>`（见第 3 节） |
| 只有一台设备的数据 | 前缀不唯一 / ACL 未按 clientid | 见 DEPLOYMENT.md 3.3 |
| 时间戳看着差 8 小时 | TDengine 存 UTC，客户端按本地时区显示 | 正常 |
