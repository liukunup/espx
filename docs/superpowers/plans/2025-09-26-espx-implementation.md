# ESPX Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 ESPX 固件，支持 HTTPS Web 管理、MQTT 通信、差分 OTA、参数配置、工厂预配置、生产线测试

**Architecture:** 基于 ESP-IDF 6.1，使用 esp_https_server 提供 HTTPS Web 服务，esp_delta_ota 处理差分升级，espressif/mqtt 连接云端，NVS 存储参数和证书

**Tech Stack:** ESP-IDF 6.1, esp_https_server, esp_delta_ota, espressif/mqtt, network_provisioning, NVS

**Spec:** `docs/superpowers/specs/2025-09-26-espx-design.md`

---

## Global Constraints

- 目标硬件: ESP32-S3 R16N8
- ESP-IDF 版本: >=6.1.0
- 组件版本: esp_delta_ota ^1.1.4, espressif/mqtt ^1.1.0, esp_https_server ^1.1.4
- 分区布局: factory + ota_0 + ota_1 (双分区 OTA)
- 证书: RSA 2048-bit 自签名, 有效期10年

---

## Review Focus

| 风险点 | 预期行为 |
|--------|----------|
| 差分包校验失败 | 显示错误信息，可重新下载 |
| MQTT 断线重连 | 自动重连，使用指数退避 |
| OTA 升级中断 | 支持断点续传或重新开始 |
| NVS 读写失败 | 记录日志，使用默认值 |
| 证书生成失败 | 显示错误，重试机制 |

---

## File Structure

```
main/
├── CMakeLists.txt              # 修改: 添加组件
├── Kconfig.projbuild          # 修改: 添加配置项
├── idf_component.yml          # 修改: 添加依赖
├── app_main.c                 # 修改: 初始化顺序
├── app/
│   ├── app.c                  # 修改: 集成新模块
│   └── app.h
├── wifi_prov/                  # 已有
├── param_store/               # 新增
│   ├── param_store.c
│   └── param_store.h
├── cert_manager/              # 新增
│   ├── cert_manager.c
│   └── cert_manager.h
├── web_server/                # 新增
│   ├── web_server.c
│   ├── web_server.h
│   ├── handlers/
│   │   ├── system_handler.c
│   │   ├── params_handler.c
│   │   ├── wifi_handler.c
│   │   ├── ota_handler.c
│   │   └── mqtt_handler.c
│   └── web_files/
│       ├── index.html
│       ├── style.css
│       └── app.js
├── ota_service/               # 新增
│   ├── ota_service.c
│   └── ota_service.h
├── mqtt_client/               # 新增
│   ├── mqtt_client.c
│   └── mqtt_client.h
├── mfg_provision/             # 新增
│   ├── mfg_provision.c
│   └── mfg_provision.h
└── test_mode/                 # 新增
    ├── test_mode.c
    └── test_mode.h
components/
└── led_driver/                # 新增
    ├── led_driver.c
    └── led_driver.h
partitions.csv                 # 修改: 添加 OTA 分区
sdkconfig.defaults             # 修改: 添加默认配置
```

---

## Task 1: 更新分区表和依赖配置

**Files:**
- Modify: `partitions.csv`
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`

**Interfaces:**
- Produces: OTA 双分区布局, 组件依赖

- [ ] **Step 1: 更新分区表**

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,      0x6000,
phy_init, data, phy,     ,      0x1000,
factory,  app,  factory, ,      0x100000,
ota_0,    app,  ota_0,   ,      0x100000,
ota_1,    app,  ota_1,   ,      0x100000,
certs,    data, fat,     ,      0x4000,
mfg_data, data, nvs,     ,      0x4000,
```

- [ ] **Step 2: 更新 idf_component.yml**

```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  espressif/qrcode: ^0.2.0
  espressif/network_provisioning: ^1.2.4
  espressif/led_indicator: ^2.1.2
  espressif/i2c_bus: ^1.5.2
  espressif/esp_delta_ota: ^1.1.4
  espressif/mqtt: ^1.1.0
  espressif/esp_https_server: ^1.1.4
```

- [ ] **Step 3: 更新 sdkconfig.defaults**

```makefile
# ESPX Configuration
CONFIG_ESPX=y
CONFIG_DEVICE_NAME="ESPX"
CONFIG_FIRMWARE_VERSION="1.0.0"

# Partition Table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Wi-Fi
CONFIG_WIFI_DEFAULT_SSID=""
CONFIG_WIFI_DEFAULT_PASSWORD=""

# MQTT
CONFIG_MQTT_BROKER_URL="mqtt://localhost:1883"
CONFIG_MQTT_RECONNECT_INTERVAL=5

# OTA
CONFIG_OTA_SERVER_URL=""
CONFIG_OTA_CHECK_INTERVAL=60

# HTTPS Server
CONFIG_HTTPS_SERVER_PORT=443
CONFIG_ENABLE_HTTP_REDIRECT=y
```

- [ ] **Step 4: 提交**

```bash
git add partitions.csv main/idf_component.yml sdkconfig.defaults
git commit -m "feat: update partition table and dependencies for OTA, MQTT, HTTPS"
```

---

## Task 2: 实现 param_store 模块

**Files:**
- Create: `main/param_store/param_store.h`
- Create: `main/param_store/param_store.c`
- Modify: `main/app/app.c`

**Interfaces:**
- Produces: `param_store_init()`, `param_store_get()`, `param_store_set()`, `param_store_save()`, `param_store_reset()`

- [ ] **Step 1: 创建 param_store.h**

```c
#ifndef PARAM_STORE_H
#define PARAM_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

typedef enum {
    PARAM_TYPE_INT,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_STRING,
    PARAM_TYPE_BOOL,
} param_type_t;

typedef enum {
    PARAM_ACCESS_RO,
    PARAM_ACCESS_RW,
} param_access_t;

typedef struct {
    const char *key;
    param_type_t type;
    param_access_t access;
    void *value;
    const void *default_val;
    const void *min;
    const void *max;
    const char *description;
} param_def_t;

// System RO params
#define PARAM_DEVICE_NAME      {"device_name", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "ESPX", NULL, NULL, "Device Name"}
#define PARAM_DEVICE_ID       {"device_id", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "", NULL, NULL, "Device ID (MAC)"}
#define PARAM_CHIP_MODEL       {"chip_model", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "ESP32-S3", NULL, NULL, "Chip Model"}
#define PARAM_CHIP_REVISION    {"chip_revision", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "", NULL, NULL, "Chip Revision"}
#define PARAM_FIRMWARE_VER    {"firmware_ver", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "", NULL, NULL, "Firmware Version"}
#define PARAM_BUILD_TIME      {"build_time", PARAM_TYPE_STRING, PARAM_ACCESS_RO, NULL, "", NULL, NULL, "Build Time"}

// Runtime RW params
#define PARAM_WIFI_SSID       {"wifi_ssid", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "Wi-Fi SSID"}
#define PARAM_WIFI_PASSWORD    {"wifi_password", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "Wi-Fi Password"}
#define PARAM_MQTT_BROKER     {"mqtt_broker", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "mqtt://localhost:1883", NULL, NULL, "MQTT Broker URL"}
#define PARAM_MQTT_USERNAME   {"mqtt_username", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "MQTT Username"}
#define PARAM_MQTT_PASSWORD   {"mqtt_password", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "MQTT Password"}
#define PARAM_MQTT_CLIENT_ID  {"mqtt_client_id", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "MQTT Client ID"}
#define PARAM_OTA_SERVER_URL  {"ota_server_url", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "OTA Server URL"}

// Business params
#define PARAM_INT_001         {"param_int_001", PARAM_TYPE_INT, PARAM_ACCESS_RW, NULL, &(int){0}, &(int){0}, &(int){1000}, "Business Int Param 1"}
#define PARAM_FLOAT_001       {"param_float_001", PARAM_TYPE_FLOAT, PARAM_ACCESS_RW, NULL, &(float){0.0}, &(float){0.0}, &(float){1000.0}, "Business Float Param 1"}
#define PARAM_STR_001         {"param_str_001", PARAM_TYPE_STRING, PARAM_ACCESS_RW, NULL, "", NULL, NULL, "Business String Param 1"}
#define PARAM_BOOL_001        {"param_bool_001", PARAM_TYPE_BOOL, PARAM_ACCESS_RW, NULL, &(bool){false}, NULL, NULL, "Business Bool Param 1"}

esp_err_t param_store_init(void);
esp_err_t param_store_get(const char *key, void *value, size_t *len);
esp_err_t param_store_set(const char *key, const void *value, size_t len);
esp_err_t param_store_save(const char *key);
esp_err_t param_store_save_all(void);
esp_err_t param_store_reset(const char *key);
esp_err_t param_store_reset_all(void);
esp_err_t param_store_get_all(char *json, size_t max_len);
const char* param_store_get_device_id(void);

#endif // PARAM_STORE_H
```

- [ ] **Step 2: 创建 param_store.c**

实现参数存储核心逻辑:
- NVS 初始化和读写
- 参数定义表管理
- 值类型转换
- 自动保存回调

- [ ] **Step 3: 提交**

```bash
git add main/param_store/
git commit -m "feat: add param_store module for parameter management"
```

---

## Task 3: 实现 cert_manager 模块

**Files:**
- Create: `main/cert_manager/cert_manager.h`
- Create: `main/cert_manager/cert_manager.c`

**Interfaces:**
- Consumes: param_store_get_device_id()
- Produces: `cert_manager_init()`, `cert_manager_get_server_cert()`, `cert_manager_regenerate()`

- [ ] **Step 1: 创建 cert_manager.h**

```c
#ifndef CERT_MANAGER_H
#define CERT_MANAGER_H

#include <esp_err.h>
#include <esp_err.h>

typedef struct {
    char *cert_pem;
    char *key_pem;
    size_t cert_len;
    size_t key_len;
} server_cert_t;

esp_err_t cert_manager_init(void);
esp_err_t cert_manager_get_server_cert(server_cert_t *cert);
esp_err_t cert_manager_regenerate(void);
bool cert_manager_is_valid(void);

#endif // CERT_MANAGER_H
```

- [ ] **Step 2: 创建 cert_manager.c**

实现:
- 检测 NVS 中是否已有证书
- 无则使用 mbedTLS 生成 RSA 2048 自签名证书
- CN = ESPX-{chip_id}
- 保存到 NVS

- [ ] **Step 3: 提交**

```bash
git add main/cert_manager/
git commit -m "feat: add cert_manager for self-signed certificate generation"
```

---

## Task 4: 实现 web_server 模块 (基础)

**Files:**
- Create: `main/web_server/web_server.h`
- Create: `main/web_server/web_server.c`
- Create: `main/web_server/handlers/system_handler.c`
- Create: `main/web_server/handlers/params_handler.c`

**Interfaces:**
- Consumes: cert_manager_get_server_cert(), param_store_*()
- Produces: `web_server_start()`, `web_server_stop()`

- [ ] **Step 1: 创建 web_server.h**

```c
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <esp_err.h>

esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);

#endif // WEB_SERVER_H
```

- [ ] **Step 2: 创建 web_server.c**

实现:
- HTTPS 服务器初始化 (esp_https_server)
- URI 路由注册
- 静态文件服务 (spiffs 或嵌入)
- WebSocket 支持

- [ ] **Step 3: 创建 handlers/system_handler.c**

实现 /api/system/info 端点:
```json
{
  "device_name": "ESPX",
  "device_id": "XX:XX:XX:XX:XX:XX",
  "chip_model": "ESP32-S3",
  "firmware_ver": "1.0.0",
  "uptime": 12345
}
```

- [ ] **Step 4: 创建 handlers/params_handler.c**

实现:
- GET /api/params - 获取所有参数
- GET /api/params/{key} - 获取单个参数
- PUT /api/params/{key} - 更新参数
- POST /api/params/batch - 批量更新

- [ ] **Step 5: 提交**

```bash
git add main/web_server/
git commit -m "feat: add web_server module with HTTPS and REST API"
```

---

## Task 5: 实现 Web 静态页面

**Files:**
- Create: `main/web_server/web_files/index.html`
- Create: `main/web_server/web_files/style.css`
- Create: `main/web_server/web_files/app.js`

**Interfaces:**
- Consumes: web_server REST API
- Produces: 完整的 Web 管理界面

- [ ] **Step 1: 创建 index.html**

单页应用结构:
```html
<!DOCTYPE html>
<html>
<head>
  <title>ESPX Device Manager</title>
  <link rel="stylesheet" href="/static/style.css">
</head>
<body>
  <header>ESPX Device Manager</header>
  <nav>
    <button class="tab-btn active" data-tab="system">System</button>
    <button class="tab-btn" data-tab="params">Parameters</button>
    <button class="tab-btn" data-tab="wifi">Wi-Fi</button>
    <button class="tab-btn" data-tab="ota">OTA</button>
  </nav>
  <main>
    <section id="system-section" class="tab-content active">...</section>
    <section id="params-section" class="tab-content">...</section>
    <section id="wifi-section" class="tab-content">...</section>
    <section id="ota-section" class="tab-content">...</section>
  </main>
  <footer id="status-bar">Status: Connected</footer>
  <script src="/static/app.js"></script>
</body>
</html>
```

- [ ] **Step 2: 创建 style.css**

响应式设计，支持深色模式

- [ ] **Step 3: 创建 app.js**

实现:
- REST API 调用
- WebSocket 连接
- 实时状态更新
- 表单处理

- [ ] **Step 4: 提交**

```bash
git add main/web_server/web_files/
git commit -m "feat: add web UI for device management"
```

---

## Task 6: 实现 mqtt_client 模块

**Files:**
- Create: `main/mqtt_client/mqtt_client.h`
- Create: `main/mqtt_client/mqtt_client.c`

**Interfaces:**
- Consumes: param_store_get(), mqtt_broker, mqtt_username 等
- Produces: `mqtt_client_start()`, `mqtt_client_stop()`, `mqtt_client_publish()`

- [ ] **Step 1: 创建 mqtt_client.h**

```c
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <esp_err.h>

typedef void (*mqtt_on_message_t)(const char *topic, const char *data, size_t len);

esp_err_t mqtt_client_start(void);
esp_err_t mqtt_client_stop(void);
esp_err_t mqtt_client_publish(const char *topic, const char *data, int qos, bool retain);
esp_err_t mqtt_client_subscribe(const char *topic, int qos);
void mqtt_client_set_message_handler(mqtt_on_message_t handler);
bool mqtt_client_is_connected(void);

#endif // MQTT_CLIENT_H
```

- [ ] **Step 2: 创建 mqtt_client.c**

实现:
- ESP-MQTT 配置和初始化
- 自动重连机制 (指数退避)
- 心跳保活
- 主题订阅管理
- 消息回调分发

- [ ] **Step 3: 提交**

```bash
git add main/mqtt_client/
git commit -m "feat: add mqtt_client module for cloud communication"
```

---

## Task 7: 实现 ota_service 模块

**Files:**
- Create: `main/ota_service/ota_service.h`
- Create: `main/ota_service/ota_service.c`
- Create: `main/web_server/handlers/ota_handler.c`

**Interfaces:**
- Consumes: esp_delta_ota, param_store_get("ota_server_url")
- Produces: `ota_service_check()`, `ota_service_start()`, `ota_service_get_status()`

- [ ] **Step 1: 创建 ota_service.h**

```c
#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <esp_err.h>
#include <stdint.h>

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    float progress;
    char version[32];
    char error_msg[128];
} ota_status_t;

esp_err_t ota_service_init(void);
esp_err_t ota_service_check(void);
esp_err_t ota_service_start(const char *url);
esp_err_t ota_service_cancel(void);
esp_err_t ota_service_get_status(ota_status_t *status);

#endif // OTA_SERVICE_H
```

- [ ] **Step 2: 创建 ota_service.c**

实现:
- 差分包 manifest 获取和解析
- HTTP 下载管理器
- esp_delta_ota 集成
- 升级状态机
- 启动分区切换
- 回滚机制

- [ ] **Step 3: 创建 handlers/ota_handler.c**

实现:
- GET /api/ota/status
- POST /api/ota/start
- POST /api/ota/cancel

- [ ] **Step 4: 提交**

```bash
git add main/ota_service/ main/web_server/handlers/ota_handler.c
git commit -m "feat: add ota_service with delta OTA support"
```

---

## Task 8: 实现 mfg_provision 模块

**Files:**
- Create: `main/mfg_provision/mfg_provision.h`
- Create: `main/mfg_provision/mfg_provision.c`

**Interfaces:**
- Produces: `mfg_provision_load()`, `mfg_provision_has_data()`

- [ ] **Step 1: 创建 mfg_provision.h**

```c
#ifndef MFG_PROVISION_H
#define MFG_PROVISION_H

#include <stdbool.h>
#include <esp_err.h>

bool mfg_provision_has_data(void);
esp_err_t mfg_provision_load(void);
esp_err_t mfg_provision_clear(void);

#endif // MFG_PROVISION_H
```

- [ ] **Step 2: 创建 mfg_provision.c**

实现:
- 检测 mfg_data 分区
- 读取预置参数
- 写入 param_store
- 清除 mfg_data

- [ ] **Step 3: 提交**

```bash
git add main/mfg_provision/
git commit -m "feat: add mfg_provision for factory data loading"
```

---

## Task 9: 实现 test_mode 模块

**Files:**
- Create: `main/test_mode/test_mode.h`
- Create: `main/test_mode/test_mode.c`

**Interfaces:**
- Produces: `test_mode_enter()`

- [ ] **Step 1: 创建 test_mode.h**

```c
#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <stdbool.h>
#include <esp_err.h>

typedef enum {
    TEST_ITEM_LED,
    TEST_ITEM_BUTTON,
    TEST_ITEM_WIFI,
    TEST_ITEM_MQTT,
    TEST_ITEM_UART,
} test_item_t;

typedef enum {
    TEST_RESULT_NONE,
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
} test_result_t;

esp_err_t test_mode_check_trigger(void);
void test_mode_enter(void);
test_result_t test_mode_get_result(test_item_t item);

#endif // TEST_MODE_H
```

- [ ] **Step 2: 创建 test_mode.c**

实现:
- GPIO 检测 (GPIO0 低电平触发)
- 测试菜单界面 (UART)
- LED 测试命令
- Wi-Fi 连接测试
- MQTT 连接测试
- 结果记录

- [ ] **Step 3: 提交**

```bash
git add main/test_mode/
git commit -m "feat: add test_mode for manufacturing test"
```

---

## Task 10: 实现 led_driver 组件

**Files:**
- Create: `components/led_driver/CMakeLists.txt`
- Create: `components/led_driver/Kconfig`
- Create: `components/led_driver/led_driver.h`
- Create: `components/led_driver/led_driver.c`

**Interfaces:**
- Produces: `led_driver_init()`, `led_set_color()`, `led_blink()`, `led_set_pattern()`

- [ ] **Step 1: 创建 led_driver 组件文件**

使用 led_indicator 库封装:
- 单色 LED 控制
- 闪烁模式
- 状态指示 (Wi-Fi 连接/断开, MQTT 连接状态)

- [ ] **Step 2: 提交**

```bash
git add components/led_driver/
git commit -m "feat: add led_driver component for status indication"
```

---

## Task 11: 创建 wifi_handler 和 mqtt_handler

**Files:**
- Create: `main/web_server/handlers/wifi_handler.c`
- Create: `main/web_server/handlers/mqtt_handler.c`

**Interfaces:**
- Consumes: wifi_prov 模块, mqtt_client 模块
- Produces: Wi-Fi 和 MQTT 的 REST API

- [ ] **Step 1: 创建 wifi_handler.c**

实现:
- GET /api/wifi/status
- POST /api/wifi/connect
- POST /api/wifi/scan

- [ ] **Step 2: 创建 mqtt_handler.c**

实现:
- GET /api/mqtt/status
- POST /api/mqtt/reconnect

- [ ] **Step 3: 提交**

```bash
git add main/web_server/handlers/wifi_handler.c main/web_server/handlers/mqtt_handler.c
git commit -m "feat: add wifi and mqtt API handlers"
```

---

## Task 12: 集成所有模块到 app_main

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/app/app.c`

**Interfaces:**
- Consumes: 所有模块初始化函数
- Produces: 完整的启动流程

- [ ] **Step 1: 更新 app_main.c**

```c
void app_main(void)
{
    // 1. 检测测试模式触发
    if (test_mode_check_trigger() == ESP_OK) {
        test_mode_enter();
        // 不返回
    }

    // 2. 初始化 NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // 3. 初始化参数存储
    ESP_ERROR_CHECK(param_store_init());

    // 4. 加载工厂预配置
    if (mfg_provision_has_data()) {
        ESP_ERROR_CHECK(mfg_provision_load());
    }

    // 5. 初始化证书管理
    ESP_ERROR_CHECK(cert_manager_init());

    // 6. 初始化 LED 驱动
    ESP_ERROR_CHECK(led_driver_init());

    // 7. 初始化 Wi-Fi 配网
    ESP_ERROR_CHECK(wifi_prov_init());
    ESP_ERROR_CHECK(wifi_prov_start(NULL));
    wifi_prov_wait_for_connection();

    // 8. 初始化 MQTT 客户端
    ESP_ERROR_CHECK(mqtt_client_start());

    // 9. 启动 HTTPS Web 服务器
    ESP_ERROR_CHECK(web_server_start());

    // 10. 初始化 OTA 服务
    ESP_ERROR_CHECK(ota_service_init());

    // 11. 主循环
    app_loop();
}
```

- [ ] **Step 2: 提交**

```bash
git add main/app_main.c main/app/app.c
git commit -m "feat: integrate all modules in app_main"
```

---

## Task 13: 最终集成测试

**Files:**
- Modify: `main/Kconfig.projbuild`

**Interfaces:**
- 验证所有模块集成

- [ ] **Step 1: 添加完整 Kconfig 配置项**

- [ ] **Step 2: 运行完整构建测试**

```bash
idf.py set-target esp32s3
idf.py build
```

- [ ] **Step 3: 烧录测试**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

- [ ] **Step 4: 提交**

```bash
git add main/Kconfig.projbuild
git commit -m "chore: add Kconfig and final integration"
```

---

## 任务清单

| # | 任务 | 依赖 |
|---|------|------|
| 1 | 更新分区表和依赖配置 | - |
| 2 | 实现 param_store 模块 | 1 |
| 3 | 实现 cert_manager 模块 | 2 |
| 4 | 实现 web_server 模块 (基础) | 3 |
| 5 | 实现 Web 静态页面 | 4 |
| 6 | 实现 mqtt_client 模块 | 2 |
| 7 | 实现 ota_service 模块 | 1, 2 |
| 8 | 实现 mfg_provision 模块 | 2 |
| 9 | 实现 test_mode 模块 | 2, 8 |
| 10 | 实现 led_driver 组件 | - |
| 11 | 创建 wifi_handler 和 mqtt_handler | 4, 6 |
| 12 | 集成所有模块到 app_main | 2-11 |
| 13 | 最终集成测试 | 12 |
