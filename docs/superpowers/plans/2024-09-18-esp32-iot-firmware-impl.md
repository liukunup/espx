# ESP32 IoT 固件实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现企业级 ESP32 IoT 传感器/执行器固件，包含配网、OTA、安全、高可靠等完整功能。

**Architecture:** 基于 ESP-IDF v6.1，采用分层架构：应用层 → 核心服务层 → 基础组件层。各组件独立，通过事件总线通信。

**Tech Stack:** ESP-IDF v6.1, FreeRTOS, MQTT (esp-mqtt), NVS, mbedTLS

**Spec:** `docs/superpowers/specs/2024-09-18-esp32-iot-firmware-design.md`

---

## 全局约束

| 约束项 | 值 |
|--------|-----|
| ESP-IDF 版本 | v6.1 |
| 目标芯片 | ESP32 / ESP32-S3 / ESP32-C6 |
| MQTT 版本 | v3.1.1 |
| OTA 签名 | RSA-2048 |
| 日志级别默认 | INFO |
| 编译工具链 | xtensa-esp-elf |

---

## 项目结构

```
esp32-iot-firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
│
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c
│
├── components/
│   ├── core/               # 核心服务层
│   │   ├── event_loop/
│   │   ├── config_manager/
│   │   ├── mqtt_client/
│   │   ├── ota_manager/
│   │   ├── health_monitor/
│   │   └── remote_cmd/
│   │
│   ├── utils/              # 基础组件层
│   │   ├── nvs_storage/
│   │   ├── log_system/
│   │   ├── network_manager/
│   │   ├── security/
│   │   └── timer_manager/
│   │
│   ├── drivers/            # 驱动层
│   │   ├── sensor_manager/
│   │   └── actuator_manager/
│   │
│   └── provisioning/        # 配网
│       └── ap_provisioning/
│
├── esp-sec/                # 安全文件
│   └── esp_cert.pem
│
└── tests/
    └── unit/
```

---

## 实现阶段

| 阶段 | 组件 | 优先级 | 说明 |
|------|------|--------|------|
| **Phase 1** | Event Loop, NVS, Log, Network | P0 | 基础架构 |
| **Phase 2** | MQTT Client | P0 | 通信核心 |
| **Phase 3** | Config Manager, Health Monitor | P1 | 配置与监控 |
| **Phase 4** | OTA Manager, Security | P1 | OTA 与安全 |
| **Phase 5** | Remote Cmd, Provisioning, Factory Test | P2 | 运维功能 |
| **Phase 6** | Sensor/Actuator Manager | P3 | 传感器驱动 |

---

## Phase 1: 基础架构

### Phase 1.1: Event Loop

**Files:**
- Create: `components/core/event_loop/CMakeLists.txt`
- Create: `components/core/event_loop/event_loop.h`
- Create: `components/core/event_loop/event_loop.c`

**Interfaces:**
- Produces: `event_loop_init()`, `event_publish()`, `event_subscribe()`, `event_type_t`

```c
// event_loop.h
typedef enum {
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_SENSOR_DATA_READY,
    EVENT_TELEMETRY_SEND,
    EVENT_OTA_START,
    EVENT_OTA_PROGRESS,
    EVENT_OTA_COMPLETE,
    EVENT_OTA_FAILED,
    EVENT_CONFIG_UPDATED,
    EVENT_CMD_RECEIVED,
    EVENT_LOW_MEMORY,
    EVENT_WATCHDOG_TIMEOUT,
    EVENT_EXCEPTION,
    EVENT_TYPE_COUNT
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t timestamp;
    void *data;
    size_t data_len;
} event_t;

typedef void (*event_handler_t)(const event_t *event);

int event_loop_init(void);
int event_publish(event_type_t type, const void *data, size_t len);
int event_subscribe(event_type_t type, event_handler_t handler);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 定义事件类型和结构体**
- [ ] **Step 3: 实现事件发布/订阅机制 (Ring Buffer)**
- [ ] **Step 4: 实现事件分发任务**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 1.2: NVS Storage

**Files:**
- Create: `components/utils/nvs_storage/CMakeLists.txt`
- Create: `components/utils/nvs_storage/nvs_storage.h`
- Create: `components/utils/nvs_storage/nvs_storage.c`

**Interfaces:**
- Produces: `nvs_init()`, `nvs_set_string()`, `nvs_get_string()`, `nvs_set_blob()`, `nvs_get_blob()`, `nvs_set_encrypted()`, `nvs_get_decrypted()`, `nvs_erase()`

```c
// nvs_storage.h
#define NVS_NAMESPACE_CONFIG "config"
#define NVS_NAMESPACE_SYSTEM "system"

int nvs_init(void);
int nvs_set_string(const char *key, const char *value);
int nvs_get_string(const char *key, char *value, size_t len);
int nvs_set_int(const char *key, int value);
int nvs_get_int(const char *key, int *value);
int nvs_set_blob(const char *key, const void *value, size_t len);
int nvs_get_blob(const char *key, void *value, size_t len);
int nvs_set_encrypted(const char *key, const void *value, size_t len);
int nvs_get_decrypted(const char *key, void *value, size_t len);
int nvs_erase(const char *key);
int nvs_erase_all(void);
int nvs_commit(void);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现基础读写接口**
- [ ] **Step 3: 实现加密/解密接口 (使用 nvs_flash_secure_cfg)**
- [ ] **Step 4: 单元测试**
- [ ] **Step 5: 提交**

---

### Phase 1.3: Log System

**Files:**
- Create: `components/utils/log_system/CMakeLists.txt`
- Create: `components/utils/log_system/log_system.h`
- Create: `components/utils/log_system/log_system.c`

**Interfaces:**
- Produces: `log_system_init()`, `log_set_level()`, `log_write()`, `log_read_entries()`, `LOGE()`, `LOGW()`, `LOGI()`, `LOGD()`, `LOGV()`

```c
// log_system.h
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE = 5
} log_level_t;

typedef struct {
    uint32_t index;
    log_level_t level;
    uint32_t timestamp;
    char tag[16];
    char message[128];
} log_entry_t;

int log_system_init(void);
int log_set_level(log_level_t level);
int log_write(log_level_t level, const char *tag, const char *format, ...);
int log_read_entries(log_entry_t *entries, int max_count, int start_index);

#define LOGE(tag, format, ...) log_write(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOGW(tag, format, ...) log_write(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOGI(tag, format, ...) log_write(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOGD(tag, format, ...) log_write(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOGV(tag, format, ...) log_write(LOG_LEVEL_VERBOSE, tag, format, ##__VA_ARGS__)
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 定义日志级别和宏**
- [ ] **Step 3: 实现日志写入 (Ring Buffer + 串口输出)**
- [ ] **Step 4: 实现日志读取接口**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 1.4: Network Manager

**Files:**
- Create: `components/utils/network_manager/CMakeLists.txt`
- Create: `components/utils/network_manager/network_manager.h`
- Create: `components/utils/network_manager/network_manager.c`

**Interfaces:**
- Produces: `network_manager_init()`, `network_connect()`, `network_disconnect()`, `network_get_state()`, `network_get_rssi()`, `network_register_callback()`

```c
// network_manager.h
typedef enum {
    NETWORK_STATE_IDLE,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_FAILED
} network_state_t;

typedef enum {
    RECONNECT_POLICY_EXPONENTIAL,
    RECONNECT_POLICY_LINEAR
} reconnect_policy_type_t;

typedef struct {
    uint32_t base_delay_ms;
    uint32_t max_delay_ms;
    uint8_t backoff_factor;
    uint32_t max_retries;
} reconnect_policy_t;

int network_manager_init(void);
int network_connect(const char *ssid, const char *password);
int network_disconnect(void);
int network_reconnect(void);
network_state_t network_get_state(void);
int network_get_rssi(void);
void network_register_callback(void (*callback)(network_state_t state, void *data));
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现 WiFi 事件处理**
- [ ] **Step 3: 实现连接/断开接口**
- [ ] **Step 4: 实现指数退避重连策略**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

## Phase 2: 通信核心

### Phase 2.1: MQTT Client

**Files:**
- Create: `components/core/mqtt_client/CMakeLists.txt`
- Create: `components/core/mqtt_client/mqtt_client.h`
- Create: `components/core/mqtt_client/mqtt_client.c`

**Interfaces:**
- Consumes: `event_publish()`, `network_get_state()`
- Produces: `mqtt_client_init()`, `mqtt_client_start()`, `mqtt_client_stop()`, `mqtt_publish()`, `mqtt_subscribe()`, `mqtt_get_state()`, `mqtt_message_cb_t`

```c
// mqtt_client.h
typedef enum {
    MQTT_STATE_IDLE,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;

typedef enum {
    MQTT_QOS_0,
    MQTT_QOS_1,
    MQTT_QOS_2
} mqtt_qos_t;

typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int len);

int mqtt_client_init(void);
int mqtt_client_start(void);
int mqtt_client_stop(void);
int mqtt_client_reconnect(void);

int mqtt_publish(const char *topic, const char *data, int len, mqtt_qos_t qos);
int mqtt_publish_telemetry(const char *json_data);
int mqtt_publish_status(const char *json_status);
int mqtt_publish_log(int level, const char *message);
int mqtt_publish_cmd_response(const char *cmd_id, int code, const char *message, const char *result);
int mqtt_publish_ota_progress(int progress, const char *message);

int mqtt_subscribe(const char *topic, mqtt_message_cb_t callback);

mqtt_state_t mqtt_get_state(void);

// Topic 宏定义
#define TOPIC_TELEMETRY(device_id) "/device/" device_id "/telemetry"
#define TOPIC_STATUS(device_id) "/device/" device_id "/status"
#define TOPIC_LOG(device_id) "/device/" device_id "/log"
#define TOPIC_CMD(device_id) "/device/" device_id "/cmd"
#define TOPIC_CMD_RESPONSE(device_id) "/device/" device_id "/cmd/response"
#define TOPIC_CONFIG(device_id) "/device/" device_id "/config"
#define TOPIC_OTA_START(device_id) "/device/" device_id "/ota/start"
#define TOPIC_OTA_DATA(device_id) "/device/" device_id "/ota/data"
#define TOPIC_OTA_END(device_id) "/device/" device_id "/ota/end"
#define TOPIC_OTA_PROGRESS(device_id) "/device/" device_id "/ota/progress"
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现 MQTT 配置和初始化 (TLS)**
- [ ] **Step 3: 实现连接/断开/重连**
- [ ] **Step 4: 实现发布/订阅接口**
- [ ] **Step 5: 实现心跳保活**
- [ ] **Step 6: 实现消息路由回调**
- [ ] **Step 7: 单元测试**
- [ ] **Step 8: 提交**

---

## Phase 3: 配置与监控

### Phase 3.1: Config Manager

**Files:**
- Create: `components/core/config_manager/CMakeLists.txt`
- Create: `components/core/config_manager/config_manager.h`
- Create: `components/core/config_manager/config_manager.c`

**Interfaces:**
- Consumes: `nvs_*()`, `event_publish()`
- Produces: `config_manager_init()`, `config_get_string()`, `config_set_string()`, `config_apply_update()`, `config_export()`

```c
// config_manager.h
typedef struct {
    // WiFi
    char wifi_ssid[32];
    char wifi_password[64];
    // MQTT
    char mqtt_broker[128];
    int mqtt_port;
    char mqtt_username[32];
    char mqtt_password[64];
    int mqtt_keepalive;
    char mqtt_client_id[32];
    // 设备
    char device_id[32];
    char device_name[64];
    char firmware_version[16];
    // 运行时
    int telemetry_interval;
    int heartbeat_interval;
    int reconnect_base_delay;
    int reconnect_max_delay;
    // 校准
    float sensor_calibration[8];
    // 版本
    int config_version;
    int config_checksum;
} device_config_t;

int config_manager_init(void);
int config_manager_load(void);
int config_manager_save(void);

int config_get_string(const char *key, char *value, size_t len);
int config_get_int(const char *key, int *value);
int config_get_float(const char *key, float *value);
int config_set_string(const char *key, const char *value);
int config_set_int(const char *key, int value);
int config_set_float(const char *key, float value);

int config_apply_update(const char *json_payload);
int config_rollback(void);
int config_export(char *json_buffer, size_t len);
int config_reset(void);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 定义配置结构体和默认值**
- [ ] **Step 3: 实现 NVS 读写**
- [ ] **Step 4: 实现配置加密存储 (密码等敏感字段)**
- [ ] **Step 5: 实现热更新 (JSON 解析 + 版本校验)**
- [ ] **Step 6: 实现配置导出**
- [ ] **Step 7: 单元测试**
- [ ] **Step 8: 提交**

---

### Phase 3.2: Health Monitor

**Files:**
- Create: `components/core/health_monitor/CMakeLists.txt`
- Create: `components/core/health_monitor/health_monitor.h`
- Create: `components/core/health_monitor/health_monitor.c`

**Interfaces:**
- Consumes: `event_loop`, `mqtt_client`, `config_manager`
- Produces: `health_monitor_init()`, `watchdog_register()`, `watchdog_feed()`, `health_report_status()`, `health_report_metrics()`, `health_get_reset_count()`

```c
// health_monitor.h
typedef enum {
    RESET_REASON_POWER_ON = 0,
    RESET_REASON_SW = 1,
    RESET_REASON_PANIC = 2,
    RESET_REASON_EXCEPTION = 3,
    RESET_REASON_WDT = 4,
    RESET_REASON_BROWNOUT = 5,
    RESET_REASON_OTA = 6,
} reset_reason_t;

typedef struct {
    uint32_t timestamp;
    uint32_t free_heap_min;
    uint32_t free_heap_current;
    float cpu_temp;
    int mqtt_reconnect_count;
    uint32_t mqtt_packets_sent;
    uint32_t sensor_read_count;
    uint32_t sensor_error_count;
    float sensor_success_rate;
} metrics_t;

typedef struct {
    TaskHandle_t task_handle;
    uint32_t timeout_ms;
    const char *task_name;
} watchdog_config_t;

int health_monitor_init(void);

int watchdog_register(const char *task_name, uint32_t timeout_ms);
int watchdog_feed(const char *task_name);
int watchdog_unregister(const char *task_name);

int health_report_status(void);
int health_report_metrics(void);

void register_panic_handler(void);
int health_get_reset_count(void);
reset_reason_t health_get_last_reset_reason(void);
const char* health_get_reset_reason_string(reset_reason_t reason);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现多层看门狗 (系统级 + 任务级)**
- [ ] **Step 3: 实现心跳上报**
- [ ] **Step 4: 实现指标采集 (metrics)**
- [ ] **Step 5: 实现异常处理 (panic handler, crash log)**
- [ ] **Step 6: 实现重启原因记录和上报**
- [ ] **Step 7: 单元测试**
- [ ] **Step 8: 提交**

---

## Phase 4: OTA 与安全

### Phase 4.1: Security

**Files:**
- Create: `components/utils/security/CMakeLists.txt`
- Create: `components/utils/security/security.h`
- Create: `components/utils/security/security.c`
- Create: `esp-sec/ota_public_key.pem` (预置公钥)

**Interfaces:**
- Produces: `security_init()`, `security_verify_signature()`, `security_get_random()`

```c
// security.h
// OTA 签名格式
typedef struct {
    uint32_t magic;           // 0xELOG
    uint32_t version;
    uint32_t size;
    uint8_t  sha256[32];
    uint8_t  signature[256];
    uint8_t  reserved[252];
} __attribute__((packed)) ota_header_t;

#define OTA_HEADER_MAGIC 0x454C4F47

int security_init(void);
bool security_verify_signature(const uint8_t *data, size_t data_len,
                               const uint8_t *signature,
                               const uint8_t *public_key);
int security_get_random(uint8_t *buffer, size_t len);
bool security_check_factory_reset_pin(void);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现 mbedTLS 初始化**
- [ ] **Step 3: 实现 RSA 签名验证 (使用预置公钥)**
- [ ] **Step 4: 实现 SHA256 计算**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 4.2: OTA Manager

**Files:**
- Create: `components/core/ota_manager/CMakeLists.txt`
- Create: `components/core/ota_manager/ota_manager.h`
- Create: `components/core/ota_manager/ota_manager.c`

**Interfaces:**
- Consumes: `mqtt_client`, `security`, `nvs_storage`
- Produces: `ota_manager_init()`, `ota_handle_start()`, `ota_handle_data()`, `ota_handle_end()`, `ota_get_state()`, `ota_rollback()`, `ota_commit()`

```c
// ota_manager.h
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_RECEIVING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_WRITING,
    OTA_STATE_REBOOTING,
    OTA_STATE_COMMITTED,
    OTA_STATE_ROLLBACK,
    OTA_STATE_FAILED
} ota_state_t;

typedef struct {
    char target_version[16];
    uint32_t target_size;
    uint8_t  target_sha256[32];
    uint8_t  signature[256];
    int  total_batches;
    int  current_batch;
} ota_info_t;

typedef struct {
    char target_version[16];
    int  total_devices;
    int  current_batch;
    int  total_batches;
    int  batch_percentage;
    uint32_t start_time;
    uint32_t timeout;
} ota_rollout_t;

int ota_manager_init(void);

int ota_handle_start(const char *json_payload);
int ota_handle_data(const char *topic, const uint8_t *data, int len);
int ota_handle_end(const char *json_payload);

ota_state_t ota_get_state(void);
int ota_get_progress(void);
int ota_get_last_error(void);

int ota_rollback(void);
int ota_commit(void);
bool ota_is_valid_version(const char *version);
const char* ota_get_current_version(void);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现分区切换逻辑**
- [ ] **Step 3: 实现 OTA 下载 (MQTT 分块接收)**
- [ ] **Step 4: 实现 SHA256 校验**
- [ ] **Step 5: 实现签名验证**
- [ ] **Step 6: 实现灰度发布验证**
- [ ] **Step 7: 实现自动回滚**
- [ ] **Step 8: 单元测试**
- [ ] **Step 9: 提交**

---

## Phase 5: 运维功能

### Phase 5.1: Remote Commander

**Files:**
- Create: `components/core/remote_cmd/CMakeLists.txt`
- Create: `components/core/remote_cmd/remote_cmd.h`
- Create: `components/core/remote_cmd/remote_cmd.c`

**Interfaces:**
- Consumes: `mqtt_client`, `event_loop`
- Produces: `remote_cmd_init()`, `cmd_register()`, `cmd_execute()`

```c
// remote_cmd.h
typedef enum {
    CMD_REBOOT = 1,
    CMD_RESET_CONFIG,
    CMD_FACTORY_RESET,
    CMD_READ_SENSOR,
    CMD_CONTROL_ACTUATOR,
    CMD_SET_LED,
    CMD_READ_LOG,
    CMD_EXECUTE_TEST,
    CMD_READ_STATUS,
    CMD_CUSTOM
} cmd_type_t;

typedef struct {
    char cmd_id[32];
    cmd_type_t type;
    char params[256];
    uint32_t timestamp;
    uint32_t timeout_ms;
} command_t;

typedef struct {
    char cmd_id[32];
    int code;
    char message[128];
    char result[512];
    uint32_t timestamp;
} cmd_response_t;

typedef int (*cmd_handler_t)(const char *params, char *result, size_t len);

int remote_cmd_init(void);
int cmd_register(cmd_type_t type, cmd_handler_t handler);
int cmd_execute(const command_t *cmd, cmd_response_t *response);
int cmd_response_publish(const cmd_response_t *response);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现命令注册表**
- [ ] **Step 3: 实现命令执行框架**
- [ ] **Step 4: 实现内置命令处理器 (reboot, status, log)**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 5.2: AP Provisioning

**Files:**
- Create: `components/provisioning/ap_provisioning/CMakeLists.txt`
- Create: `components/provisioning/ap_provisioning/provisioning.h`
- Create: `components/provisioning/ap_provisioning/provisioning.c`

**Interfaces:**
- Produces: `provisioning_start()`, `provisioning_stop()`, `provisioning_get_state()`

```c
// provisioning.h
typedef enum {
    PROV_STATE_IDLE,
    PROV_STATE_AP_READY,
    PROV_STATE_RECEIVING,
    PROV_STATE_COMPLETE,
    PROV_STATE_FAILED
} provisioning_state_t;

typedef struct {
    char ssid[32];
    char password[64];
} wifi_cred_t;

int provisioning_init(void);
int provisioning_start(void);
int provisioning_stop(void);
provisioning_state_t provisioning_get_state(void);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现 AP 模式启动**
- [ ] **Step 3: 实现 HTTP 服务器 (配网页面)**
- [ ] **Step 4: 实现 WiFi 凭证接收和保存**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 5.3: Factory Test

**Files:**
- Create: `components/utils/factory_test/CMakeLists.txt`
- Create: `components/utils/factory_test/factory_test.h`
- Create: `components/utils/factory_test/factory_test.c`

**Interfaces:**
- Produces: `factory_test_init()`, `factory_test_run()`, `factory_test_run_all()`, `factory_calibrate()`

```c
// factory_test.h
typedef enum {
    TEST_WIFI = 1,
    TEST_MQTT,
    TEST_FLASH,
    TEST_SENSOR,
    TEST_ACTUATOR,
    TEST_LED,
    TEST_FULL
} test_type_t;

typedef struct {
    test_type_t type;
    bool passed;
    char message[256];
    int duration_ms;
} test_result_t;

int factory_test_init(void);
int factory_test_run(test_type_t type, test_result_t *result);
int factory_test_run_all(test_result_t *results, int *count);
int factory_calibrate(const char *params, char *result, size_t len);
int factory_generate_device_id(char *buffer, size_t len);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现测试用例 (WiFi, Flash, etc.)**
- [ ] **Step 3: 实现校准流程**
- [ ] **Step 4: 实现设备 ID 生成**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

## Phase 6: 传感器驱动

### Phase 6.1: Sensor Manager

**Files:**
- Create: `components/drivers/sensor_manager/CMakeLists.txt`
- Create: `components/drivers/sensor_manager/sensor_manager.h`
- Create: `components/drivers/sensor_manager/sensor_manager.c`

**Interfaces:**
- Produces: `sensor_init()`, `sensor_read()`, `sensor_read_all()`, `sensor_register()`

```c
// sensor_manager.h
typedef enum {
    SENSOR_TYPE_TEMPERATURE = 1,
    SENSOR_TYPE_HUMIDITY,
    SENSOR_TYPE_PRESSURE,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_DOOR,
    SENSOR_TYPE_MOTION,
    SENSOR_TYPE_CUSTOM
} sensor_type_t;

typedef struct {
    char name[32];
    sensor_type_t type;
    float value;
    char unit[8];
    uint32_t last_read;
    bool valid;
} sensor_data_t;

typedef int (*sensor_read_fn)(float *value);

typedef struct {
    char name[32];
    sensor_type_t type;
    sensor_read_fn read_fn;
    char unit[8];
    float calibration_offset;
    float calibration_scale;
} sensor_config_t;

int sensor_manager_init(void);
int sensor_register(const sensor_config_t *config);
int sensor_read(const char *name, float *value);
int sensor_read_all(sensor_data_t *data, int max_count, int *actual_count);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现传感器注册表**
- [ ] **Step 3: 实现传感器读取接口**
- [ ] **Step 4: 实现校准功能**
- [ ] **Step 5: 单元测试**
- [ ] **Step 6: 提交**

---

### Phase 6.2: Actuator Manager

**Files:**
- Create: `components/drivers/actuator_manager/CMakeLists.txt`
- Create: `components/drivers/actuator_manager/actuator_manager.h`
- Create: `components/drivers/actuator_manager/actuator_manager.c`

**Interfaces:**
- Produces: `actuator_init()`, `actuator_set()`, `actuator_register()`

```c
// actuator_manager.h
typedef enum {
    ACTUATOR_TYPE_LED = 1,
    ACTUATOR_TYPE_RELAY,
    ACTUATOR_TYPE_MOTOR,
    ACTUATOR_TYPE_SERVO,
    ACTUATOR_TYPE_CUSTOM
} actuator_type_t;

typedef int (*actuator_set_fn)(int value);

typedef struct {
    char name[32];
    actuator_type_t type;
    actuator_set_fn set_fn;
    int min_value;
    int max_value;
} actuator_config_t;

int actuator_manager_init(void);
int actuator_register(const actuator_config_t *config);
int actuator_set(const char *name, int value);
int actuator_get(const char *name, int *value);
```

- [ ] **Step 1: 创建目录和 CMakeLists.txt**
- [ ] **Step 2: 实现执行器注册表**
- [ ] **Step 3: 实现执行器控制接口**
- [ ] **Step 4: 单元测试**
- [ ] **Step 5: 提交**

---

## 主程序整合

### App Main

**Files:**
- Create: `main/CMakeLists.txt` (已存在，修改)
- Modify: `main/app_main.c`

```c
// app_main.c
void app_main(void) {
    // 1. 初始化日志
    log_system_init();
    LOGI(TAG, "Starting ESP32 IoT Firmware...");

    // 2. 初始化 NVS
    nvs_init();

    // 3. 加载配置
    config_manager_init();

    // 4. 初始化事件循环
    event_loop_init();

    // 5. 初始化网络
    network_manager_init();

    // 6. 初始化 MQTT
    mqtt_client_init();

    // 7. 初始化健康监控
    health_monitor_init();

    // 8. 初始化 OTA
    ota_manager_init();

    // 9. 初始化传感器/执行器
    sensor_manager_init();
    actuator_manager_init();

    // 10. 初始化远程命令
    remote_cmd_init();

    // 11. 检查是否需要配网
    if (!config_has_wifi()) {
        LOGI(TAG, "No WiFi config, starting provisioning...");
        provisioning_start();
    } else {
        // 连接 WiFi
        network_connect(get_wifi_ssid(), get_wifi_password());
    }

    // 12. 启动主循环
    // (事件循环任务已在各组件初始化时启动)
}
```

- [ ] **Step 1: 创建 sdkconfig.defaults**
- [ ] **Step 2: 创建 partitions.csv**
- [ ] **Step 3: 实现 app_main.c**
- [ ] **Step 4: 配置组件依赖关系**
- [ ] **Step 5: 编译测试**
- [ ] **Step 6: 提交**

---

## 测试策略

| 层级 | 测试方式 |
|------|----------|
| 单元测试 | 各组件独立测试，使用 mock |
| 集成测试 | 组件间交互测试 |
| 系统测试 | 完整功能测试 |
| 压力测试 | 长时间运行测试 |

---

## 提交规范

```
feat: add event_loop component
fix: fix nvs encryption bug
docs: add component documentation
test: add mqtt client unit tests
refactor: improve ota state machine
```

---

## 实现计划总结

| Phase | 任务数 | 预计步骤 |
|-------|--------|----------|
| Phase 1: 基础架构 | 4 | 20 |
| Phase 2: 通信核心 | 1 | 8 |
| Phase 3: 配置与监控 | 2 | 16 |
| Phase 4: OTA 与安全 | 2 | 18 |
| Phase 5: 运维功能 | 3 | 18 |
| Phase 6: 传感器驱动 | 2 | 12 |
| 主程序整合 | 1 | 6 |
| **总计** | **15** | **98** |
