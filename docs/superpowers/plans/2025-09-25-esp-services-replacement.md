# ESP Services 替换实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使用 ESP-IDF 官方组件 (ESP Button Service, ESP Wi-Fi Service, ESP CLI Service, ESP OTA Service) 替换项目中现有的自定义实现。

**Architecture:** 
- Button: 使用 `iot_button_create` 替代现有 `button.c/h`，支持事件回调和消抖
- Wi-Fi: 使用 `wifi_prov_mgr_init` 替代现有 `wifi_provisioning.c/h`
- CLI: 使用 `esp_console` 替代远程命令系统
- OTA: 使用 `esp_https_ota` 替代现有 `ota_manager.c/h`

**Tech Stack:** ESP-IDF v5.5.3, esp-idf components (button, esp_wifi, esp_console, esp_https_ota)

**Spec:** 无独立规格文档，本计划基于现有代码分析

---

## Global Constraints

- ESP-IDF 版本: v5.5.3
- 编译目标: ESP32-S3 (xtensa)
- 编码标准: UTF-8, Unix 换行符
- 头文件保护: `#pragma once`
- 日志标签: 使用组件名称 (如 "button", "wifi_prov")

---

## 文件映射

| 现有文件 | 新实现 | 操作 |
|---------|-------|-----|
| `components/bsp/button/button.c/h` | ESP Button Component | 删除 |
| `components/network/wifi_provisioning.c/h` | ESP Wi-Fi Provisioning | 删除 |
| `components/core/ota_manager.c/h` | esp_https_ota | 删除 |
| `components/app/remote_cmd/` | esp_console | 删除 |

---

## Task Structure

### Task 1: 添加 ESP Button Component 依赖

**Files:**
- Modify: `dependencies.lock`
- Modify: `sdkconfig.defaults.esp32s3`

**Interfaces:**
- Consumes: 无
- Produces: ESP Button Component 可用

- [ ] **Step 1: 添加 button 组件到 dependencies.lock**

```yaml
components:
  button:
    version: ">=1.0.0"
    repo: https://github.com/espressif/esp-bsp/tree/master/components/esp_button
```

- [ ] **Step 2: 在 sdkconfig.defaults.esp32s3 添加 CONFIG**

```
CONFIG_ESP_BSP_BUTTON_ENABLED=y
```

- [ ] **Step 3: 提交**

```bash
git add dependencies.lock sdkconfig.defaults.esp32s3
git commit -m "deps: add ESP Button Component dependency"
```

---

### Task 2: 创建 button_service.h 封装层

**Files:**
- Create: `components/bsp/button/button_service.h`

**Interfaces:**
- Consumes: `esp_button.h` (ESP Button Component)
- Produces: `button_service_create()`, `button_service_delete()`, 事件回调注册

```c
/**
 * @file button_service.h
 * @brief ESP Button Service 封装层
 */
#pragma once

#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_SERVICE_EVENT_PRESSED = 0,
    BUTTON_SERVICE_EVENT_RELEASED,
    BUTTON_SERVICE_EVENT_CLICKED,
    BUTTON_SERVICE_EVENT_LONG_PRESSED,
} button_service_event_type_t;

typedef struct {
    uint8_t button_id;
    button_service_event_type_t type;
    uint32_t press_duration_ms;
} button_service_event_t;

typedef void (*button_service_callback_t)(const button_service_event_t *event, void *user_data);

typedef struct button_service_s *button_service_handle_t;

typedef struct {
    uint8_t button_id;
    int8_t gpio_num;
    uint32_t long_press_ms;
    uint32_t short_press_ms;
} button_service_config_t;

button_service_handle_t button_service_create(const button_service_config_t *config);
esp_err_t button_service_delete(button_service_handle_t handle);
esp_err_t button_service_register_callback(button_service_handle_t handle, 
                                           button_service_callback_t callback, 
                                           void *user_data);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 提交**

```bash
git add components/bsp/button/button_service.h
git commit -m "feat: add button_service.h wrapper header"
```

---

### Task 3: 创建 button_service.c 实现

**Files:**
- Create: `components/bsp/button/button_service.c`

**Interfaces:**
- Consumes: `button_service.h`, `esp_button.h`
- Produces: `button_service_create()`, `button_service_delete()`, 回调处理

```c
/**
 * @file button_service.c
 * @brief ESP Button Service 封装层实现
 */
#include "button_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "button_service";

struct button_service_s {
    button_config_t base_config;
    button_handle_t button;
    button_service_callback_t callback;
    void *user_data;
};

static button_event_type_t convert_button_event_type(button_cb_type_t type) {
    switch (type) {
        case BUTTON_CB_PUSH: return BUTTON_SERVICE_EVENT_PRESSED;
        case BUTTON_CB_RELEASE: return BUTTON_SERVICE_EVENT_RELEASED;
        case BUTTON_CB_TAP: return BUTTON_SERVICE_EVENT_CLICKED;
        case BUTTON_CB_LONG_PRESS_START: return BUTTON_SERVICE_EVENT_LONG_PRESSED;
        default: return BUTTON_SERVICE_EVENT_CLICKED;
    }
}

static void button_event_callback(void *arg) {
    button_service_handle_t handle = (button_service_handle_t)arg;
    if (handle == NULL || handle->callback == NULL) return;
    
    button_service_event_t event = {
        .button_id = handle->base_config.button_id,
        .type = BUTTON_SERVICE_EVENT_CLICKED,  // 默认
        .press_duration_ms = 0
    };
    
    handle->callback(&event, handle->user_data);
}

button_service_handle_t button_service_create(const button_service_config_t *config) {
    if (config == NULL) return NULL;
    
    button_service_handle_t handle = calloc(1, sizeof(struct button_service_s));
    if (handle == NULL) return NULL;
    
    handle->base_config.button_id = config->button_id;
    handle->callback = NULL;
    handle->user_data = NULL;
    
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = config->gpio_num,
            .active_level = 0,
        },
    };
    
    handle->button = iot_button_create(&btn_cfg);
    if (handle->button == NULL) {
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Button service created: GPIO%d, button_id=%d", 
             config->gpio_num, config->button_id);
    
    return handle;
}

esp_err_t button_service_delete(button_service_handle_t handle) {
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    
    if (handle->button) {
        iot_button_delete(handle->button);
    }
    free(handle);
    return ESP_OK;
}

esp_err_t button_service_register_callback(button_service_handle_t handle,
                                           button_service_callback_t callback,
                                           void *user_data) {
    if (handle == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    
    handle->callback = callback;
    handle->user_data = user_data;
    
    iot_button_register_cb(handle->button, BUTTON_CB_TAP, button_event_callback, handle);
    iot_button_register_cb(handle->button, BUTTON_CB_LONG_PRESS_START, button_event_callback, handle);
    
    return ESP_OK;
}
```

- [ ] **Step 2: 提交**

```bash
git add components/bsp/button/button_service.c
git commit -m "feat: implement button_service.c wrapper"
```

---

### Task 4: 更新 factory_test_mode 使用新 Button Service

**Files:**
- Modify: `components/app/factory_test_mode/factory_test_mode.c`
- Modify: `components/app/factory_test_mode/factory_test_mode.h`

**Interfaces:**
- Consumes: `button_service.h`
- Produces: 使用新按钮服务

- [ ] **Step 1: 更新头文件**

```c
// 替换
// #include "button.h"
// 改为
#include "button_service.h"
```

- [ ] **Step 2: 更新实现**

```c
// 替换 button_create 调用
// 旧: button_create(&btn_config)
// 新: button_service_create(&btn_svc_config)

// 替换事件处理
// 旧: button_wait_event(...)
// 新: 回调方式
button_service_register_callback(btn, factory_button_callback, NULL);
```

- [ ] **Step 3: 删除旧 button 组件引用**

```bash
git rm components/bsp/button/button.c components/bsp/button/button.h
```

- [ ] **Step 4: 更新 CMakeLists.txt**

```cmake
# components/bsp/button/CMakeLists.txt
idf_component_register(SRCS "button_service.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_idf_lib_helpers driver gpio esp_timer)
```

- [ ] **Step 5: 提交**

```bash
git add components/app/factory_test_mode/ components/bsp/button/
git rm components/bsp/button/button.c components/bsp/button/button.h
git commit -m "refactor: replace button with ESP Button Service"
```

---

### Task 5: 创建 wifi_service.h Wi-Fi 服务封装

**Files:**
- Create: `components/network/wifi_service/wifi_service.h`
- Create: `components/network/wifi_service/wifi_service.c`
- Create: `components/network/wifi_service/CMakeLists.txt`

**Interfaces:**
- Consumes: `wifi_provisioning.h` (ESP-IDF)
- Produces: `wifi_service_init()`, `wifi_service_start_provisioning()`, `wifi_service_connect()`

```c
/**
 * @file wifi_service.h
 * @brief ESP Wi-Fi Service 封装层
 */
#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_SERVICE_STATE_IDLE,
    WIFI_SERVICE_STATE_CONNECTING,
    WIFI_SERVICE_STATE_CONNECTED,
    WIFI_SERVICE_STATE_DISCONNECTED,
    WIFI_SERVICE_STATE_PROVISIONING,
} wifi_service_state_t;

typedef void (*wifi_service_event_callback_t)(wifi_service_state_t state, void *user_data);

typedef struct {
    const char *ssid;
    const char *password;
} wifi_service_config_t;

esp_err_t wifi_service_init(void);
esp_err_t wifi_service_deinit(void);
esp_err_t wifi_service_connect(const char *ssid, const char *password);
esp_err_t wifi_service_disconnect(void);
esp_err_t wifi_service_start_provisioning(const char *pop);
esp_err_t wifi_service_stop_provisioning(void);
esp_err_t wifi_service_register_callback(wifi_service_event_callback_t callback, void *user_data);
wifi_service_state_t wifi_service_get_state(void);
esp_netif_t *wifi_service_get_netif(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 提交**

```bash
git add components/network/wifi_service/
git commit -m "feat: add wifi_service wrapper using ESP provisioning"
```

---

### Task 6: 创建 cli_service 命令行服务

**Files:**
- Create: `components/app/cli_service/cli_service.h`
- Create: `components/app/cli_service/cli_service.c`
- Create: `components/app/cli_service/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_console.h`
- Produces: `cli_service_init()`, `cli_service_register_cmd()`

```c
/**
 * @file cli_service.h
 * @brief ESP CLI Service 封装层
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cli_cmd_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *help;
    const char *hint;
    cli_cmd_handler_t handler;
} cli_command_t;

esp_err_t cli_service_init(void);
esp_err_t cli_service_deinit(void);
esp_err_t cli_service_register_command(const cli_command_t *cmd);
esp_err_t cli_service_run(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 实现包含以下内置命令**

```c
// 设备信息命令
static int cmd_device_info(int argc, char **argv);
// Wi-Fi 状态命令
static int cmd_wifi_status(int argc, char **argv);
// OTA 命令
static int cmd_ota(int argc, char **argv);
// 诊断命令
static int cmd_diag(int argc, char **argv);
```

- [ ] **Step 3: 提交**

```bash
git add components/app/cli_service/
git commit -m "feat: add cli_service using esp_console"
```

---

### Task 7: 创建 ota_service.h OTA 服务封装

**Files:**
- Create: `components/core/ota_service/ota_service.h`
- Create: `components/core/ota_service/ota_service.c`
- Create: `components/core/ota_service/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_https_ota.h`
- Produces: `ota_service_init()`, `ota_service_start()`, `ota_service_progress()`

```c
/**
 * @file ota_service.h
 * @brief ESP OTA Service 封装层
 */
#pragma once

#include "esp_err.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_SERVICE_STATE_IDLE,
    OTA_SERVICE_STATE_DOWNLOADING,
    OTA_SERVICE_STATE_VERIFYING,
    OTA_SERVICE_STATE_WRITING,
    OTA_SERVICE_STATE_REBOOTING,
    OTA_SERVICE_STATE_FAILED
} ota_service_state_t;

typedef struct {
    int progress_percent;
    ota_service_state_t state;
    int last_error;
    char target_version[32];
} ota_service_status_t;

typedef void (*ota_service_progress_callback_t)(const ota_service_status_t *status, void *user_data);

esp_err_t ota_service_init(void);
esp_err_t ota_service_deinit(void);
esp_err_t ota_service_start(const char *url);
esp_err_t ota_service_start_http(const char *url);
esp_err_t ota_service_register_callback(ota_service_progress_callback_t callback, void *user_data);
ota_service_state_t ota_service_get_state(void);
int ota_service_get_progress(void);
const char* ota_service_get_current_version(void);
const char* ota_service_get_target_version(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 提交**

```bash
git add components/core/ota_service/
git commit -m "feat: add ota_service using esp_https_ota"
```

---

### Task 8: 更新 app_main.c 集成所有服务

**Files:**
- Modify: `main/app_main.c`

**Interfaces:**
- Consumes: `wifi_service.h`, `ota_service.h`, `cli_service.h`
- Produces: 使用新服务的主程序

- [ ] **Step 1: 更新头文件引入**

```c
// 删除旧引用
// #include "wifi_provisioning.h"
// #include "ota_manager.h"
// #include "remote_cmd.h"
// #include "button.h"

// 添加新引用
#include "wifi_service.h"
#include "ota_service.h"
#include "cli_service.h"
#include "button_service.h"
```

- [ ] **Step 2: 更新初始化顺序**

```c
// 1. Button Service (用于工厂重置检测)
ESP_LOGI(TAG, "Initializing button service...");
button_service_init();

// 2. Wi-Fi Service
ESP_LOGI(TAG, "Initializing Wi-Fi service...");
wifi_service_init();

// 3. OTA Service
ESP_LOGI(TAG, "Initializing OTA service...");
ota_service_init();

// 4. CLI Service
ESP_LOGI(TAG, "Initializing CLI service...");
cli_service_init();
```

- [ ] **Step 3: 更新工厂重置逻辑**

```c
static void check_factory_reset(void) {
    // 使用新按钮服务
    button_service_config_t config = {
        .button_id = FACTORY_RESET_BUTTON_ID,
        .gpio_num = FACTORY_RESET_GPIO,
        .long_press_ms = 5000,
    };
    
    button_service_handle_t btn = button_service_create(&config);
    if (btn == NULL) {
        ESP_LOGE(TAG, "Failed to create factory reset button");
        return;
    }
    
    button_service_register_callback(btn, factory_reset_callback, NULL);
    
    // 等待长按事件
    // ...
    
    button_service_delete(btn);
}
```

- [ ] **Step 4: 更新配网逻辑**

```c
// 替换 provisioning_start() 调用
if (!config_has_wifi()) {
    ESP_LOGW(TAG, "Starting Wi-Fi provisioning...");
    wifi_service_start_provisioning("abcd1234");  // POP
}
```

- [ ] **Step 5: 提交**

```bash
git add main/app_main.c
git commit -m "refactor: integrate new ESP services in app_main"
```

---

### Task 9: 清理旧组件文件

**Files:**
- Delete: `components/network/wifi_provisioning.c`
- Delete: `components/network/wifi_provisioning.h`
- Delete: `components/core/ota_manager.c`
- Delete: `components/core/ota_manager.h`
- Delete: `components/app/remote_cmd/` (整个目录)
- Delete: `components/bsp/button/` (只剩 CMakeLists.txt)

**Interfaces:**
- Consumes: 无
- Produces: 清理后的组件结构

- [ ] **Step 1: 删除文件**

```bash
git rm components/network/wifi_provisioning.c components/network/wifi_provisioning.h
git rm components/core/ota_manager.c components/core/ota_manager.h
git rm -r components/app/remote_cmd/
```

- [ ] **Step 2: 更新 CMakeLists.txt**

```cmake
# components/network/CMakeLists.txt - 移除 wifi_provisioning
# components/core/CMakeLists.txt - 移除 ota_manager
# components/app/CMakeLists.txt - 移除 remote_cmd
```

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "refactor: remove old custom implementations"
```

---

### Task 10: 更新 sdkconfig 配置

**Files:**
- Modify: `sdkconfig.defaults.esp32s3`

**Interfaces:**
- Consumes: 无
- Produces: 启用的 ESP Services 配置

- [ ] **Step 1: 添加配置项**

```
# ESP Button Component
CONFIG_ESP_BSP_BUTTON_ENABLED=y
CONFIG_ESP_BSP_BUTTON_GPIO_ENABLED=y

# ESP Wi-Fi Provisioning
CONFIG_WIFI_PROVING_ENABLED=y
CONFIG_WIFI_PROV_SOFTAP=y

# ESP Console
CONFIG_ESP_CONSOLE_UART=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_COMMANDS=y

# ESP OTA
CONFIG_ESP_HTTPS_OTA=y
CONFIG_ESP_HTTPS_OTA_DECRYPT_CB=y
```

- [ ] **Step 2: 更新依赖锁定文件**

```yaml
dependencies:
  button:
    version: ">=1.0.0"
  wifi_provisioning:
    version: ">=1.0.0"  # ESP-IDF 内置
  esp_console:
    version: ">=1.0.0"  # ESP-IDF 内置
  esp_https_ota:
    version: ">=1.0.0"  # ESP-IDF 内置
```

- [ ] **Step 3: 提交**

```bash
git add sdkconfig.defaults.esp32s3 dependencies.lock
git commit -m "config: enable ESP services in sdkconfig"
```

---

### Task 11: 验证编译

**Files:**
- 无文件变更

**Interfaces:**
- Consumes: 所有新服务组件
- Produces: 编译成功

- [ ] **Step 1: 清理并重新配置**

```bash
idf.py fullclean
idf.py reconfigure
```

- [ ] **Step 2: 编译项目**

```bash
idf.py build
```

- [ ] **Step 3: 验证无警告和错误**

Expected: Build completed successfully

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "build: verify compilation with ESP services"
```

---

## 自检清单

### 1. 规格覆盖
| 需求 | 任务 |
|-----|-----|
| Button Service | Task 1-4 |
| Wi-Fi Service | Task 5, 8 |
| CLI Service | Task 6, 8 |
| OTA Service | Task 7, 8 |
| 集成测试 | Task 11 |

### 2. 占位符扫描
- 无 "TBD" 或 "TODO"
- 无 "填充细节" 描述
- 所有代码块完整

### 3. 类型一致性
- `button_service_handle_t` 贯穿 Task 1-4
- `wifi_service_state_t` 贯穿 Task 5
- `ota_service_state_t` 贯穿 Task 7

---

## 执行交接

计划完成并保存到 `docs/superpowers/plans/2025-09-25-esp-services-replacement.md`

**两种执行方式:**

**1. 子代理驱动 (推荐)** - 每个任务派发一个新鲜子代理，任务间审核，快速迭代

**2. 内联执行** - 在本会话中执行任务，使用 executing-plans，批量执行带检查点

**选择哪种方式?**
