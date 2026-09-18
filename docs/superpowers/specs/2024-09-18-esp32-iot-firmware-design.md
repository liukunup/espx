# ESP32 IoT 传感器/执行器固件架构设计

**版本**: v1.0  
**日期**: 2024-09-18  
**目标芯片**: ESP32 / ESP32-S3 / ESP32-C6 系列  
**ESP-IDF**: v6.1

---

## 1. 概述

### 1.1 项目背景

设计一套企业级 ESP32 IoT 设备固件，适用于传感器数据采集和执行器控制场景。要求高可靠性、高安全性、可远程运维管理。

### 1.2 设计目标

- **安全**: 固件签名验签、配置加密存储、安全启动
- **可靠**: 多层看门狗、异常自动恢复、断线自动重连
- **可观测**: 指标采集、远程命令执行、日志上报
- **可运维**: 动态配置、批量配置、灰度发布、自动回滚
- **可生产**: 工厂测试模式、唯一设备 ID

---

## 2. 系统架构

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                           应用层 (Application)                        │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┤
│  Sensor     │  Actuator   │  Data       │  Remote     │  Factory    │
│  Manager    │  Manager    │  Collector  │  Commander  │  Test       │
├─────────────┴─────────────┴─────────────┴─────────────┴─────────────┤
│                        核心服务层 (Core Services)                      │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┤
│   Config    │   MQTT      │    OTA      │   Health    │   Event     │
│   Manager   │   Client    │   Manager   │   Monitor   │   Loop      │
├─────────────┴─────────────┴─────────────┴─────────────┴─────────────┤
│                         基础组件层 (Components)                       │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┤
│  NVS        │  Security   │  Network    │   Log       │   Timer     │
│  Storage    │  (TLS/JWT)  │  Manager    │   System    │   Manager   │
└─────────────┴─────────────┴─────────────┴─────────────┴─────────────┘
```

### 2.2 目录结构

```
esp32-iot-firmware/
├── CMakeLists.txt                    # 项目级 CMake
├── sdkconfig.defaults                # 默认配置
├── partitions.csv                    # 分区表 (含 A/B OTA)
│
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c                   # 应用程序入口
│
├── components/
│   │
│   ├── core/                        # 核心服务层
│   │   ├── config_manager/          # 配置管理
│   │   ├── mqtt_client/             # MQTT 客户端
│   │   ├── ota_manager/            # OTA 管理
│   │   ├── health_monitor/          # 健康监控
│   │   ├── event_loop/             # 事件循环
│   │   └── remote_cmd/              # 远程命令
│   │
│   ├── drivers/                     # 驱动层
│   │   ├── sensor_manager/         # 传感器管理
│   │   └── actuator_manager/       # 执行器管理
│   │
│   ├── utils/                       # 工具层
│   │   ├── nvs_storage/            # NVS 存储
│   │   ├── security/               # 安全组件
│   │   ├── log_system/             # 日志系统
│   │   ├── network_manager/        # 网络管理
│   │   └── timer_manager/          # 定时器
│   │
│   └── provisioning/               # 配网组件
│       └── ap_provisioning/        # AP 配网
│
├── esp-sec/
│   ├── signing-keys/               # OTA 签名密钥 (gitignore)
│   └── esp_cert.pem                 # CA 证书
│
└── tests/
    ├── unit/                        # 单元测试
    └── integration/                 # 集成测试
```

---

## 3. 组件详细设计

### 3.1 Config Manager

**职责**: 统一管理所有配置，支持热更新和加密存储。

**存储结构 (NVS)**:

```c
// NVS Namespace: "config"
typedef struct {
    // WiFi 配置
    char wifi_ssid[32];
    char wifi_password[64];       // 加密存储

    // MQTT 配置
    char mqttBroker[128];
    int  mqttPort;
    char mqttUsername[32];
    char mqttPassword[64];         // 加密存储
    int  mqttKeepalive;
    char mqttClientId[32];

    // 设备信息
    char deviceId[32];             // 唯一设备 ID
    char deviceName[64];
    char firmwareVersion[16];     // 固件版本

    // 运行时配置
    int  telemetryInterval;        // 数据上报间隔 (ms)
    int  heartbeatInterval;        // 心跳间隔 (ms)
    int  reconnectBaseDelay;       // 重连基础延迟 (ms)
    int  reconnectMaxDelay;        // 重连最大延迟 (ms)

    // 校准参数
    float sensorCalibration[8];   // 传感器校准系数

    // 配置版本
    int configVersion;
    int configChecksum;
} device_config_t;
```

**API 接口**:

```c
// 初始化
int config_manager_init(void);

// 读取配置
int config_get_string(const char *key, char *value, size_t len);
int config_get_int(const char *key, int *value);
int config_get_float(const char *key, float *value);

// 写入配置
int config_set_string(const char *key, const char *value);
int config_set_int(const char *key, int value);
int config_set_float(const char *key, float value);

// 配置加密 (用于敏感字段)
int config_set_encrypted(const char *key, const char *value);
int config_get_decrypted(const char *key, char *value, size_t len);

// 热更新
int config_apply_update(const char *json_payload);
int config_rollback(void);

// 批量操作
int config_batch_apply(const char *json_payload);
int config_export(char *json_buffer, size_t len);
```

### 3.2 MQTT Client

**职责**: MQTT 通信、消息路由、心跳保活。

**连接参数**:

| 参数 | 默认值 | 说明 |
|------|--------|------|
| Broker | mqtt://192.168.1.100:8883 | EMQX 地址 |
| KeepAlive | 60s | MQTT KeepAlive |
| CleanSession | false | 持久会话 |
| QoS | 1 | 至少一次 |

**Topic 规范**:

```
// 设备 → 云端
/device/{device_id}/telemetry      // 传感器数据上报
/device/{device_id}/status         // 设备状态/心跳
/device/{device_id}/log            // 日志上报 (分级别)
// /device/{device_id}/event        // 事件上报
/device/{device_id}/cmd/response   // 命令响应
/device/{device_id}/ota/progress    // OTA 进度
/device/{device_id}/config/ack     // 配置响应

// 云端 → 设备
/device/{device_id}/cmd            // 下发命令
/device/{device_id}/config         // 配置下发
/device/{device_id}/ota/start      // OTA 开始
/device/{device_id}/ota/data       // OTA 数据块
/device/{device_id}/ota/end        // OTA 完成
```

**状态机**:

```
                        ┌──────────┐
                        │  IDLE    │
                        └────┬─────┘
                             │ connect()
                             ▼
                        ┌──────────┐
           ┌─────────── │ CONNECTING │
           │            └────┬──────┘
           │ fail            │ success
           ▼                 ▼
     ┌──────────┐     ┌──────────┐
     │ RETRY    │     │ CONNECTED │
     │ (backoff)│     └────┬──────┘
     └────┬─────┘          │ disconnect()
           │                ▼
           │           ┌──────────┐
           └──────────►│DISCONNECT│
                       └──────────┘
```

**API 接口**:

```c
// 初始化
int mqtt_client_init(void);
int mqtt_client_start(void);
int mqtt_client_stop(void);

// 发布消息
int mqtt_publish(const char *topic, const char *data, int len, int qos);
int mqtt_publish_telemetry(const char *json_data);
int mqtt_publish_status(const char *json_status);
int mqtt_publish_log(int level, const char *message);

// 订阅回调
typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int len);
int mqtt_subscribe(const char *topic, mqtt_message_cb_t callback);

// 连接状态
typedef enum {
    MQTT_STATE_IDLE,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;
mqtt_state_t mqtt_get_state(void);
```

### 3.3 OTA Manager

**职责**: 固件升级、签名验签、双分区切换、灰度发布、自动回滚。

**分区布局**:

```c
// partitions.csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
app_ota0, app,  ota_0,   0x10000, 0x3E0000,
app_ota1, app,  ota_1,   0x3F0000,0x3E0000,
```

**OTA 流程**:

```
云端触发                      设备执行
────────                     ────────
ota/start ─────────────────► 解析固件元数据
   {                          - 目标版本
    version,                   - 固件大小
    size,                     - SHA256 校验
    sha256,                  - 签名
    signature,               - 灰度验证
    flags                    }      │
   }                           ├────┴────┐
                               │ 验证通过 │
                               └────┬────┘
ota/data ──────────────────────► 下载固件到 B 分区
   (分块传输)                      │
   ...                        写入 flash
                               │
ota/end ──────────────────────► SHA256 校验
                               │ 签名验签
                               │
                               ▼
                          ┌──────────┐
                          │ 设置 boot │
                          │ 标记      │
                          └────┬─────┘
                               │
                               ▼
                          重启设备
                          ┌──────────┐
                          │ OTA 验证 │
                          │ 成功?    │
                          └────┬─────┘
                               │
                    ┌─────────┴─────────┐
                    │                   │
                   YES                 NO
                    │                   │
                    ▼                   ▼
              ┌──────────┐        ┌──────────┐
              │ 升级成功 │        │ 自动回滚 │
              │ 确认标记 │        │ 恢复旧固件│
              └──────────┘        └──────────┘
```

**签名验签流程**:

```c
// OTA 固件格式
typedef struct {
    uint32_t magic;           // 0xELOG (0x454C4F47)
    uint32_t version;         // 固件版本
    uint32_t size;            // 固件大小
    uint8_t  sha256[32];      // 固件 SHA256
    uint8_t  signature[256];   // RSA-2048 签名
    uint8_t  reserved[252];
    uint8_t  firmware[];      // 固件本体
} __attribute__((packed)) ota_header_t;

// 验签过程
bool ota_verify_signature(const uint8_t *firmware, size_t size,
                          const uint8_t *signature,
                          const uint8_t *public_key);
```

**灰度发布**:

```c
typedef struct {
    char target_version[16];
    int  total_devices;
    int  current_batch;      // 当前批次
    int  total_batches;       // 总批次数
    int  batch_percentage;    // 本批次百分比
    uint32_t start_time;      // 开始时间
    uint32_t timeout;         // 超时时间
} ota_rollout_t;

// 设备验证明
bool ota_check_rollout(const ota_rollout_t *rollout);
```

**API 接口**:

```c
// OTA 状态
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_WRITING,
    OTA_STATE_BOOTLOADER_UPDATE,
    OTA_STATE_REBOOTING,
    OTA_STATE_COMMITTED,
    OTA_STATE_ROLLBACK,
    OTA_STATE_FAILED
} ota_state_t;

// 初始化
int ota_manager_init(void);

// MQTT OTA 回调
int ota_handle_start(const char *json_payload);
int ota_handle_data(const char *topic, const uint8_t *data, int len);
int ota_handle_end(const char *json_payload);

// HTTP OTA (备选)
int ota_start_http(const char *url);

// 状态查询
ota_state_t ota_get_state(void);
int ota_get_progress(void);  // 0-100%
int ota_get_last_error(void);

// 回滚
int ota_rollback(void);
int ota_commit(void);
```

### 3.4 Health Monitor

**职责**: 看门狗、心跳、状态监控、异常检测。

**多层看门狗**:

```c
// 看门狗配置
typedef struct {
    TaskHandle_t task_handle;
    uint32_t timeout_ms;
    const char *task_name;
} watchdog_config_t;

// 默认配置
static const watchdog_config_t g_watchdogs[] = {
    { .task_name = "mqtt",     .timeout_ms = 30000 },
    { .task_name = "sensor",    .timeout_ms = 10000 },
    { .task_name = "ota",       .timeout_ms = 120000 },
    { .task_name = "event",     .timeout_ms = 5000 },
};
```

**心跳机制**:

```c
typedef struct {
    uint32_t timestamp;
    uint8_t  battery_level;    // 电量 (0-100)
    int8_t   rssi;            // WiFi 信号强度
    uint32_t free_heap;
    uint32_t uptime;          // 运行时间
    uint8_t  error_count;     // 错误计数
    uint8_t  reconnect_count; // 重连次数
} heartbeat_t;

// 定期上报心跳
void heartbeat_send(void);
```

**异常恢复**:

```c
typedef enum {
    RESET_REASON_POWER_ON,      // 上电重启
    RESET_REASON_SW,            // 软件重启
    RESET_REASON_PANIC,         //  panic
    RESET_REASON_EXCEPTION,     // 异常
    RESET_REASON_WDT,           // 看门狗
    RESET_REASON_BROWNOUT,      // 掉电
    RESET_REASON_OTA,           // OTA 升级
} reset_reason_t;

// 记录重启原因
void log_reset_reason(void);
// 保存 crash 日志到 NVS
void save_crash_log(const char *log, size_t len);
// 上报异常事件
void report_exception(reset_reason_t reason);
```

**指标采集**:

```c
typedef struct {
    uint32_t timestamp;
    // 系统指标
    uint32_t free_heap_min;
    uint32_t free_heap_current;
    float cpu_temp;
    // 网络指标
    int mqtt_reconnect_count;
    uint32_t mqtt_packets_sent;
    uint32_t mqtt_packets_failed;
    uint32_t avg_latency_ms;
    // 应用指标
    uint32_t sensor_read_count;
    uint32_t sensor_error_count;
    float sensor_success_rate;
    // OTA 指标
    uint32_t ota_success_count;
    uint32_t ota_fail_count;
} metrics_t;

int metrics_collect(metrics_t *m);
int metrics_export_json(char *buf, size_t len);
```

**API 接口**:

```c
// 初始化
int health_monitor_init(void);

// 看门狗
int watchdog_register(const char *task_name, uint32_t timeout_ms);
int watchdog_feed(const char *task_name);
int watchdog_unregister(const char *task_name);

// 状态上报
int health_report_status(void);
int health_report_metrics(void);

// 异常处理
void register_panic_handler(void);
int health_get_reset_count(void);
reset_reason_t health_get_last_reset_reason(void);
```

### 3.5 Event Loop

**职责**: 事件分发、状态机管理、任务协调。

**事件类型**:

```c
typedef enum {
    // 网络事件
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,

    // 数据事件
    EVENT_SENSOR_DATA_READY,
    EVENT_TELEMETRY_SEND,
    EVENT_DATA_BUFFER_FULL,

    // OTA 事件
    EVENT_OTA_START,
    EVENT_OTA_PROGRESS,
    EVENT_OTA_COMPLETE,
    EVENT_OTA_FAILED,
    EVENT_OTA_ROLLBACK,

    // 配置事件
    EVENT_CONFIG_UPDATED,
    EVENT_CONFIG_REVERT,

    // 命令事件
    EVENT_CMD_RECEIVED,
    EVENT_CMD_EXECUTED,

    // 系统事件
    EVENT_LOW_MEMORY,
    EVENT_WATCHDOG_TIMEOUT,
    EVENT_EXCEPTION,
} event_type_t;

// 事件结构
typedef struct {
    event_type_t type;
    uint32_t timestamp;
    void *data;
    size_t data_len;
} event_t;
```

**API 接口**:

```c
// 初始化
int event_loop_init(void);

// 事件发布
int event_publish(event_type_t type, const void *data, size_t len);

// 事件订阅
typedef void (*event_handler_t)(const event_t *event);
int event_subscribe(event_type_t type, event_handler_t handler);

// 事件循环任务
void event_loop_task(void *params);
```

### 3.6 Remote Commander

**职责**: 接收云端命令、执行、返回结果。

**预定义命令**:

```c
typedef enum {
    CMD_REBOOT,              // 重启设备
    CMD_RESET_CONFIG,        // 重置配置
    CMD_FACTORY_RESET,      // 恢复出厂
    CMD_READ_SENSOR,        // 读取传感器
    CMD_CONTROL_ACTUATOR,   // 控制执行器
    CMD_SET_LED,            // 控制 LED
    CMD_READ_LOG,           // 读取日志
    CMD_EXECUTE_TEST,       // 执行测试
    CMD_READ_STATUS,        // 读取状态
    CMD_CUSTOM              // 自定义命令
} cmd_type_t;

// 命令结构
typedef struct {
    char cmd_id[32];        // 命令唯一 ID
    cmd_type_t type;
    char params[256];       // JSON 参数
    uint32_t timestamp;
    uint32_t timeout_ms;
} command_t;

// 命令响应
typedef struct {
    char cmd_id[32];
    int code;               // 0 = 成功
    char message[128];
    char result[512];       // JSON 结果
    uint32_t timestamp;
} cmd_response_t;
```

**API 接口**:

```c
// 初始化
int remote_cmd_init(void);

// 注册命令处理器
typedef int (*cmd_handler_t)(const char *params, char *result, size_t len);
int cmd_register(cmd_type_t type, cmd_handler_t handler);

// 执行命令
int cmd_execute(const command_t *cmd, cmd_response_t *response);

// 响应云端
int cmd_response_publish(const cmd_response_t *response);
```

### 3.7 Log System

**职责**: 分级日志、本地缓冲、远程上报。

**日志级别**:

```c
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_INFO  = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE = 5
} log_level_t;

// 日志格式
// [LEVEL] [TAG] [TIME] message
// [E] [mqtt] [12:34:56.123] Connection failed: timeout
```

**本地存储**:

```c
typedef struct {
    uint32_t index;
    log_level_t level;
    uint32_t timestamp;
    char tag[16];
    char message[128];
} log_entry_t;

// 本地缓冲 (Ring Buffer)
#define LOG_BUFFER_SIZE  512
```

**API 接口**:

```c
// 初始化
int log_system_init(void);

// 日志输出
#define LOGE(tag, format, ...)  log_write(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOGW(tag, format, ...)  log_write(LOG_LEVEL_WARN,  tag, format, ##__VA_ARGS__)
#define LOGI(tag, format, ...)  log_write(LOG_LEVEL_INFO,  tag, format, ##__VA_ARGS__)
#define LOGD(tag, format, ...)  log_write(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOGV(tag, format, ...)  log_write(LOG_LEVEL_VERBOSE, tag, format, ##__VA_ARGS__)

// 读取日志
int log_read_entries(log_entry_t *entries, int max_count);
int log_export_recent(char *buffer, size_t len);

// 日志级别控制
int log_set_level(log_level_t level);
```

### 3.8 Network Manager

**职责**: WiFi 连接管理、断线重连、指数退避。

**重连策略**:

```c
typedef struct {
    uint32_t base_delay_ms;    // 基础延迟: 1000ms
    uint32_t max_delay_ms;     // 最大延迟: 60000ms
    uint8_t  backoff_factor;   // 退避因子: 2
    uint32_t max_retries;      // 最大重试: 0 (无限)
} reconnect_policy_t;

// 默认配置
static const reconnect_policy_t DEFAULT_RECONNECT = {
    .base_delay_ms = 1000,
    .max_delay_ms = 60000,
    .backoff_factor = 2,
    .max_retries = 0
};
```

**API 接口**:

```c
// 初始化
int network_manager_init(void);

// 连接管理
int network_connect(const char *ssid, const char *password);
int network_disconnect(void);
int network_reconnect(void);

// 状态查询
typedef enum {
    NETWORK_STATE_IDLE,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_FAILED
} network_state_t;
network_state_t network_get_state(void);
int network_get_rssi(void);

// WiFi 事件回调
void network_register_callback(void (*callback)(network_state_t state, void *data));
```

### 3.9 NVS Storage

**职责**: 键值存储、数据加密。

**加密方案**:

```c
// 使用 ESP32 的 Flash 加密 + NVS AES
// 配置加密密钥 (烧录时写入)
#define NVS_ENCRYPTION_KEY_LEN 32
extern const uint8_t nvs_encryption_key[NVS_ENCRYPTION_KEY_LEN];

// 加密存储接口
int nvs_set_encrypted(const char *namespace, const char *key,
                      const void *value, size_t len);
int nvs_get_decrypted(const char *namespace, const char *key,
                       void *value, size_t len);
```

**API 接口**:

```c
// 基础操作
int nvs_init(void);
int nvs_set_string(const char *key, const char *value);
int nvs_get_string(const char *key, char *value, size_t len);
int nvs_set_blob(const char *key, const void *value, size_t len);
int nvs_get_blob(const char *key, void *value, size_t len);
int nvs_erase(const char *key);
int nvs_erase_all(void);
```

### 3.10 Security

**职责**: TLS/SSL、OTA 签名验签、安全启动配置。

**TLS 配置**:

```c
// MQTT TLS 配置
typedef struct {
    bool enable;
    char *ca_cert;           // CA 证书
    char *client_cert;       // 客户端证书
    char *client_key;       // 私钥
    bool skip_common_name_check;
} tls_config_t;

// 默认使用 mbedTLS
const tls_config_t DEFAULT_TLS = {
    .enable = true,
    .ca_cert = NULL,         // 从文件系统加载
    .client_cert = NULL,
    .client_key = NULL,
    .skip_common_name_check = false
};
```

**OTA 签名**:

```c
// 签名验证公钥 (预置在固件中)
extern const uint8_t ota_public_key[];

// 验签函数
bool security_verify_signature(const uint8_t *data, size_t data_len,
                                const uint8_t *signature,
                                const uint8_t *public_key);

// 安全启动 (编译配置)
#define CONFIG_SECURE_BOOT_ENABLED 1
#define CONFIG_SECURE_BOOT_V2_ENABLED 1
#define CONFIG_ESP32_DEBUG_OCDAWARE 1
```

### 3.11 Provisioning

**职责**: AP 配网、烧录配置。

**AP 配网流程**:

```
设备启动
    │
    ▼
检查 NVS 是否有 WiFi 配置
    │
    ├── 有 ──► 连接 WiFi ──► 启动 MQTT
    │
    └── 无 ──► 启动 AP 模式 (192.168.4.1)
                   │
                   ▼
              创建配网页面
                   │
                   ▼
              接收 WiFi SSID/Password
                   │
                   ▼
              保存到 NVS (加密)
                   │
                   ▼
              重启或连接 WiFi
```

**API 接口**:

```c
// 配网状态
typedef enum {
    PROV_STATE_IDLE,
    PROV_STATE_AP_READY,
    PROV_STATE_RECEIVING,
    PROV_STATE_COMPLETE,
    PROV_STATE_FAILED
} provisioning_state_t;

// 启动配网
int provisioning_start_ap(void);
int provisioning_stop(void);

// 配网页面
int provisioning_handle_request(httpd_req_t *req);

// 状态查询
provisioning_state_t provisioning_get_state(void);
```

### 3.12 Factory Test

**职责**: 生产测试指令、自动化校准。

**测试命令**:

```c
typedef enum {
    TEST_WIFI,              // WiFi 连接测试
    TEST_MQTT,              // MQTT 连接测试
    TEST_SENSOR,             // 传感器读取测试
    TEST_ACTUATOR,           // 执行器测试
    TEST_LED,                // LED 测试
    TEST_FLASH,              // Flash 测试
    TEST_OTA,                // OTA 测试
    TEST_FULL,               // 完整测试
    TEST_CALIBRATE           // 校准
} test_type_t;

// 测试结果
typedef struct {
    test_type_t type;
    bool passed;
    char message[256];
    int duration_ms;
} test_result_t;
```

**API 接口**:

```c
// 执行测试
int factory_test_run(test_type_t type, test_result_t *result);
int factory_test_run_all(test_result_t *results, int *count);

// 校准
int factory_calibrate_sensor(const char *params);
int factory_calibrate_save(void);
```

---

## 4. 数据结构定义

### 4.1 遥测数据格式

```json
{
  "device_id": "ESP32-001122334455",
  "timestamp": 1695043200,
  "version": "1.0.0",
  "sensors": [
    {
      "name": "temperature",
      "type": "float",
      "value": 25.6,
      "unit": "℃"
    },
    {
      "name": "humidity",
      "type": "float",
      "value": 65.2,
      "unit": "%"
    }
  ],
  "status": {
    "rssi": -65,
    "uptime": 86400,
    "free_heap": 150000
  }
}
```

### 4.2 设备状态格式

```json
{
  "device_id": "ESP32-001122334455",
  "timestamp": 1695043200,
  "online": true,
  "firmware_version": "1.0.0",
  "config_version": 5,
  "wifi": {
    "ssid": "MyWiFi",
    "rssi": -65,
    "channel": 6
  },
  "mqtt": {
    "connected": true,
    "uptime": 3600,
    "reconnect_count": 2
  },
  "ota": {
    "state": "idle",
    "last_version": "1.0.0",
    "last_success_time": 1695000000
  },
  "metrics": {
    "free_heap_min": 100000,
    "sensor_success_rate": 99.8,
    "error_count": 0
  }
}
```

### 4.3 命令响应格式

```json
{
  "cmd_id": "uuid-xxxx-xxxx",
  "device_id": "ESP32-001122334455",
  "timestamp": 1695043200,
  "code": 0,
  "message": "success",
  "result": {
    "key": "value"
  }
}
```

---

## 5. 错误码定义

```c
typedef enum {
    // 成功
    ESP_OK = 0,

    // 通用错误 (1xxx)
    ERR_INVALID_PARAM = 1001,
    ERR_NO_MEMORY = 1002,
    ERR_TIMEOUT = 1003,
    ERR_NOT_FOUND = 1004,
    ERR_BUSY = 1005,

    // 配置错误 (2xxx)
    ERR_CONFIG_INVALID = 2001,
    ERR_CONFIG_CORRUPT = 2002,
    ERR_CONFIG_VERSION = 2003,

    // 网络错误 (3xxx)
    ERR_WIFI_CONNECT = 3001,
    ERR_WIFI_TIMEOUT = 3002,
    ERR_MQTT_CONNECT = 3003,
    ERR_MQTT_PUBLISH = 3004,
    ERR_MQTT_SUBSCRIBE = 3005,

    // OTA 错误 (4xxx)
    ERR_OTA_VERSION = 4001,
    ERR_OTA_DOWNLOAD = 4002,
    ERR_OTA_VERIFY = 4003,
    ERR_OTA_SIGNATURE = 4004,
    ERR_OTA_WRITE = 4005,
    ERR_OTA_REBOOT = 4006,

    // 安全错误 (5xxx)
    ERR_SECURITY_INIT = 5001,
    ERR_SECURITY_TLS = 5002,
    ERR_SECURITY_SIGN = 5003,

    // 传感器错误 (6xxx)
    ERR_SENSOR_NOT_FOUND = 6001,
    ERR_SENSOR_READ = 6002,
    ERR_SENSOR_CALIBRATION = 6003,
} error_code_t;
```

---

## 6. 编译配置

### 6.1 sdkconfig 关键配置

```bash
# 安全配置
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_SIGNED_APPS=y
CONFIG_SECURE_SIGNED_APP_RSA_SCHEME="RSA2048"
CONFIG_SECURE_SIGNED_APP_ECDSA_SCHEME="ECDSA P256"

# OTA 配置
CONFIG_ESP_OTA_PARTITION_TABLE_CUSTOM=y
CONFIG_ESP_OTA_PARTITION_TABLE_CUSTOM_APP_B=y
CONFIG_ESP_OTA_FORCE_TEST_B_PARTITION=y

# WiFi 配置
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_TX_BUFFER_TYPE=1

# MQTT 配置
CONFIG_MQTT_TRANSPORT_SSL=y
CONFIG_MQTT_PROTOCOL_311=y
CONFIG_MQTT_TASK_STACK_SIZE=4096

# 日志配置
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# Flash 加密
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC=n
```

---

## 7. 实现优先级

| 优先级 | 组件 | 说明 |
|--------|------|------|
| P0 | Event Loop | 核心事件分发 |
| P0 | NVS Storage | 基础存储 |
| P0 | Log System | 日志输出 |
| P0 | Network Manager | WiFi 连接 |
| P0 | MQTT Client | 通信基础 |
| P1 | Config Manager | 配置管理 |
| P1 | Health Monitor | 看门狗/心跳 |
| P1 | OTA Manager | OTA 核心 |
| P1 | Remote Commander | 命令执行 |
| P2 | Security | 安全组件 |
| P2 | Provisioning | 配网功能 |
| P2 | Factory Test | 工厂测试 |
| P3 | Sensor/Actuator | 传感器驱动 |

---

## 8. 后续迭代

- [ ] 低功耗模式支持
- [ ] 差分 OTA
- [ ] 设备影子
- [ ] SD 卡日志归档
- [ ] 多协议支持 (CoAP)
