# ESPX 工程重构设计文档

> 日期：2026-09-28
> 状态：草稿
> 版本：1.0

---

## 1. 概述

### 1.1 背景

ESPX 是一个软件定义的 IoT 节点固件，当前代码存在以下问题：

| 问题 | 影响 |
|------|------|
| AT 指令服务不再需要 | 增加固件体积和维护负担 |
| `web_server.c` 921 行，单一文件过大 | 可读性差，难以维护 |
| 工具函数重复定义 | 代码冗余，容易出错 |
| 模块边界模糊 | `core/` 承载过多职责 |
| 缺少统一的工具模块 | 公共代码散落各处 |
| 内存使用缺乏基准 | 优化方向不明确 |

### 1.2 目标

1. **移除 AT 指令支持** — 删除 `at_service/` 模块
2. **模块边界重构** — 拆分为 `config/`、`device/`、`common/`、`utils/` 四个新模块
3. **消除重复代码** — 提取公共工具函数到 `utils/`
4. **改善代码组织** — 拆分 `web_server.c` 为多个 handler 文件
5. **内存优化** — 先基准测试，再针对性优化

### 1.3 约束

- 符合 AGENT.md 的分层约束（依赖只能向下）
- `yaml.c` 必须可主机单元测试
- 编译必须 0 error / 0 warning
- 所有改动必须更新相关文档

---

## 2. 目标架构

### 2.1 目录结构

```
main/
├── CMakeLists.txt              # 更新：移除 at_service，添加新模块源码与 include 路径
├── Kconfig.projbuild           # 更新：移除 AT 配置项
├── app_main.c                  # 更新：移除 at_service 引用
│
├── utils/                     # 新增：公共工具模块
│   ├── utils.h                 # 汇总头文件（include 下面三个）
│   ├── json_utils.c/h          # JSON 操作辅助
│   ├── str_utils.c/h           # 字符串操作辅助
│   └── nvs_utils.c/h           # NVS 操作辅助
│
├── config/                    # 新增：从 core/ 拆分
│   ├── node_config.c/h         # 节点身份与配置
│   ├── config_apply.c/h        # 配置应用逻辑
│   └── yaml.c/h                # YAML 解析器
│
├── device/                    # 新增：从 core/ 拆分
│   ├── device_type.c/h         # 设备类型注册表
│   ├── device_manager.c/h      # 设备实例管理
│   └── event_bus.c/h          # 事件总线
│
├── common/                    # 新增：从 core/ 拆分
│   ├── task_util.h             # 任务创建辅助（已是头文件）
│   ├── defaults.c/h            # 默认配置
│   ├── sys_stats.c/h           # 系统统计
│   └── sys_info.c/h            # 系统信息
│
├── peripherals/               # 保持不变
├── mqtt_client/               # 保持不变（更新 import 路径）
├── web_server/                # 拆分（见 2.2）
├── wifi_prov/                 # 保持不变（更新 import 路径）
├── cert_manager/              # 保持不变（更新 import 路径）
├── ota_service/               # 保持不变（更新 import 路径）
├── net_services/              # 保持不变（更新 import 路径）
├── test_mode/                 # 保持不变（更新 import 路径）
└── mfg_provision/             # 保持不变（更新 import 路径）

components/                    # 保持不变
tools/                         # 删除 at_test.py；修改 gen_cert.py；新增 sysinfo_baseline.py
tests/                         # 新增 utils_test.c；更新 run_yaml_tests.sh 路径
```

**注：** ESP-IDF 的 `main/` 是**单一组件**，所有源码都列在
`main/CMakeLists.txt` 的 `SRCS` 中，因此新增的子目录**不需要**各自的
`CMakeLists.txt`，只需在 `main/CMakeLists.txt` 添加源码和 include 路径。

### 2.2 Web Server 拆分

`web_server/web_server.c` (921 行) 拆分为：

```
web_server/
├── web_server.h               # 主头文件
├── web_server.c               # 启动/停止逻辑 + URI 注册
├── ws_server.c/h              # 保持不变
├── handlers/
│   ├── handlers.h              # 统一 handler 注册
│   ├── node_handler.c/h        # /api/node
│   ├── device_handler.c/h      # /api/peripherals/*
│   ├── config_handler.c/h       # /api/config
│   ├── network_handler.c/h      # /api/network
│   ├── system_handler.c/h       # /api/system/*
│   ├── ota_handler.c/h          # /api/ota/*
│   ├── cert_handler.c/h         # /api/certs/*
│   └── wifi_handler.c/h         # /api/wifi/*
└── web_files/
    └── index.html              # 保持不变（EMBED_FILES 路径不变）
```

`handlers/` 同样无需独立的 `CMakeLists.txt`，在 `main/CMakeLists.txt` 中列出即可。

---

## 3. 详细设计

### 3.1 Utils 模块

#### 3.1.1 json_utils.h

```c
/**
 * @brief JSON 操作辅助函数
 *
 * 封装常见的 cJSON 操作，减少重复代码。
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 安全获取字符串值
 * @param obj  cJSON 对象
 * @param key  键名
 * @param def  默认值（key 不存在时返回）
 * @return 字符串指针（永不返回 NULL）
 */
const char* json_get_string(const cJSON *obj, const char *key, const char *def);

/**
 * @brief 安全获取整数值
 * @param obj  cJSON 对象
 * @param key  键名
 * @param def  默认值
 * @return 整数值
 */
int json_get_int(const cJSON *obj, const char *key, int def);

/**
 * @brief 安全获取布尔值
 * @param obj  cJSON 对象
 * @param key  键名
 * @param def  默认值
 * @return 布尔值
 */
bool json_get_bool(const cJSON *obj, const char *key, bool def);

/**
 * @brief 安全设置字符串值（先删除后添加）
 * @param obj  cJSON 对象
 * @param key  键名
 * @param value 值（可为 NULL）
 */
void json_set_string(cJSON *obj, const char *key, const char *value);

/**
 * @brief 安全设置数值
 */
void json_set_number(cJSON *obj, const char *key, double value);

/**
 * @brief 安全设置布尔值
 */
void json_set_bool(cJSON *obj, const char *key, bool value);

/**
 * @brief 检查对象是否包含非空字符串
 */
bool json_has_string(const cJSON *obj, const char *key);

/**
 * @brief 深度复制 cJSON 对象
 * @return 新对象（需调用 cJSON_Delete 释放）
 */
cJSON* json_dup(const cJSON *obj);

#ifdef __cplusplus
}
#endif

#endif /* JSON_UTILS_H */
```

#### 3.1.2 str_utils.h

```c
/**
 * @brief 字符串操作辅助函数
 */

#ifndef STR_UTILS_H
#define STR_UTILS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 安全复制字符串
 * @param dest  目标缓冲区
 * @param dest_size  目标大小
 * @param src   源字符串
 * @return 成功复制的字符数（不含终止符）
 */
size_t str_copy(char *dest, size_t dest_size, const char *src);

/**
 * @brief 大小写不敏感字符串比较
 */
bool str_eq_ignore_case(const char *a, const char *b);

/**
 * @brief 检查字符串是否包含子串
 */
bool str_contains(const char *haystack, const char *needle);

/**
 * @brief 检查字符串是否为空白
 */
bool str_is_blank(const char *s);

/**
 * @brief 去除首尾空白
 * @param s 输入字符串（会被修改）
 * @return 去除空白后的字符串
 */
char* str_trim(char *s);

/**
 * @brief MAC 地址转字符串
 * @param mac  6 字节 MAC 地址
 * @param dest 目标缓冲区（至少 18 字节）
 */
void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size);

/**
 * @brief 格式化时间戳为 ISO 8601 字符串
 */
void timestamp_to_iso8601(char *dest, size_t dest_size, int64_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* STR_UTILS_H */
```

#### 3.1.3 nvs_utils.h

```c
/**
 * @brief NVS 操作辅助函数
 */

#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <nvs.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开 NVS 命名空间
 * @param ns     命名空间名
 * @param mode   读写模式
 * @param out    输出句柄（需调用 nvs_close）
 * @return ESP_OK on success
 */
esp_err_t nvs_open_ns(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);

/**
 * @brief 读取字符串值
 * @param nvs    NVS 句柄
 * @param key    键名
 * @param def    默认值（键不存在时返回）
 * @param buf    输出缓冲区
 * @param buf_size 缓冲区大小
 * @return ESP_OK on success
 */
esp_err_t nvs_get_str_safe(nvs_handle_t nvs, const char *key,
                            const char *def, char *buf, size_t buf_size);

/**
 * @brief 写入 JSON 对象（序列化为字符串）
 */
esp_err_t nvs_set_json(nvs_handle_t nvs, const char *key, const cJSON *obj);

/**
 * @brief 读取 JSON 对象（反序列化）
 * @return 需调用 cJSON_Delete 释放
 */
cJSON* nvs_get_json(nvs_handle_t nvs, const char *key);

/**
 * @brief 原子性更新 JSON 对象（读取-修改-写入）
 * @param ns     命名空间
 * @param key    键名
 * @param update 修改函数（返回修改后的对象）
 * @return ESP_OK on success
 */
typedef cJSON* (*nvs_json_update_fn)(cJSON *existing);
esp_err_t nvs_update_json(const char *ns, const char *key, nvs_json_update_fn update);

#ifdef __cplusplus
}
#endif

#endif /* NVS_UTILS_H */
```

### 3.2 Config 模块

```
config/
├── CMakeLists.txt
├── node_config.c/h     # 从 core/ 移动
├── config_apply.c/h    # 从 core/ 移动
└── yaml.c/h            # 从 core/ 移动
```

**依赖关系：**
```
config_apply.c → node_config.c → event_bus.c → utils/json_utils.c
yaml.c → cJSON (无 ESP-IDF 依赖，可主机测试)
```

**关键改进：**
- 所有 cJSON 操作使用 `utils/json_utils.c` 替代直接调用
- `config_apply.c` 中的 `is_secret()` 移动到 `str_utils.c`

### 3.3 Device 模块

```
device/
├── CMakeLists.txt
├── device_type.c/h     # 从 core/ 移动
├── device_manager.c/h  # 从 core/ 移动
└── event_bus.c/h       # 从 core/ 移动
```

**依赖关系：**
```
device_manager.c → device_type.c → event_bus.c → utils/json_utils.c
device_manager.c → node_config.c (config/)
```

**关键改进：**
- `device_get_json()` 和 `device_get_json_array()` 使用 `utils/json_utils.c`

### 3.4 Common 模块

```
common/
├── CMakeLists.txt
├── task_util.h          # 从 core/ 移动（已是头文件）
├── defaults.c/h         # 从 core/ 移动
├── sys_stats.c/h        # 从 core/ 移动
└── sys_info.c/h         # 从 core/ 移动
```

**依赖关系：**
```
defaults.c → node_config.c (config/)
sys_stats.c → event_bus.c (device/)
sys_info.c → (无核心依赖)
```

### 3.5 Web Server Handler 拆分

每个 handler 文件约 100-150 行，职责单一：

| Handler 文件 | 职责 | API 端点 |
|-------------|------|----------|
| `node_handler.c` | 节点身份 | `/api/node` |
| `device_handler.c` | 设备 CRUD | `/api/peripherals/*` |
| `config_handler.c` | 配置导入导出 | `/api/config` |
| `network_handler.c` | 网络配置 | `/api/network` |
| `system_handler.c` | 系统操作 | `/api/system/*` |
| `ota_handler.c` | OTA 升级 | `/api/ota/*` |
| `cert_handler.c` | 证书信息 | `/api/certs/*` |
| `wifi_handler.c` | Wi-Fi 配置 | `/api/wifi/*` |

**主文件 `web_server.c` 保留：**
- 启动/停止逻辑
- HTTPD 配置
- URI 注册循环
- WebSocket 启动

**关键改进：**
- 移除 `json_set_string()`、`mac_to_str()` 等（使用 utils）
- Handler 函数签名统一

### 3.6 AT 服务移除

#### 代码文件

| 文件 | 操作 |
|------|------|
| `main/at_service/at_service.c` | 删除（910 行）|
| `main/at_service/at_service.h` | 删除 |
| `main/app_main.c` | 移除 `#include "at_service/at_service.h"` 和 `#ifdef CONFIG_ESPX_AT_ENABLE` 块 |
| `main/CMakeLists.txt` | 移除 `at_service/at_service.c` 与 include 目录 `at_service` |
| `main/Kconfig.projbuild` | 移除 `ESPX_AT_ENABLE`、`ESPX_AT_UART_NUM`、`ESPX_AT_USE_DEFAULT_PINS`、`ESPX_AT_UART_TX_GPIO`、`ESPX_AT_UART_RX_GPIO`、`ESPX_AT_UART_BAUD`（第 120-188 行）|
| `sdkconfig.defaults` | 移除第 62-70 行 AT 配置块 |
| `sdkconfig` | 删除后由 `idf.py reconfigure` 重新生成 |

#### 工具文件

| 文件 | 操作 |
|------|------|
| `tools/at_test.py` | 删除（202 行 AT 回归测试）|
| `tools/gen_cert.py` | 第 86 行 `ser.write(b'AT+ID?\r\n')` 需改为使用 Web API 或删除该探测逻辑 |

#### `task_util.h` 注释更新

移除 AT 相关注释行：
```c
// 移除: at_service    AT+CFG -> config_apply -> NVS
```

#### 文档中 AT 内容移除

| 文档 | 涉及内容 |
|------|----------|
| `docs/ARCHITECTURE.md` | 第 25 行架构图、4.4 节 `Serial AT commands`、第 368/453/488 行、第 591-592 行目录树 |
| `docs/USAGE.md` | 第 3 行简介、第 4 章 `通道三：串口 AT 指令`（第 241-280+ 行）|
| `docs/TESTING.md` | 第 618-637 行 AT 测试说明与手工命令示例 |
| `docs/DEPLOYMENT.md` | 第 247 行安全表、第 284/291 行 OTA 示例、第 324/406 行调试命令 |
| `docs/DEVELOPMENT.md` | 第 55 行目录树、第 336 节 `AT 指令手工调试`、第 402 行任务栈表 |
| `README.md` | 第 36/43 行特性表、第 172/197-209 行示例、第 292 行说明 |
| `README.zh-CN.md` | 第 33/40 行特性表、第 144/178-190 行示例、第 268 行说明 |

**注意：** 删除 AT 后，配置通道从 4 条减少为 3 条（MQTT / HTTPS / 串口测试控制台）+ 工厂预置。需同步更新所有"四条通道"的表述，并将串口测试控制台作为唯一的串口通道。

---

## 4. 依赖更新矩阵

### 4.1 Import 路径变更

| 原路径 | 新路径 |
|--------|--------|
| `core/node_config.h` | `config/node_config.h` |
| `core/config_apply.h` | `config/config_apply.h` |
| `core/yaml.h` | `config/yaml.h` |
| `core/device_type.h` | `device/device_type.h` |
| `core/device_manager.h` | `device/device_manager.h` |
| `core/event_bus.h` | `device/event_bus.h` |
| `core/defaults.h` | `common/defaults.h` |
| `core/sys_stats.h` | `common/sys_stats.h` |
| `core/sys_info.h` | `common/sys_info.h` |
| (新增) | `utils/json_utils.h` |
| (新增) | `utils/str_utils.h` |
| (新增) | `utils/nvs_utils.h` |

### 4.2 CMake include_dirs 更新

```cmake
set(espx_include_dirs
    "."
    "utils"          # 新增
    "config"         # 从 core/ 重命名
    "device"         # 从 core/ 重命名
    "common"         # 从 core/ 重命名
    "peripherals"
    "mqtt_client"
    "cert_manager"
    "web_server"
    "wifi_prov"
    "test_mode"
    "mfg_provision"
    "ota_service"
    "net_services"
)
```

---

## 5. 内存优化

### 5.1 基准测试机制

扩展现有的 `common/sys_info.c`（`sys_info_add()`），无需新结构体。

当前 `/api/system/info` 已包含堆内存信息，需补充：

```c
// 在 sys_info_add() 中新增字段：
//   ram.iram_free / iram_min / iram_total   (MALLOC_CAP_INTERNAL)
//   ram.psram_free / psram_total            (MALLOC_CAP_SPIRAM)
//   tasks[]: name, stack_highwater, stack_size  (来自 vTaskGetRunTimeStats/
//            uxTaskGetStackHighWaterMark)
```

主机侧用 `tools/sysinfo_baseline.py`（新增）记录 `/api/system/info`
的多次采样，形成重构前后的对比基线。

### 5.2 优化方向（基于测试结果决定）

| 方向 | 条件 | 措施 |
|------|------|------|
| 栈使用 | 某个任务 high_water > 80% | 增大栈或优化代码 |
| 堆碎片 | 频繁 alloc/free | 考虑内存池 |
| PSRAM 利用 | PSRAM 利用率 < 50% | 将更多缓冲区移到 PSRAM |

---

## 6. 文档更新

### 6.1 因重构而更新的文档

| 文档 | 更新内容 |
|------|----------|
| `docs/ARCHITECTURE.md` | 目录结构、依赖图、模块职责（core/ 拆分为 config/device/common）|
| `docs/DEVELOPMENT.md` | 目录树、新增模块位置、编译测试命令 |
| `docs/TESTING.md` | utils 模块主机测试说明、`run_yaml_tests.sh` 路径 |
| `AGENT.md` | 第 5 节分层图的模块路径引用 |
| `tests/run_yaml_tests.sh` | `main/core/yaml.c` → `main/config/yaml.c`；`-I$ROOT/main/core` → `-I$ROOT/main/config` |

### 6.2 因移除 AT 而更新的文档

见 3.6 节"文档中 AT 内容移除"表。要点：

- 配置通道从 **4 条** 减少为 **3 条**（MQTT / HTTPS / 测试控制台）+ 工厂预置
- 移除所有 AT 命令示例，替换为等价的 REST API 或 MQTT 示例
- 移除 AT 相关安全条目（`DEPLOYMENT.md` 第 247 行）

---

## 7. 测试计划

### 7.1 主机测试（无需硬件）

```bash
# YAML 解析器测试（经 tests/run_yaml_tests.sh，重构后需更新路径）
tests/run_yaml_tests.sh

# 等价地手动执行（重构后）：
gcc -Wall -Wextra -Werror -Wno-unused-parameter \
    -I main/config -I managed_components/espressif__cjson/cJSON \
    main/config/yaml.c managed_components/espressif__cjson/cJSON/cJSON.c \
    tests/yaml_test_core.c -o /tmp/yaml_core -lm && /tmp/yaml_core

# Utils 测试（新增）
gcc -Wall -Wextra -Werror -I main/utils \
    main/utils/*.c tests/utils_test.c -o /tmp/utils_test && /tmp/utils_test
```

### 7.2 设备测试

```bash
# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 产线自检控制台测试（不依赖 AT，AT 移除后仍有效）
python3 tools/espx_test.py --port /dev/ttyUSB0 all

# 网络回归（不含 AT）
python3 tools/network_tests.py --device-ip <ip> --local-ip <ip> --patch <p>
```

### 7.3 AT 移除专项验证

```bash
# 确认无残留引用
grep -rn "at_service\|ESPX_AT\|AT+[A-Z]" main/ tools/ docs/ README*.md \
  | grep -v "superpowers/specs"
# 应无输出
```

---

## 8. 实施顺序

**原则：先做最小、最独立的改动（AT 移除），再做大范围移动，最后做代码替换。**
AT 移除优先还有一个好处：它让重构的基线少 910 行代码。

| 阶段 | 内容 | 验证方式 | 风险 |
|------|------|----------|------|
| **Phase 1** | 移除 AT 服务（代码 + 工具 + 配置）| `idf.py build` 通过；`grep` 无残留 | 低 |
| **Phase 2** | 更新 AT 相关文档 | 通读校对 | 低 |
| **Phase 3** | 创建 `utils/` 模块 + 主机测试 | `gcc` 主机测试通过 | 低 |
| **Phase 4** | 移动 `config/`（纯移动，不改代码）| `idf.py build` 通过 | 低 |
| **Phase 5** | 移动 `device/`（纯移动）| `idf.py build` 通过 | 低 |
| **Phase 6** | 移动 `common/`（纯移动）| `idf.py build` 通过 | 低 |
| **Phase 7** | 更新全部 import 路径 + CMakeLists | `idf.py build` 0 warning | 中 |
| **Phase 8** | 将 utils 函数应用到各模块 | 主机测试 + `idf.py build` | 中 |
| **Phase 9** | 拆分 `web_server/` handlers | 逐个 API 端点冒烟测试 | 中 |
| **Phase 10** | 更新重构文档（ARCHITECTURE/DEVELOPMENT/AGENT）| 通读校对 | 低 |
| **Phase 11** | 内存基准测量 + 针对性优化 | `/api/system/info` 前后对比 | 中 |

**分阶段提交建议：** 每个 Phase 一个独立 commit，便于回滚。

**Phase 4-6 的"纯移动"含义：** 只移动文件、更新 `CMakeLists.txt` 路径，
不修改任何代码内容。这样编译通过就证明移动本身无误，
与后续的 import 路径更新（Phase 7）隔离开来。

---

## 9. 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Import 路径遗漏 | 编译失败 | Phase 7 用 `grep -rn "core/" main/` 辅助；每阶段编译验证 |
| `run_yaml_tests.sh` 路径未更新 | 主机测试失败 | Phase 4 移动 yaml.c 时同步更新 |
| cJSON 操作遗漏 | 内存泄漏 | Phase 8 逐文件审查 `cJSON_Delete` 配对 |
| Handler 拆分遗漏 URI | 404 | Phase 9 对照 3.5 节端点表逐个测试 |
| AT 移除遗漏引用 | 编译失败 / 文档误导 | Phase 1 结束跑 7.3 节的 `grep` 检查 |
| `sdkconfig` 残留 AT 项 | 编译告警 | 删除 `sdkconfig` 后 `idf.py reconfigure` 重新生成 |
| 移动文件后 `EMBED_FILES` 路径失效 | 链接失败 | `web_files/index.html`、certs 保持原位，只移动代码文件 |

---

## 10. 验收标准

- [ ] 编译 0 error / 0 warning
- [ ] `tests/run_yaml_tests.sh` 通过（路径已更新）
- [ ] 新增的 utils 主机测试通过
- [ ] `python3 tools/espx_test.py --port <port> all` 设备测试通过
- [ ] `grep -rn "at_service\|ESPX_AT"` 无残留（排除 spec 文档）
- [ ] AT 服务移除后固件 `.bin` 体积减小
- [ ] `/api/system/info` 返回内部 RAM 与 PSRAM 分项统计
- [ ] 所有 Web API 端点（3.5 节表）逐一可用
- [ ] `docs/ARCHITECTURE.md`、`docs/DEVELOPMENT.md`、`AGENT.md` 反映新结构
- [ ] README / README.zh-CN / USAGE / TESTING / DEPLOYMENT 无 AT 残留
