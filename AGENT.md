# AGENT.md — ESPX 工程方向

> 本文件是**工程意图的权威声明**。任何改动如果与本文冲突，以本文为准；如需偏离，先改本文。

---

## 1. 方向

**ESPX 是一个软件定义节点（Software-Defined Node）。**

核心命题：**配置即节点**（Configuration IS the node）。

固件是通用的、与硬件无关的执行器；节点是什么，由配置决定。

```
        固件（不变）                     配置（可变）
  ┌──────────────────────┐        ┌──────────────────────────┐
  │  设备驱动目录         │        │  devices:                │
  │   dht11              │        │    - id: temp_in         │
  │   button             │  +     │      type: dht11         │
  │   relay              │        │      config: {gpio: 4}   │
  │   shiftreg_595       │        │    - id: relay_a         │
  │   ws2812             │        │      type: relay         │
  │                      │        │      config: {gpio: 5}   │
  │  MQTT / HTTPS / OTA  │        │  network: {...}          │
  │  NVS 持久化          │        │  node: {...}             │
  └──────────────────────┘        └──────────────────────────┘
              └──────────────►  一个具体的节点
```

推论（这些是必须遵守的设计约束）：

1. **不硬编码具体外设**。任何"某个产品有 3 个继电器、1 个 DHT11"的知识都不得出现在代码里，只能出现在配置里。
2. **不加产品专属编译开关**。一个新外形不是新固件分支，而是一份新配置。
3. **同一个固件能表达任意组合**。外设类型、数量、引脚、参数全部运行时可配。
4. **配置是第一类公民**：有 schema、有校验、有回执、可导出、可回滚（重启即回到已持久化的配置）。
5. **驱动是插件**。新增硬件 = 新增一个实现 `device_type_t` 的文件 + 一行注册。

---

## 2. 配置是唯一真相

节点状态 = NVS 中的配置 + 该配置在当前硬件上的实例化结果。

| 层 | 存放 | 说明 |
|---|---|---|
| 节点身份与网络 | NVS `espx_node/config` | `node` + `network` 两个块 |
| 设备绑定 | NVS `espx_devices/config` | 设备数组，按 `id` upsert |
| 工厂数据 | NVS 分区 `mfg_data` | 一次性应用后清除 |

配置**只通过**下面三条通道进入，三条通道走同一个应用函数 `config_apply()`：

| 通道 | 入口 | 用途 |
|---|---|---|
| MQTT | `<prefix>/cmd/config`，载荷为 **YAML** | 云端批量下发（主通道） |
| HTTPS | `POST /api/config`，载荷为 YAML 或 JSON | 网页/脚本配置 |
| 串口 | 测试控制台 `cfg <yaml\|json>` | 产线与调试 |

工厂预置是 HTTPS/MQTT 之外的第四条：`mfg_data` 分区，开机应用一次。

---

## 3. MQTT 下发 YAML（主接口）

### 3.1 主题

| 方向 | 主题 | 载荷 |
|---|---|---|
| 下发 | `<prefix>/cmd/config` | **YAML 文档**（无 `action` 字段） |
| 回执 | `<prefix>/config/result` | 应用结果 JSON |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_config"}` |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_devices"}` |
| 查询 | `<prefix>/cmd/config` | `{"action":"get_device_types"}` |
| 动作 | `<prefix>/cmd/config` | `{"action":"reboot"}` / `{"action":"testmode"}` |
| 动作 | `<prefix>/cmd/config` | `{"action":"apply","config":{...}}`（内嵌文档） |

**判定规则**：载荷以 `{` 或 `[` 开头 → 按 JSON 解析并走 `action` 语义；否则 → 按 YAML 文档直接应用。这样 YAML 与旧的 JSON 动词接口共存且互不干扰。

### 3.2 配置文档 schema

```yaml
# 全部小节都是可选的；只处理出现的小节
node:
  device_id: espx-0001        # 同时是 MQTT client id
  name: Line-1

network:
  wifi_ssid: PlantNet         # 出现则直连，跳过 SoftAP 配网（工厂预置）
  wifi_password: secret
  mqtt_broker: mqtt://broker.plant:1883
  mqtt_username: line1
  mqtt_password: secret
  mqtt_topic_prefix: plant/line1

devices:                      # 按 id upsert：不存在则新增，存在则更新配置并重新初始化
  - id: temp_in
    type: dht11
    enabled: true
    config: {gpio: 4, interval_ms: 5000}
  - id: relay_a
    type: relay
    config: {gpio: 5, active_level: 1}
  - id: out16
    type: shiftreg_595
    config: {data_gpio: 16, clock_gpio: 17, latch_gpio: 18, count: 2}
  - id: strip
    type: ws2812
    config: {data_gpio: 48, count: 8, brightness: 128}

remove_devices: [old1, old2]  # 显式删除
replace_devices: false        # true 则同时删除未列出的设备
```

### 3.3 语义（必须保持）

| 行为 | 规则 | 理由 |
|---|---|---|
| 小节缺省 | 不触碰 | 局部下发不能意外清空其他配置 |
| `devices` | 按 `id` upsert | 幂等；云端可重复下发同一份文档 |
| `enabled` 缺省 | 视为 `true` | 只显式写 `false` 才禁用 |
| `replace_devices` 缺省 | `false` | 批量解绑必须显式声明 |
| 配置校验 | 先校验后落盘 | 坏配置不改变设备状态 |
| 未知键 | 警告并忽略，不报错 | 向前兼容：新固件可下发含未来字段的文档 |
| 敏感值 | 日志只打印 `<set>` | 不把密码写进串口日志 |
| 网络变更 | 回执 `reboot_required: true` | MQTT/Wi-Fi 参数在连接建立时读取 |
| 应用结果 | 必定发布 `<prefix>/config/result` | 云端需要知道成功与否 |

### 3.4 回执格式

```json
{
  "ok": true,
  "applied": { "added": 2, "updated": 1, "removed": 1, "failed": 0 },
  "node_changed": false,
  "network_changed": true,
  "reboot_required": true
}
```

失败时附 `"error"`，例如：

```json
{ "ok": false, "applied": {"added":0,"updated":0,"removed":0,"failed":1},
  "error": "device 'sr1': ESP_ERR_INVALID_ARG" }
```

### 3.5 完整示例

```bash
PREFIX=espx/84C7BB772E74

mosquitto_pub -h $BROKER -t "$PREFIX/cmd/config" -f plant-line1.yaml
mosquitto_sub -h $BROKER -t "$PREFIX/config/result" -C 1
```

```yaml
# plant-line1.yaml
node:
  name: Line-1
network:
  mqtt_broker: mqtt://broker.plant:1883
  mqtt_topic_prefix: plant/line1
devices:
  - id: temp_in
    type: dht11
    config: {gpio: 4, interval_ms: 5000}
  - id: relay_a
    type: relay
    config: {gpio: 5, active_level: 1}
```

---

## 4. 硬性约束

### 4.1 外设

* 每个驱动实现 `device_type_t`，只通过 `capabilities` 声明自己（READ/WRITE/NOTIFY/PERIODIC）。
* 驱动**不做**策略决策：不决定何时上报、是否告警、如何命名。那是配置与云端的事。
* 驱动必须提供 `get_default_config()`，让配置端知道该填什么字段。
* 驱动能用 `validate_config()` 拒绝非法参数，就必须实现它。
* 新增驱动必须：写一个 `.c/.h` → 在 `peripherals_register_all()` 注册 → 加进 `main/CMakeLists.txt`。不得修改 core。

### 4.2 配置

* 只有一个应用函数：`config_apply()`。任何新通道都必须复用它，不得另写一套。
* 配置必须能被完整导出（`GET /api/config` / `{"action":"get_config"}`）。
* 持久化后立即生效，不需要重启（网络类参数除外）。

### 4.3 稳定性

* 配置错误不得导致崩溃。非法 YAML/JSON → 返回错误，节点继续运行。
* 配置错误不得造成部分生效后静默丢弃：设备级失败要计数并上报 `failed`。
* OTA 必须校验基线：只接受针对**当前运行固件**生成的差分包。

### 4.4 安全

* 不得在日志中打印密码、密钥。
* 自签名证书是**开发默认值**，量产必须替换（`tools/gen_certs.sh`）。
* 当前 REST API **无鉴权**（见 `docs/ARCHITECTURE.md` 已知限制 #1）。在生产网络中暴露前必须补上。

---

## 5. 分层（依赖只能向下）

```
        config_apply            配置语义（唯一真相入口）      [config/]
       /     |      \
   mqtt     https    console     三条通道，只做解析与转发
       \     |      /
        yaml (可选)              YAML → cJSON，不依赖 ESP-IDF   [config/yaml.c]
             |
  device_manager · node_config · event_bus   [device/ · config/]
             |
       device_type 目录                              [device/]
             |
        peripherals 驱动                            [peripherals/]
             |
          ESP-IDF HAL
```

规则：

* `config/yaml.c` 只依赖 cJSON 与 libc —— **必须可主机单元测试**。任何 ESP-IDF 依赖都不许进。
* 通道层（mqtt/https/console）不含配置语义，只做解析 + 调用 `config_apply()` + 回执。
* 驱动不反向依赖 `device_manager`（`device_manager` 调用驱动的函数指针，反向只需 `device_t` 定义）。

---

## 6. 工作方式

* 编译必须 **0 error / 0 warning**（`-Werror` 生效）。
* 目标芯片固定在 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET`；删掉 `sdkconfig` 后重建也必须仍是 `esp32s3`。
* 证书、网页等资源以**项目文件**形式维护，构建时嵌入（CMake `EMBED_FILES`），**不得把内容写进 C 代码**。
* 提交前跑：
  * 主机单元测试：`tests/run_yaml_tests.sh`（内部使用 `main/config/yaml.c`）
  * 设备回归：`python3 tools/espx_test.py --port <port> all`
  * 网络回归：`python3 tools/network_tests.py --device-ip <ip> --local-ip <ip> --patch <p>`
* 每个行为变更同步更新 `docs/ARCHITECTURE.md`；每个新增用例同步更新 `docs/TESTING.md`。

---

## 7. 术语

| 术语 | 含义 |
|---|---|
| 节点 / node | 一台运行 ESPX 的设备实例 |
| 驱动 / device_type | 一类硬件的能力描述与实现 |
| 设备 / device | 驱动的一个运行时实例，有唯一 `id` |
| 绑定 / binding | 把某个驱动实例绑到具体引脚与参数上，即一条 `devices` 条目 |
| 配置文档 | YAML 或 JSON，含 `node`/`network`/`devices` 等小节 |
| 工厂数据 / mfg_data | 产线写入、开机应用一次即清除的预置配置 |

---

## 8. 非目标

以下明确**不做**，避免偏离方向：

* 不做产品专属固件分支。
* 不在驱动里做业务规则（告警阈值、上报策略、联动逻辑）。
* 不引入需要编译期选择外设组合的机制（`#ifdef PRODUCT_X`）。
* 不把 YAML 支持做成完整 YAML 1.1/1.2 实现；只支持配置子集，超出子集必须**报错**而不是猜测。
