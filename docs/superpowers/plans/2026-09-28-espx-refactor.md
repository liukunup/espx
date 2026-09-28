# ESPX 重构实现计划（移除 AT + 模块边界重构）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 移除串口 AT 指令支持，并把 `main/core/` 拆分为 `config/`、`device/`、`common/`，新增 `utils/` 公共工具模块，拆分 `web_server.c` 为多个 handler 模块。

**Architecture:** 三阶段推进。(1) 先做最独立的 AT 移除，缩小重构基线 910 行；(2) 纯文件移动 + 扁平 include 路径调整（全仓仅 2 处 `core/` 前缀 include）；(3) 提取公共工具并逐模块替换重复代码，最后拆分 web server。

**Tech Stack:** ESP-IDF v6.x、C99、FreeRTOS、cJSON、ESP HTTPS Server、NVS、CMake。

**Spec:** `docs/superpowers/specs/2026-09-28-espx-refactor-design.md`

**Branch:** `feature/esp32-iot-firmware`（当前分支，工作区干净，不需要 worktree）

## Global Constraints

- 编译必须 **0 error / 0 warning**（项目启用 `-Werror`）。
- `yaml.c` **不得**引入任何 ESP-IDF 依赖，必须可主机（gcc）单元测试。
- 依赖只能向下（AGENT.md 第 5 节）：`config_apply` → `yaml`/`device_manager`/`node_config`/`event_bus` → `device_type` → `peripherals`。通道层（mqtt/https/console）只做解析与转发。
- **NVS 命名空间与键名不得改变**：`espx_node`/`config`、`espx_devices`/`config`。改名会导致已部署设备配置静默丢失。
- **对外契约不得改变**：MQTT 主题前缀与 REST 路径保持不变。
- 敏感值（含 `password`/`key`）日志只打印 `<set>`。
- 每个 Phase 一个独立 commit，便于回滚。
- AT 移除后配置通道为 **3 条**：MQTT YAML、HTTPS `POST /api/config`、产线串口控制台；外加 `mfg_data` 工厂预置。

## Deviations from spec

Spec 3.1 提议的 utils API 中，以下函数经 YAGNI 检查后**不实现**（无现有重复代码可消除，或与既有实现重复）：

| Spec 提议 | 决定 | 理由 |
|---|---|---|
| `json_dup()` | 不实现 | `cJSON_Duplicate(o, true)` 已是一行，包装无增益 |
| `json_has_string()` | 不实现 | 无调用方 |
| `str_eq_ignore_case()` | 不实现 | libc 已有 `strcasecmp` |
| `str_is_blank()` / `str_trim()` | 不实现 | 新增功能，非消除重复；有需要时再加 |
| `timestamp_to_iso8601()` | 不实现 | 与 `time_sync_iso8601()`（net_services）重复 |
| `nvs_open_ns()` / `nvs_update_json()` | 不实现 | 无调用方 / 无需 read-modify-write |
| `config_apply.c` 的 `is_secret()` 移入 utils | 不移动 | 仅 1 处使用，非重复 |

**保留并实现的 API（均有实测重复代码支撑）：**

```
json_utils.h:  json_get_string / json_get_int / json_get_bool
               json_set_string / json_set_number / json_set_bool
str_utils.h:   str_copy / str_contains / mac_to_str
nvs_utils.h:   nvs_load_alloc / nvs_load_json / nvs_save_json
```

**证据：** `cJSON_IsString()` 守卫 35 处；`strncpy()` 25 处；`strstr()` 8 处；NVS「两次 `nvs_get_str` + malloc」模式 3 处（`node_config.c`、`device_manager.c`、`mfg_provision.c`）；`mac_to_str()` 与 `json_set_string()` 在 `web_server.c` 内定义。

## Review Focus

以下 5 类输入/失败模式是 spec 隐含要求但任何任务的测试都不会自动覆盖的，最可能伤到使用者。每一条都在其归属任务中钉了测试：

1. **NVS 字面量不变** — 重构后 `espx_node`/`config` 等命名空间与键名必须逐字不变；改动会让已部署设备的配置在 OTA 后静默消失。→ Task 10 步骤 5 用 `grep -F` 断言字面量。
2. **`str_copy` 截断语义** — 源串长于目标缓冲区时，必须 NUL 结尾且不越界写。→ Task 5 测试 `test_str_copy` 的截断分支。
3. **`json_set_string(key, NULL)` 删除键** — Web UI 清空 Wi-Fi 密码依赖这一语义。→ Task 4 测试 `test_set_string_replace`。
4. **Handler 注册顺序** — `/api/peripherals/*` 通配路由必须在精确路径之后注册，否则 `/api/peripherals/reload` 被通配吞掉。→ Task 13 步骤 6 的 curl 冒烟测试。
5. **`EMBED_FILES` 符号仍链接** — 拆分 `web_server.c` 后 `_binary_index_html_start` 必须仍可解析，`GET /` 仍返回 UI。→ Task 12 步骤 6。

---

## Task 1: 移除 AT 服务（代码与编译配置）

**Files:**
- Delete: `main/at_service/at_service.c`、`main/at_service/at_service.h`（整个目录）
- Modify: `main/app_main.c:36`、`main/app_main.c:90-97`
- Modify: `main/CMakeLists.txt:20-21`、`main/CMakeLists.txt:76-77`
- Modify: `main/Kconfig.projbuild:120-188`
- Modify: `sdkconfig.defaults:62-70`
- Modify: `main/core/task_util.h:25`

**Interfaces:**
- Consumes: 无
- Produces: 无（纯删除；`main/` 不再有 `at_service` 目标）

- [ ] **Step 1: 删除 AT 服务源文件**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
git rm -r main/at_service
```

- [ ] **Step 2: 从 `app_main.c` 移除 AT include 与启动代码**

删除第 36 行：
```c
#include "at_service/at_service.h"
```

删除第 90-97 行整块（注释 + `#ifdef`）：
```c
    /* ---- 3. Serial AT interface ----------------------------------------- */
    /* Started before the network so a host MCU can talk to the node even while
     * it is still unprovisioned. */
#ifdef CONFIG_ESPX_AT_ENABLE
    if (at_service_start() != ESP_OK) {
        ESP_LOGW(TAG, "AT service failed to start");
    }
#endif
```

并把紧随其后的 `/* ---- 4. Configuration ---` 注释与下一个 `/* ---- 4. Core services ---` 重复编号修正为 3/4：

```c
    /* ---- 3. Configuration ---------------------------------------------- */
    ESP_LOGI(TAG, "Initializing configuration...");
    ESP_ERROR_CHECK(node_config_init());
    ESP_ERROR_CHECK(node_config_load());

    /* ---- 4. Core services ---------------------------------------------- */
```

- [ ] **Step 3: 从 `main/CMakeLists.txt` 移除 AT 源码与 include 目录**

删除这两行：
```cmake
    # Serial AT interface
    "at_service/at_service.c"
```

删除 include 目录里的一行：
```cmake
    "at_service"
```

- [ ] **Step 4: 从 `Kconfig.projbuild` 删除整个 AT 配置段**

删除从 `    # =====...` 分隔注释 `Serial AT interface` 开始，到 `config ESPX_AT_UART_BAUD` 条目结束（含 `depends on ESPX_AT_ENABLE`）的整段：

```kconfig
    # =====================================================================
    # Serial AT interface
    # =====================================================================

    config ESPX_AT_ENABLE
        ...
    config ESPX_AT_UART_BAUD
        int "AT UART baud rate"
        default 115200
        depends on ESPX_AT_ENABLE
```

保留其后的 `# Network services (require an IP address)` 分隔段。确认删除后文件里不再含 `ESPX_AT`：

```bash
grep -c "ESPX_AT" main/Kconfig.projbuild   # 期望输出 0
```

- [ ] **Step 5: 从 `sdkconfig.defaults` 删除 AT 配置块**

删除第 62-70 行：
```
# --- Serial AT interface (UART1, 115200) ---
CONFIG_ESPX_AT_ENABLE=y
CONFIG_ESPX_AT_UART_NUM=1
# UART1's own IO_MUX pins (TX=GPIO17, RX=GPIO18) -- no GPIO-matrix routing.
CONFIG_ESPX_AT_USE_DEFAULT_PINS=y
CONFIG_ESPX_AT_UART_TX_GPIO=17
CONFIG_ESPX_AT_UART_RX_GPIO=18
CONFIG_ESPX_AT_UART_BAUD=115200
```

- [ ] **Step 6: 更新 `task_util.h` 的任务归属注释**

把 `at_service` 那一行从「MUST stay on internal RAM」清单中删除：

```c
 *   MUST stay on internal RAM (touches flash via NVS or the OTA partition):
 *     ota_service   esp_ota_write()
 *     at_service    AT+CFG -> config_apply -> NVS      <-- 删除这一行
 *     ws_server     a config push over the socket -> NVS
```

- [ ] **Step 7: 删除本地 `sdkconfig` 并重新生成（文件已被 gitignore）**

```bash
rm -f sdkconfig
idf.py reconfigure
```

- [ ] **Step 8: 编译验证**

```bash
idf.py build 2>&1 | tail -20
```

Expected: `Project build complete`，无 `at_service` 相关符号，0 warning。

- [ ] **Step 9: 断言无残留引用**

```bash
grep -rn "at_service\|ESPX_AT" main/ sdkconfig.defaults 2>/dev/null
```

Expected: 无输出。

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "refactor(at): remove serial AT command service

Removes main/at_service/ (910 lines), its Kconfig options and
sdkconfig.defaults entries. Configuration channels are now MQTT,
HTTPS POST /api/config, the manufacturing console, and factory data."
```

---

## Task 2: 移除 / 修正 AT 相关工具脚本

**Files:**
- Delete: `tools/at_test.py`
- Modify: `tools/gen_cert.py:86`

**Interfaces:**
- Consumes: 无
- Produces: `tools/gen_cert.py::get_device_id_from_serial()` 在无 AT 通道时返回 `None`（保持原契约：调用方已处理 `None`）

- [ ] **Step 1: 删除 AT 回归测试脚本**

```bash
git rm tools/at_test.py
```

- [ ] **Step 2: 处理 `gen_cert.py` 的 AT 探测**

`get_device_id_from_serial()` 通过 `AT+ID?` 读取设备 ID。AT 移除后该探测必然失败，函数应显式说明并返回 `None`（保留函数与其调用契约，避免改动调用方）。

将 `tools/gen_cert.py` 中 `get_device_id_from_serial()` 的函数体整体替换为：

```python
def get_device_id_from_serial():
    """Read device ID from serial connection.

    Not supported: the firmware no longer exposes a serial AT command
    interface, so there is no AT+ID? query to issue. Device IDs are
    derived from the MAC (see mac_to_device_id) or supplied explicitly.
    """
    return None
```

- [ ] **Step 3: 确认脚本仍可导入**

```bash
python3 -c "import ast,sys; ast.parse(open('tools/gen_cert.py').read()); print('gen_cert.py OK')"
```

Expected: `gen_cert.py OK`

- [ ] **Step 4: 断言无 AT 残留**

注意：Step 2 的 docstring 故意包含字面量 `AT+ID?` 来解释该探测为何被移除，
因此这里只能断言**可执行**的 AT 用法与 `at_test` 引用已消失，不能用宽泛的 `AT+ID` 子串。

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
# 1) 不得再有 at_test 引用
grep -rn "at_test" tools/*.py tools/*.sh 2>/dev/null || echo "no at_test refs OK"
# 2) 不得再有可执行的 AT 命令写入（排除新增的解释性 docstring）
grep -rn "AT+[A-Z]\(\\|'\)" tools/*.py 2>/dev/null || echo "no executable AT usage OK"
# 3) 确认被替换的函数体已不是原实现
grep -n "serial.Serial" tools/gen_cert.py || echo "serial.Serial gone OK"
```

Expected: 三条均输出对应的 `OK`（前两条对 docstring 行不匹配）。

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore(tools): drop AT test harness, disable AT device-id probe"
```

---

## Task 3: 更新文档以移除 AT 内容

**Files:**
- Modify: `README.md`（第 36、43、172、197-209、292 行附近）
- Modify: `README.zh-CN.md`（第 33、40、144、178-190、268 行附近）
- Modify: `docs/ARCHITECTURE.md`（第 25、223-237、368、453、488、591-592 行附近）
- Modify: `docs/USAGE.md`（第 3、241-300+ 行附近）
- Modify: `docs/TESTING.md`（第 618-637 行附近）
- Modify: `docs/DEPLOYMENT.md`（第 247、284-291、324、406 行附近）
- Modify: `docs/DEVELOPMENT.md`（第 55、336-345、402 行附近）

**Interfaces:**
- Consumes: 无
- Produces: 无（文档一致性）

**统一改写规则（对每个文件都适用）：**

1. 把「四种配置通道」（Four configuration channels）改为「三种配置通道」（Three configuration channels），并去掉「serial AT commands」一项。
2. 删除 AT 命令表/示例段落。
3. 把 AT 示例替换为等价的 REST / MQTT 示例：
   - 导出配置：`AT+CFG?` → `curl -k https://<ip>/api/config`
   - 下发配置：`AT+CFG=<yaml>` → `curl -k -X POST --data-binary @cfg.yaml https://<ip>/api/config`
   - 设备列表：`AT+DEV?` → `curl -k https://<ip>/api/peripherals`
   - 写设备：`AT+DEV="relay_a",true` → `curl -k -X POST -d 'true' https://<ip>/api/peripherals/relay_a/write`（请求体**就是**值本身，没有 `value` 包装层；继电器驱动接受裸 bool `true` 或 `{"state":true}`，见 `main/peripherals/relay.c:83-99`）
   - 重启：`AT+RST` → `curl -k -X POST https://<ip>/api/system/reboot`
   - 版本：`AT+GMR` → `curl -k https://<ip>/api/node`
   - 进入产线自检：`AT+TESTMODE` → `curl -k -X POST https://<ip>/api/system/testmode`
4. 保留并明确「产线串口控制台」（test-mode console，UART0）为**唯一**串口配置通道。

- [ ] **Step 1: 改写 `README.md`**

- 特性表第 36 行：`MQTT (YAML), HTTPS POST /api/config, serial AT commands, factory data` → `MQTT (YAML), HTTPS POST /api/config, factory data`
- 特性表第 43 行「Serial AT commands」整行删除。
- 第 172 行 AT 配置示例 → `curl -k -X POST --data-binary @cfg.yaml https://<device-ip>/api/config`
- 第 197-209 行「AT+GMR … AT+HELP?」整块替换为「REST API quick reference」列表（见上方规则 3）。
- 第 292 行第 6 条「The AT command set is a subset of ESP-AT」整条删除，并把该列表后续项重新编号。

- [ ] **Step 2: 改写 `README.zh-CN.md`**（与 Step 1 一一对应：第 33、40、144、178-190、268 行）

- [ ] **Step 3: 改写 `docs/ARCHITECTURE.md`**

- 第 25 行架构图中的 `host MCU ──┤ Serial AT commands (UART1, ESP-AT style) │` 该行删除（保持 ASCII 边框对齐）。
- 第 223-237 行「### 4.4 Serial AT commands」整节删除，并把原 4.5 及其后小节编号前移（4.5→4.4，依此类推）。
- 第 368 行 `same string as the MQTT topic prefix and AT+ID` → `same string as the MQTT topic prefix and the device id`
- 第 453 行启动序列图里的 `├─ at_service_start()  // UART1, network independent` 整行删除。
- 第 488 行「`at_service_start()` also runs before the network, so a host MCU…」整段删除。
- 第 591-592 行目录树里的 `├── at_service/` 与 `│   └── at_service.{c,h}` 两行删除。

- [ ] **Step 4: 改写 `docs/USAGE.md`**

- 第 3 行简介里的「与 AT 指令的完整用法」→「的完整用法」
- 第 241 行「## 4. 通道三：串口 AT 指令」整节（含命令表、示例、`AT+MQTTPUB` 说明）替换为「## 4. 通道三：产线串口控制台」，内容改为指向 `docs/TESTING.md` 的测试控制台用法（`help` / `types` / `list` / `add` / `del` / `read` / `write` / `cfg`）。
- 后续「通道四」等小节编号前移。

- [ ] **Step 5: 改写 `docs/TESTING.md`**

- 第 618-637 行 AT 测试命令与手工 AT 示例整块删除。
- 在主机/设备测试清单中删除 `tools/at_test.py` 条目。

- [ ] **Step 6: 改写 `docs/DEPLOYMENT.md`**

- 第 247 行安全表「串口 AT」整行删除，后续行号重排。
- 第 284、291 行 OTA 的 `AT+OTASTART` / `AT+OTASTATUS` 示例 → `curl -k -X POST -d '{"url":"http://…/p.patch"}' https://<ip>/api/ota/start` 与 `curl -k https://<ip>/api/ota/status`。
- 第 324 行 `AT+CIFSR` → `curl -k https://<ip>/api/system/info`
- 第 406 行 `AT+MQTTCONN? 查询` → `curl -k https://<ip>/api/network`

- [ ] **Step 7: 改写 `docs/DEVELOPMENT.md`**

- 第 55 行目录树 `├── at_service/  串口 AT 指令` 整行删除。
- 第 336-345 行「### AT 指令手工调试」整节删除。
- 第 402 行任务栈表里 `at_service | AT+CFG → config_apply → 写 NVS` 整行删除。

- [ ] **Step 8: 断言文档无 AT 残留**

```bash
grep -rn "AT+\|at_service\|ESPX_AT" README.md README.zh-CN.md docs/*.md \
  | grep -v "docs/superpowers/"
```

Expected: 无输出。

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "docs: remove serial AT command references

Configuration channels are documented as MQTT, HTTPS and the
manufacturing console only."
```

---

## Task 4: 新增 `utils/json_utils`（TDD）

**Files:**
- Create: `main/utils/json_utils.h`
- Create: `main/utils/json_utils.c`
- Create: `tests/json_utils_test.c`
- Modify: `main/CMakeLists.txt`（SRCS 增加源码，include_dirs 增加 `"utils"`）

**Interfaces:**
- Consumes: `cJSON`（已有 `REQUIRES ... cjson`）
- Produces:
  - `const char *json_get_string(const cJSON *obj, const char *key, const char *def)`
  - `int json_get_int(const cJSON *obj, const char *key, int def)`
  - `bool json_get_bool(const cJSON *obj, const char *key, bool def)`
  - `void json_set_string(cJSON *obj, const char *key, const char *value)`（`value == NULL` 时删除键）
  - `void json_set_number(cJSON *obj, const char *key, double value)`
  - `void json_set_bool(cJSON *obj, const char *key, bool value)`

- [ ] **Step 1: 写失败测试 `tests/json_utils_test.c`**

```c
/* Host unit tests for main/utils/json_utils.c (no ESP-IDF dependency). */
#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include "json_utils.h"

static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_get_string(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", "Node-1");
    cJSON_AddNumberToObject(o, "port", 1883);

    CHECK(strcmp(json_get_string(o, "name", "?"), "Node-1") == 0);
    /* missing key -> default */
    CHECK(strcmp(json_get_string(o, "nope", "def"), "def") == 0);
    /* wrong type -> default */
    CHECK(strcmp(json_get_string(o, "port", "def"), "def") == 0);
    /* NULL object -> default */
    CHECK(strcmp(json_get_string(NULL, "name", "def"), "def") == 0);
    /* empty string is a value, not a default */
    cJSON_AddStringToObject(o, "empty", "");
    CHECK(strcmp(json_get_string(o, "empty", "def"), "") == 0);

    cJSON_Delete(o);
}

static void test_get_scalars(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "port", 1883);
    cJSON_AddBoolToObject(o, "enabled", 1);
    cJSON_AddStringToObject(o, "name", "x");

    CHECK(json_get_int(o, "port", 0) == 1883);
    CHECK(json_get_int(o, "name", -1) == -1);
    CHECK(json_get_bool(o, "enabled", false) == true);
    CHECK(json_get_bool(o, "name", true) == true);
    CHECK(json_get_int(NULL, "port", 7) == 7);

    cJSON_Delete(o);
}

static void test_set_string_replace(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "k", "old");

    json_set_string(o, "k", "new");
    CHECK(strcmp(json_get_string(o, "k", ""), "new") == 0);
    CHECK(cJSON_GetArraySize(o) == 1); /* replaced, not duplicated */

    json_set_string(o, "fresh", "v"); /* insert when missing */
    CHECK(strcmp(json_get_string(o, "fresh", ""), "v") == 0);
    CHECK(cJSON_GetArraySize(o) == 2);

    /* NULL deletes the key: the Web UI clears the Wi-Fi password this way */
    json_set_string(o, "fresh", NULL);
    CHECK(cJSON_GetObjectItem(o, "fresh") == NULL);
    CHECK(cJSON_GetArraySize(o) == 1);

    cJSON_Delete(o);
}

static void test_set_scalars(void)
{
    cJSON *o = cJSON_CreateObject();
    json_set_number(o, "n", 42);
    json_set_bool(o, "b", true);
    CHECK(json_get_int(o, "n", 0) == 42);
    CHECK(json_get_bool(o, "b", false) == true);

    json_set_number(o, "n", 7); /* replace in place */
    CHECK(json_get_int(o, "n", 0) == 7);
    CHECK(cJSON_GetArraySize(o) == 2);

    /* NULL object must not crash */
    json_set_string(NULL, "k", "v");
    json_set_number(NULL, "k", 1);
    json_set_bool(NULL, "k", true);

    cJSON_Delete(o);
}

int main(void)
{
    test_get_string();
    test_get_scalars();
    test_set_string_replace();
    test_set_scalars();

    if (g_fail == 0) {
        printf("json_utils: all tests passed\n");
        return 0;
    }
    printf("json_utils: %d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
mkdir -p /tmp/espx-t
gcc -Wall -Wextra -Werror -Wno-unused-parameter \
    -Imain/utils -Imanaged_components/espressif__cjson/cJSON \
    tests/json_utils_test.c managed_components/espressif__cjson/cJSON/cJSON.c \
    -o /tmp/espx-t/json_utils_test -lm
```

Expected: 编译失败，报错类似 `fatal error: 'json_utils.h' file not found`。

- [ ] **Step 3: 写头文件 `main/utils/json_utils.h`**

```c
/**
 * @file json_utils.h
 * @brief Small, type-checked helpers over cJSON
 *
 * cJSON's accessors are not type-safe: cJSON_GetObjectItem() returns an item
 * whatever its type, and reading ->valuestring off a number is a NULL
 * dereference. These helpers collapse the repeated
 * "cJSON_IsString(x) ? x->valuestring : default" pattern into one call and
 * always return a usable value.
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdbool.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a string member
 *
 * @param obj  Object to read from (NULL yields @p def)
 * @param key  Member name
 * @param def  Value returned when the member is absent or not a string
 * @return The string value, or @p def. Never NULL unless @p def is NULL.
 */
const char *json_get_string(const cJSON *obj, const char *key, const char *def);

/**
 * @brief Read a numeric member
 *
 * @return The integer value, or @p def when absent or not a number
 */
int json_get_int(const cJSON *obj, const char *key, int def);

/**
 * @brief Read a boolean member
 *
 * @return The boolean value, or @p def when absent or not a bool
 */
bool json_get_bool(const cJSON *obj, const char *key, bool def);

/**
 * @brief Set a string member, replacing any existing value
 *
 * @param value New value. Passing NULL REMOVES the member (the Web UI relies
 *              on this to clear a stored Wi-Fi password).
 */
void json_set_string(cJSON *obj, const char *key, const char *value);

/**
 * @brief Set a numeric member, replacing any existing value
 */
void json_set_number(cJSON *obj, const char *key, double value);

/**
 * @brief Set a boolean member, replacing any existing value
 */
void json_set_bool(cJSON *obj, const char *key, bool value);

#ifdef __cplusplus
}
#endif

#endif /* JSON_UTILS_H */
```

- [ ] **Step 4: 写实现 `main/utils/json_utils.c`**

```c
/**
 * @file json_utils.c
 * @brief Implementation of the cJSON helpers (see json_utils.h)
 */

#include <string.h>

#include "json_utils.h"

const char *json_get_string(const cJSON *obj, const char *key, const char *def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return def;
}

int json_get_int(const cJSON *obj, const char *key, int def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return def;
}

bool json_get_bool(const cJSON *obj, const char *key, bool def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return def;
}

void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    /* cJSON_ReplaceItemInObject() ignores a key that does not exist yet, which
     * silently drops first-time settings. Delete, then add. */
    cJSON_DeleteItemFromObject(obj, key);
    if (value != NULL) {
        cJSON_AddStringToObject(obj, key, value);
    }
}

void json_set_number(cJSON *obj, const char *key, double value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddNumberToObject(obj, key, value);
}

void json_set_bool(cJSON *obj, const char *key, bool value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddBoolToObject(obj, key, value);
}
```

- [ ] **Step 5: 运行测试，确认通过**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
gcc -Wall -Wextra -Werror -Wno-unused-parameter \
    -Imain/utils -Imanaged_components/espressif__cjson/cJSON \
    tests/json_utils_test.c main/utils/json_utils.c \
    managed_components/espressif__cjson/cJSON/cJSON.c \
    -o /tmp/espx-t/json_utils_test -lm && /tmp/espx-t/json_utils_test
```

Expected: `json_utils: all tests passed`

- [ ] **Step 6: 接入构建（`main/CMakeLists.txt`）**

在 SRCS 的 `"app_main.c"` 下新增：

```cmake
    # Application entry
    "app_main.c"

    # Shared helpers
    "utils/json_utils.c"
```

在 `espx_include_dirs` 顶部新增 `"utils"`：

```cmake
set(espx_include_dirs
    "."
    "utils"
    "core"
    ...
)
```

- [ ] **Step 7: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(utils): add type-safe cJSON helpers with host tests"
```

---

## Task 5: 新增 `utils/str_utils`（TDD）

**Files:**
- Create: `main/utils/str_utils.h`
- Create: `main/utils/str_utils.c`
- Create: `tests/str_utils_test.c`
- Modify: `main/CMakeLists.txt`（SRCS 增加 `"utils/str_utils.c"`）

**Interfaces:**
- Consumes: libc（`string.h`、`stdio.h`、`ctype.h`）、`stdint.h`
- Produces:
  - `size_t str_copy(char *dest, size_t dest_size, const char *src)`
  - `bool str_contains(const char *haystack, const char *needle)`
  - `void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size)`

- [ ] **Step 1: 写失败测试 `tests/str_utils_test.c`**

```c
/* Host unit tests for main/utils/str_utils.c (no ESP-IDF dependency). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "str_utils.h"

static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_str_copy(void)
{
    char buf[8];

    memset(buf, 'X', sizeof(buf));
    CHECK(str_copy(buf, sizeof(buf), "abc") == 3);
    CHECK(strcmp(buf, "abc") == 0);

    /* exact fit: 7 chars + NUL in 8 bytes */
    CHECK(str_copy(buf, sizeof(buf), "1234567") == 7);
    CHECK(strcmp(buf, "1234567") == 0);

    /* truncation: NUL-terminated, last byte is NUL, nothing past the end */
    memset(buf, 'X', sizeof(buf));
    CHECK(str_copy(buf, sizeof(buf), "0123456789abcdef") == 7);
    CHECK(strcmp(buf, "0123456") == 0);
    CHECK(buf[7] == '\0');

    /* NULL source empties the destination */
    CHECK(str_copy(buf, sizeof(buf), NULL) == 0);
    CHECK(buf[0] == '\0');

    /* zero size writes nothing */
    CHECK(str_copy(buf, 0, "abc") == 0);

    /* NULL destination is a no-op */
    CHECK(str_copy(NULL, 8, "abc") == 0);

    /* size 1 writes only the terminator */
    buf[0] = 'X';
    CHECK(str_copy(buf, 1, "abc") == 0);
    CHECK(buf[0] == '\0');
}

static void test_str_contains(void)
{
    CHECK(str_contains("wifi_password", "password") == true);
    CHECK(str_contains("pass", "password") == false);
    CHECK(str_contains("mqtt://host", "://") == true);
    CHECK(str_contains("abc", "") == true);
    CHECK(str_contains(NULL, "x") == false);
    CHECK(str_contains("x", NULL) == false);
    CHECK(str_contains(NULL, NULL) == false);
}

static void test_mac_to_str(void)
{
    const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    char buf[18];

    mac_to_str(mac, buf, sizeof(buf));
    CHECK(strcmp(buf, "DE:AD:BE:EF:00:01") == 0);

    /* small buffer truncates safely and stays NUL-terminated */
    char small[9];
    memset(small, 'X', sizeof(small));
    mac_to_str(mac, small, sizeof(small));
    CHECK(strlen(small) == 8);
    CHECK(small[8] == '\0');

    /* NULL mac empties */
    mac_to_str(NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0');

    /* NULL dest / zero size must not crash */
    mac_to_str(mac, NULL, 18);
    mac_to_str(mac, buf, 0);
}

int main(void)
{
    test_str_copy();
    test_str_contains();
    test_mac_to_str();

    if (g_fail == 0) {
        printf("str_utils: all tests passed\n");
        return 0;
    }
    printf("str_utils: %d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
gcc -Wall -Wextra -Werror -Imain/utils tests/str_utils_test.c -o /tmp/espx-t/str_utils_test
```

Expected: 编译失败，`str_utils.h: No such file or directory`。

- [ ] **Step 3: 写头文件 `main/utils/str_utils.h`**

```c
/**
 * @file str_utils.h
 * @brief String helpers that are safe by construction
 *
 * The codebase used `strncpy(dst, src, sizeof(dst) - 1)` in 25 places. That
 * idiom is wrong by one (it wastes a byte and never terminates when the source
 * fills the buffer) and easy to get wrong when the buffer arrives as a pointer
 * plus a length. str_copy() states the intent once.
 */

#ifndef STR_UTILS_H
#define STR_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy a string into a fixed-size buffer, always NUL-terminating
 *
 * Never writes past @p dest_size bytes. A source longer than the buffer is
 * truncated; the destination is still NUL-terminated.
 *
 * @param dest       Destination buffer (may be NULL, then nothing happens)
 * @param dest_size  Total size of @p dest in bytes
 * @param src        Source string (may be NULL, then @p dest is emptied)
 * @return Number of characters copied, excluding the terminator
 */
size_t str_copy(char *dest, size_t dest_size, const char *src);

/**
 * @brief Whether @p haystack contains @p needle
 *
 * @return false when either argument is NULL
 */
bool str_contains(const char *haystack, const char *needle);

/**
 * @brief Render a 6-byte MAC as "AA:BB:CC:DD:EE:FF"
 *
 * @param mac       6-byte address (NULL empties @p dest)
 * @param dest      Destination buffer
 * @param dest_size Size of @p dest; 18 bytes holds the full string
 */
void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size);

#ifdef __cplusplus
}
#endif

#endif /* STR_UTILS_H */
```

- [ ] **Step 4: 写实现 `main/utils/str_utils.c`**

```c
/**
 * @file str_utils.c
 * @brief Implementation of the string helpers (see str_utils.h)
 */

#include <string.h>
#include <stdio.h>

#include "str_utils.h"

size_t str_copy(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return 0;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}

bool str_contains(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (mac == NULL) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
```

- [ ] **Step 5: 运行测试，确认通过**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
gcc -Wall -Wextra -Werror -Imain/utils \
    tests/str_utils_test.c main/utils/str_utils.c -o /tmp/espx-t/str_utils_test \
    && /tmp/espx-t/str_utils_test
```

Expected: `str_utils: all tests passed`

- [ ] **Step 6: 接入构建**

在 `main/CMakeLists.txt` 的 SRCS 中，`"utils/json_utils.c"` 下一行新增：

```cmake
    "utils/str_utils.c"
```

- [ ] **Step 7: 编译验证 + Commit**

```bash
idf.py build 2>&1 | tail -5
git add -A
git commit -m "feat(utils): add overflow-safe string helpers with host tests"
```

---

## Task 6: 新增 `utils/nvs_utils`

**Files:**
- Create: `main/utils/nvs_utils.h`
- Create: `main/utils/nvs_utils.c`
- Modify: `main/CMakeLists.txt`（SRCS 增加 `"utils/nvs_utils.c"`）

**Interfaces:**
- Consumes: `nvs.h`、`cJSON`（`main/CMakeLists.txt` 的 `PRIV_REQUIRES` 已含 `nvs_flash` 与 `cjson`）
- Produces:
  - `esp_err_t nvs_load_alloc(const char *ns, const char *key, char **out)`
  - `esp_err_t nvs_load_json(const char *ns, const char *key, cJSON **out)`
  - `esp_err_t nvs_save_json(const char *ns, const char *key, const cJSON *obj)`

**说明：** 这三个函数依赖 ESP-IDF，无法主机测试；由 Task 10 的迁移与设备回归测试覆盖。

- [ ] **Step 1: 写头文件 `main/utils/nvs_utils.h`**

```c
/**
 * @file nvs_utils.h
 * @brief NVS helpers for the "read a string blob" pattern
 *
 * Reading a string from NVS takes two calls to nvs_get_str() (one to learn the
 * length, one to fetch), a malloc, and an open/close pair. That sequence is
 * repeated for the node config, the device list and the factory payload.
 * These helpers state it once.
 */

#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <esp_err.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a string value from NVS, allocating exactly the needed size
 *
 * @param ns   Namespace
 * @param key  Key
 * @param out  Receives a malloc'd, NUL-terminated string. Caller frees with
 *             free(). Set to NULL on failure.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND (namespace or key absent),
 *         ESP_ERR_NO_MEM, or another nvs error
 */
esp_err_t nvs_load_alloc(const char *ns, const char *key, char **out);

/**
 * @brief Read and parse a JSON document stored in NVS
 *
 * @param out  Receives a cJSON tree. Caller owns it (cJSON_Delete). Set to
 *             NULL on failure.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE when the
 *         stored text is not valid JSON, or an allocation/NVS error
 */
esp_err_t nvs_load_json(const char *ns, const char *key, cJSON **out);

/**
 * @brief Serialise a cJSON tree to unformatted text and store it in NVS
 *
 * Writes and commits in one call.
 *
 * @return ESP_OK, ESP_ERR_NO_MEM, or an NVS error
 */
esp_err_t nvs_save_json(const char *ns, const char *key, const cJSON *obj);

#ifdef __cplusplus
}
#endif

#endif /* NVS_UTILS_H */
```

- [ ] **Step 2: 写实现 `main/utils/nvs_utils.c`**

```c
/**
 * @file nvs_utils.c
 * @brief Implementation of the NVS helpers (see nvs_utils.h)
 */

#include <stdlib.h>

#include <nvs.h>
#include <esp_log.h>
#include <cJSON.h>

#include "nvs_utils.h"

static const char *TAG = "nvs_utils";

esp_err_t nvs_load_alloc(const char *ns, const char *key, char **out)
{
    if (ns == NULL || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        /* ESP_ERR_NVS_NOT_FOUND simply means "never written" */
        return err;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, key, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return (err == ESP_OK) ? ESP_ERR_NVS_NOT_FOUND : err;
    }

    char *buf = malloc(len);
    if (buf == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, key, buf, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    *out = buf;
    return ESP_OK;
}

esp_err_t nvs_load_json(const char *ns, const char *key, cJSON **out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    char *str = NULL;
    esp_err_t err = nvs_load_alloc(ns, key, &str);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *json = cJSON_Parse(str);
    free(str);
    if (json == NULL) {
        ESP_LOGE(TAG, "'%s/%s' holds invalid JSON", ns, key);
        return ESP_ERR_INVALID_STATE;
    }

    *out = json;
    return ESP_OK;
}

esp_err_t nvs_save_json(const char *ns, const char *key, const cJSON *obj)
{
    if (ns == NULL || key == NULL || obj == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *str = cJSON_PrintUnformatted((cJSON *)obj);
    if (str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        free(str);
        return err;
    }

    err = nvs_set_str(nvs, key, str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    free(str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store '%s/%s': %s", ns, key, esp_err_to_name(err));
    }
    return err;
}
```

- [ ] **Step 3: 接入构建**

在 `main/CMakeLists.txt` 的 SRCS 中，`"utils/str_utils.c"` 下一行新增：

```cmake
    "utils/nvs_utils.c"
```

- [ ] **Step 4: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(utils): add NVS string/JSON load-save helpers"
```

---

## Task 7: 移动 `config/` 模块

纯文件移动 + include 路径调整。**不修改任何代码内容。**

**Files:**
- Move: `main/core/node_config.{c,h}` → `main/config/`
- Move: `main/core/config_apply.{c,h}` → `main/config/`
- Move: `main/core/yaml.{c,h}` → `main/config/`
- Modify: `main/CMakeLists.txt`（SRCS 路径 + include_dirs 增加 `"config"`）
- Modify: `tests/run_yaml_tests.sh`（2 处路径）

**Interfaces:**
- Consumes: 无
- Produces: 头文件仍在扁平 include 路径下以 `"node_config.h"`、`"config_apply.h"`、`"yaml.h"` 解析（全仓 include 均为无前缀形式）

- [ ] **Step 1: 移动文件（保留 git 历史）**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
mkdir -p main/config
git mv main/core/node_config.c   main/config/node_config.c
git mv main/core/node_config.h   main/config/node_config.h
git mv main/core/config_apply.c  main/config/config_apply.c
git mv main/core/config_apply.h  main/config/config_apply.h
git mv main/core/yaml.c          main/config/yaml.c
git mv main/core/yaml.h          main/config/yaml.h
```

- [ ] **Step 2: 更新 `main/CMakeLists.txt` 的 SRCS 路径**

```cmake
    # Core services
    "core/device_type.c"
    "core/event_bus.c"
    "core/node_config.c"       <-- 改为 "config/node_config.c"
    "core/device_manager.c"
    "core/yaml.c"              <-- 改为 "config/yaml.c"
    "core/config_apply.c"      <-- 改为 "config/config_apply.c"
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"
```

替换为：

```cmake
    # Core services
    "core/device_type.c"
    "core/event_bus.c"
    "core/device_manager.c"
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"

    # Configuration
    "config/node_config.c"
    "config/config_apply.c"
    "config/yaml.c"
```

- [ ] **Step 3: 增加 `"config"` include 目录（保留 `"core"` 供后两个 Task 过渡）**

```cmake
set(espx_include_dirs
    "."
    "utils"
    "config"        # <-- 新增
    "core"
    "peripherals"
    ...
)
```

- [ ] **Step 4: 更新 `tests/run_yaml_tests.sh` 路径**

把：

```bash
CFLAGS="-Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/main/core -I$CJSON"
```

改为：

```bash
CFLAGS="-Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/main/config -I$CJSON"
```

并把两处 `"$ROOT/main/core/yaml.c"` 改为 `"$ROOT/main/config/yaml.c"`。

- [ ] **Step 5: 主机测试通过**

```bash
tests/run_yaml_tests.sh
```

Expected: `ALL YAML TESTS PASSED`

- [ ] **Step 6: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(config): move node config, apply and YAML into config/"
```

---

## Task 8: 移动 `device/` 模块

纯文件移动 + include 路径调整。**不修改任何代码内容。**

**Files:**
- Move: `main/core/device_type.{c,h}` → `main/device/`
- Move: `main/core/device_manager.{c,h}` → `main/device/`
- Move: `main/core/event_bus.{c,h}` → `main/device/`
- Modify: `main/CMakeLists.txt`（SRCS 路径 + include_dirs 增加 `"device"`）

**Interfaces:**
- Consumes: 无
- Produces: 头文件以 `"device_type.h"`、`"device_manager.h"`、`"event_bus.h"` 解析

- [ ] **Step 1: 移动文件**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
mkdir -p main/device
git mv main/core/device_type.c     main/device/device_type.c
git mv main/core/device_type.h     main/device/device_type.h
git mv main/core/device_manager.c  main/device/device_manager.c
git mv main/core/device_manager.h  main/device/device_manager.h
git mv main/core/event_bus.c       main/device/event_bus.c
git mv main/core/event_bus.h       main/device/event_bus.h
```

- [ ] **Step 2: 更新 `main/CMakeLists.txt` 的 SRCS**

```cmake
    # Core services
    "core/device_type.c"      <-- 删
    "core/event_bus.c"        <-- 删
    "core/device_manager.c"   <-- 删
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"
```

替换为：

```cmake
    # Core services
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"

    # Device model
    "device/device_type.c"
    "device/device_manager.c"
    "device/event_bus.c"
```

- [ ] **Step 3: 增加 `"device"` include 目录**

```cmake
set(espx_include_dirs
    "."
    "utils"
    "config"
    "device"        # <-- 新增
    "core"
    ...
)
```

- [ ] **Step 4: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(device): move device type, manager and event bus into device/"
```

---

## Task 9: 移动 `common/` 模块、删除 `core/`、修正 `app_main.c`

纯文件移动 + 2 处 include 修正 + include_dirs 清理。

**Files:**
- Move: `main/core/task_util.h` → `main/common/`
- Move: `main/core/app_info.h` → `main/common/`  (header-only static-inline; preflight gap found during T8/T9 execution)
- Move: `main/core/defaults.{c,h}` → `main/common/`
- Move: `main/core/sys_stats.{c,h}` → `main/common/`
- Move: `main/core/sys_info.{c,h}` → `main/common/`
- Modify: `main/app_main.c:34-35`（去掉 `core/` 前缀）
- Modify: `main/CMakeLists.txt`（SRCS 路径 + 移除 `"core"` include 目录）
- Modify: `AGENT.md`（第 5 节分层图的模块路径）

**Interfaces:**
- Consumes: 无
- Produces: `"task_util.h"`、`"defaults.h"`、`"sys_stats.h"`、`"sys_info.h"` 解析；`core/` 目录消失

- [ ] **Step 1: 移动文件**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
mkdir -p main/common
git mv main/core/task_util.h   main/common/task_util.h
git mv main/core/defaults.c    main/common/defaults.c
git mv main/core/defaults.h    main/common/defaults.h
git mv main/core/sys_stats.c   main/common/sys_stats.c
git mv main/core/sys_stats.h   main/common/sys_stats.h
git mv main/core/sys_info.c    main/common/sys_info.c
git mv main/core/sys_info.h    main/common/sys_info.h
```

- [ ] **Step 2: 修正 `app_main.c` 的两处 `core/` 前缀 include**

```c
#include "core/sys_stats.h"     -->  #include "sys_stats.h"
#include "core/defaults.h"      -->  #include "defaults.h"
```

- [ ] **Step 3: 更新 `main/CMakeLists.txt` 的 SRCS**

```cmake
    # Core services
    "core/sys_stats.c"
    "core/sys_info.c"
    "core/defaults.c"
```

替换为：

```cmake
    # Common services
    "common/sys_stats.c"
    "common/sys_info.c"
    "common/defaults.c"
```

（`app_info.h` 是 header-only（`static inline`），**不需要** SRCS 条目，靠 `"common"` include 目录解析。）

- [ ] **Step 4: 移除 `"core"` include 目录**

```cmake
set(espx_include_dirs
    "."
    "utils"
    "config"
    "device"
    "common"
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

- [ ] **Step 5: 确认 `core/` 已空并删除**

```bash
ls -A main/core 2>/dev/null && echo "STILL POPULATED" || echo "core/ is empty"
rmdir main/core
```

Expected: `core/ is empty`

- [ ] **Step 6: 更新 `AGENT.md` 第 5 节分层图的模块路径**

把分层图注释里的 `device_type 目录` / `device_manager · node_config · event_bus` 行更新为明确的新位置：

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

并在该节「规则」列表里把 `yaml.c 只依赖 cJSON 与 libc` 一条中的路径补为 `config/yaml.c`。

**同时修正同文件 §6「工作方式」里的过时路径**（Task 7 的移动使这里失效）：

```
  * 主机单元测试：`gcc ... main/core/yaml.c` + `tests/yaml_test*.c`
```
改为：
```
  * 主机单元测试：`tests/run_yaml_tests.sh`（内部使用 `main/config/yaml.c`）
```

验证：
```bash
grep -n "main/core" AGENT.md   # 期望：无输出
```

- [ ] **Step 7: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 8: 断言目录结构**

```bash
ls main/ | sort
```

Expected: 含 `common  config  device  utils`，不含 `core` 与 `at_service`。

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor(common): move defaults, sys stats/info and task_util; drop core/"
```

---

## Task 10: 迁移 `config/` 与 `device/` 到 utils

消除重复：NVS 读取模式、`cJSON_IsString` 守卫、`strncpy(..., size-1)`。

**Files:**
- Modify: `main/config/node_config.c`
- Modify: `main/config/config_apply.c`
- Modify: `main/device/device_manager.c`

**Interfaces:**
- Consumes: `json_utils.h`（Task 4）、`str_utils.h`（Task 5）、`nvs_utils.h`（Task 6）
- Produces: 无新增对外接口；对外行为与 NVS 字面量必须逐字不变

> **重要（Review Focus #1）：** 本任务**不得**改动 `NVS_NAMESPACE` / `NVS_KEY_CONFIG` 宏的值。改名会让已部署设备的配置在 OTA 后静默丢失。

- [ ] **Step 1: 改造 `node_config.c` 的加载路径**

把 `node_config_load()` 中「nvs_open → 两次 nvs_get_str → malloc → cJSON_Parse」整段：

```c
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return ESP_OK;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, NVS_KEY_CONFIG, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return ESP_OK;
    }

    char *json_str = malloc(len);
    if (json_str == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, NVS_KEY_CONFIG, json_str, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(json_str);
        return err;
    }

    if (g_config != NULL) {
        cJSON_Delete(g_config);
    }
    g_config = cJSON_Parse(json_str);
    free(json_str);

    if (g_config != NULL) {
```

替换为：

```c
    if (g_config != NULL) {
        cJSON_Delete(g_config);
        g_config = NULL;
    }

    /* A missing namespace/key just means "never configured": not an error. */
    esp_err_t err = nvs_load_json(NVS_NAMESPACE, NVS_KEY_CONFIG, &g_config);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Corrupt stored document: fall back to defaults rather than abort. */
        ESP_LOGW(TAG, "stored node config is not valid JSON, using defaults");
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to load node config: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (g_config != NULL) {
```

在文件头部 include 区新增：

```c
#include "nvs_utils.h"
#include "json_utils.h"
#include "str_utils.h"
```

- [ ] **Step 2: 改造 `node_config.c` 的 `device_id` / `name` 读取与保存**

两处（`node_config_load()` 与 `node_config_set()`）的：

```c
            cJSON *id = cJSON_GetObjectItem(node, "device_id");
            cJSON *name = cJSON_GetObjectItem(node, "name");

            if (cJSON_IsString(id)) {
                strncpy(g_device_id, id->valuestring, sizeof(g_device_id) - 1);
            }
            if (cJSON_IsString(name)) {
                strncpy(g_name, name->valuestring, sizeof(g_name) - 1);
            }
```

替换为：

```c
            const char *id = json_get_string(node, "device_id", NULL);
            const char *name = json_get_string(node, "name", NULL);

            if (id != NULL) {
                str_copy(g_device_id, sizeof(g_device_id), id);
            }
            if (name != NULL) {
                str_copy(g_name, sizeof(g_name), name);
            }
```

`node_config_save()` 中把「cJSON_PrintUnformatted → nvs_open → nvs_set_str → nvs_commit → free」整段替换为：

```c
    esp_err_t err = nvs_save_json(NVS_NAMESPACE, NVS_KEY_CONFIG, g_config);
    ESP_LOGI(TAG, "node_config_save: result=%s", esp_err_to_name(err));
    return err;
```

（保留函数前面对 `node` 小节的 `cJSON_ReplaceItemInObject` 更新逻辑不变。）

- [ ] **Step 3: 改造 `device_manager.c` 的加载与保存**

`device_manager_load()` 中「nvs_open → 两次 nvs_get_str → malloc → cJSON_Parse」整段替换为：

```c
    cJSON *root = NULL;
    esp_err_t err = nvs_load_json(NVS_NAMESPACE, NVS_KEY_CONFIG, &root);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No devices to load");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load devices: %s", esp_err_to_name(err));
        return ESP_OK;
    }
```

`device_manager_save()` 中「cJSON_PrintUnformatted → nvs_open → nvs_set_str → nvs_commit → free」整段替换为：

```c
    esp_err_t err = nvs_save_json(NVS_NAMESPACE, NVS_KEY_CONFIG, root);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved %u peripheral(s) to NVS", (unsigned)g_device_count);
    }
    cJSON_Delete(root);
    return err;
```

并把 `device_add()` 里的：

```c
    strncpy(dev->id, id, sizeof(dev->id) - 1);
```

替换为：

```c
    str_copy(dev->id, sizeof(dev->id), id);
```

在文件头部新增：

```c
#include "nvs_utils.h"
#include "str_utils.h"
```

- [ ] **Step 4: 改造 `config_apply.c`**

把 `is_secret()` 的实现改为复用 `str_contains`：

```c
static bool is_secret(const char *key)
{
    return str_contains(key, "password") || str_contains(key, "key");
}
```

把 `config_apply()` 里 `replace_devices` 分支中收集 keep id 的部分：

```c
                cJSON *id = cJSON_GetObjectItem(entry, "id");
                if (cJSON_IsString(id) && keep_n < sizeof(keep) / sizeof(keep[0])) {
                    keep[keep_n++] = id->valuestring;
                }
```

替换为：

```c
                const char *id = json_get_string(entry, "id", NULL);
                if (id != NULL && keep_n < sizeof(keep) / sizeof(keep[0])) {
                    keep[keep_n++] = id;
                }
```

把 `upsert_device()` 顶部三处 `cJSON_GetObjectItem` + 类型判断改为 helpers：

```c
    const char *id = json_get_string(entry, "id", NULL);
    const char *type = json_get_string(entry, "type", NULL);
    cJSON *config = cJSON_GetObjectItem(entry, "config");
    cJSON *enabled = cJSON_GetObjectItem(entry, "enabled");

    if (id == NULL || id[0] == '\0') {
        snprintf(err, err_len, "device entry without an id");
        return ESP_ERR_INVALID_ARG;
    }
    if (type == NULL || type[0] == '\0') {
        snprintf(err, err_len, "device '%s' without a type", id);
        return ESP_ERR_INVALID_ARG;
    }

    bool exists = (device_get(id) != NULL);
```

后续所有 `id->valuestring` 改为 `id`、`type->valuestring` 改为 `type`。把 `device_remove` 前对 `dev->id` 的拷贝：

```c
            char id[sizeof(dev->id)];
            strncpy(id, dev->id, sizeof(id) - 1);
            id[sizeof(id) - 1] = '\0';
```

替换为：

```c
            char id[sizeof(dev->id)];
            str_copy(id, sizeof(id), dev->id);
```

在文件头部新增：

```c
#include "json_utils.h"
#include "str_utils.h"
```

- [ ] **Step 5: 断言 NVS 字面量未变（Review Focus #1）**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
grep -Fn 'NVS_NAMESPACE "espx_node"'        main/config/node_config.c
grep -Fn 'NVS_KEY_CONFIG "config"'          main/config/node_config.c
grep -Fn 'NVS_NAMESPACE "espx_devices"'     main/device/device_manager.c
grep -Fn 'NVS_KEY_CONFIG "config"'          main/device/device_manager.c
```

Expected: 4 条全部匹配到原始字面量。若有任何一条不匹配，**停止**并修正——这是配置丢失级故障。

- [ ] **Step 6: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 7: 设备回归（需要硬件；无硬件则记录为待验证）**

```bash
python3 tools/espx_test.py --port /dev/ttyUSB0 all
```

Expected: 全部通过。重点观察 `list` 能读回设备（证明 `device_manager_load()` 仍能解析 NVS）。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(config,device): use json/str/nvs utils, drop duplicated NVS I/O"
```

---

## Task 11: 迁移其余模块的 `strncpy` 到 `str_copy`

机械替换，逐文件进行。**仅替换 `strncpy` 调用，不改动其他逻辑。**

**Files:**（站点数为执行前实测值，共 **18 处 / 6 个文件**）
- Modify: `main/mqtt_client/espx_mqtt_client.c`（6 处）
- Modify: `main/mqtt_client/mqtt_commander.c`（1 处）
- Modify: `main/ota_service/ota_service.c`（4 处）
- Modify: `main/wifi_prov/wifi_prov.c`（4 处：`ssid` / `password` / `desired.sta.ssid` / `desired.sta.password`）
- Modify: `main/test_mode/test_mode.c`（1 处，第 526 行 `id`）
- Modify: `main/web_server/web_server.c`（2 处，`parse_device_uri()` 的 `id_out` / `action_out`；
  Task 13 会把这段代码迁到 `device_handler.c`，此处先迁移，迁走后代码即为已迁移版本）
- `main/mfg_provision/mfg_provision.c`：**无需改动**。它用 `nvs_open_from_partition()` 读自定义
  `MFG_PARTITION_NAME` 分区，**没有 `strncpy` 站点**，且无法套用 `nvs_load_alloc`（后者用普通
  `nvs_open`）。不要在此文件强行替换。

**Interfaces:**
- Consumes: `str_utils.h`（Task 5）。**不**消费 `nvs_utils.h`（见上：`mfg_provision.c` 用的是自定义分区 API）
- Produces: 无对外变更

**精确替换规则：** 所有匹配下列形态的语句

```c
strncpy(DEST, SRC, N - 1);
```

一律改为

```c
str_copy(DEST, N, SRC);
```

其中 `N` 是原来的缓冲区尺寸表达式（如 `sizeof(g_broker)`、`out_size`、`sizeof(s_status.error)`），**不加** `- 1`。

> 注意：若某处 `strncpy` 后紧跟一行 `DEST[N - 1] = '\0';`（手动补终止符），替换 `str_copy` 后该行**必须删除**，因为 `str_copy` 已保证终止。

- [ ] **Step 1: 定位全部站点**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
grep -rn "strncpy(" main/ --include="*.c" | grep -v managed_components
```

记录输出，逐条改写。

- [ ] **Step 2: 改写 `espx_mqtt_client.c`**

7 处，例如：

```c
            strncpy(g_broker, v->valuestring, sizeof(g_broker) - 1);
```
→
```c
            str_copy(g_broker, sizeof(g_broker), v->valuestring);
```

同法处理 `g_username`、`g_password`、`g_topic_prefix`，以及两处 `g_device_id`：

```c
    strncpy(g_device_id, node_config_get_device_id(), sizeof(g_device_id) - 1);
```
→
```c
    str_copy(g_device_id, sizeof(g_device_id), node_config_get_device_id());
```

`g_topic_prefix` 处原本紧随其后的显式终止符

```c
            strncpy(g_topic_prefix, v->valuestring, sizeof(g_topic_prefix) - 1);
            g_topic_prefix[sizeof(g_topic_prefix) - 1] = '\0';
```

一并改为单行 `str_copy(...)`，删除终止符行。

在文件头部新增 `#include "str_utils.h"`。

- [ ] **Step 3: 改写 `mqtt_commander.c`（第 68 行）**

```c
    strncpy(out, p, out_size - 1);
```
→
```c
    str_copy(out, out_size, p);
```

新增 `#include "str_utils.h"`。

- [ ] **Step 4: 改写 `ota_service.c`（4 处）**

```c
        strncpy(s_status.error, error, sizeof(s_status.error) - 1);
```
→
```c
        str_copy(s_status.error, sizeof(s_status.error), error);
```

`running_version`（2 处起始于第 201、413 行）与 `url` 同法处理。新增 `#include "str_utils.h"`。

- [ ] **Step 5: 改写 `wifi_prov.c`（2 处）**

```c
            strncpy(ssid, v->valuestring, ssid_len - 1);
            strncpy(password, v->valuestring, pass_len - 1);
```
→
```c
            str_copy(ssid, ssid_len, v->valuestring);
            str_copy(password, pass_len, v->valuestring);
```

新增 `#include "str_utils.h"`。

- [ ] **Step 6: `mfg_provision.c` —— 不改动（已核实）**

执行前已核实：该文件用 `nvs_open_from_partition(MFG_PARTITION_NAME, ...)` 读**自定义分区**，
且**没有任何 `strncpy` 站点**。因此本任务**不修改此文件**，也不套用 `nvs_load_alloc`
（后者基于普通 `nvs_open`，命名空间/分区语义不同）。

只用一条 grep 记录「确无站点」：

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
grep -n "strncpy(" main/mfg_provision/mfg_provision.c || echo "no strncpy in mfg_provision.c OK"
```

- [ ] **Step 6b: `web_server.c` —— 删除重复的本地 `mac_to_str`（否则 include 冲突）**

`web_server.c:688` 定义了一个本地 `static char* mac_to_str(uint8_t *mac)`（**返回 static 缓冲区，不可重入**），
与 `str_utils.h` 的 `void mac_to_str(const uint8_t *, char *, size_t)` 签名不同。
若只加 `#include "str_utils.h"` 会在 `-Werror` 下报
`static declaration of 'mac_to_str' follows non-static declaration`。

因此必须**删掉本地定义**（它本就是 utils 要消除的重复实现），并把唯一调用点
（`api_wifi_scan_handler`）改为使用 utils 版本：

```c
        char bssid[18];
        mac_to_str((const uint8_t *)&ap_info[i].bssid, bssid, sizeof(bssid));
        cJSON_AddStringToObject(net, "bssid", bssid);
```

替换原来的一行式 `cJSON_AddStringToObject(net, "bssid", mac_to_str((uint8_t *)&ap_info[i].bssid));`。
这样 `web_server.c` 就能安全地 `#include "str_utils.h"`，且顺带消除了一个不可重入的静态缓冲区。

验证：

```bash
grep -n "static char\* mac_to_str" main/web_server/web_server.c || echo "local mac_to_str removed OK"
```

> `web_server.c:50` 的本地 `static void json_set_string(...)` 与 `json_utils.h` 签名**相同**，
> 本任务**不动它**（Task 12/14 处理）；因此本任务 `web_server.c` **只** include `str_utils.h`，
> **不** include `json_utils.h`。

- [ ] **Step 7: 断言无遗留**

```bash
grep -rn "strncpy(" main/ --include="*.c" | grep -v managed_components
```

Expected: 无输出。

- [ ] **Step 8: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor: replace strncpy(size-1) idiom with str_copy"
```

---

## Task 12: 拆分 web server — 共享响应助手 + node/network/config handlers

**Files:**
- Create: `main/web_server/handlers/handlers.h`
- Create: `main/web_server/handlers/handlers_common.c`
- Create: `main/web_server/handlers/node_handler.c`
- Create: `main/web_server/handlers/network_handler.c`
- Create: `main/web_server/handlers/config_handler.c`
- Modify: `main/web_server/web_server.c`（删除 4 个已迁出 handler 及其 6 条 URI 条目；`send_json`/`send_error` 改为转发）
- Modify: `main/CMakeLists.txt`（SRCS 增加 4 个新文件）

**Interfaces:**
- Consumes: `json_utils.h`、`node_config.h`、`config_apply.h`、`device_manager.h`、`app_info.h`
- Produces:
  - `esp_err_t api_send_json(httpd_req_t *req, cJSON *json, int status)`（接管 `json` 所有权，负责 `cJSON_Delete`）
  - `esp_err_t api_send_error(httpd_req_t *req, const char *msg, int status)`
  - `esp_err_t node_handler_register(httpd_handle_t server)`
  - `esp_err_t network_handler_register(httpd_handle_t server)`
  - `esp_err_t config_handler_register(httpd_handle_t server)`

### 机械转换规则（Task 12/13/14 通用）

从 `web_server.c` 迁出每个 handler 时，逐字照搬函数体，**只**做以下替换：

| 原符号 | 新符号 |
|---|---|
| `static esp_err_t api_xxx_handler(` | 保持 `static`（迁入模块内部）|
| `send_json(req, json, status)` | `api_send_json(req, json, status)` |
| `send_error(req, msg, status)` | `api_send_error(req, msg, status)` |
| 本地 `json_set_string(...)` | `json_utils.h` 的 `json_set_string(...)`（签名相同）|
| 本地 `mac_to_str(mac)` | `str_utils.h` 的 `mac_to_str(mac, buf, sizeof(buf))`（签名不同，需补缓冲区）|

**过渡期约束（避免构建中断）：** 在 Task 14 把 wifi handler 也迁走之前，
`web_server.c` 仍会用到 `send_json` / `send_error` / `json_set_string` / `mac_to_str`。
因此 Task 12 **只把 `send_json`/`send_error` 改为对 `api_send_json`/`api_send_error`
的一行转发，并保留本地 static 的 `json_set_string`**（`mac_to_str` 已在 Task 11 Step 6b 删除）；
四者统一在 Task 14 Step 6 删除。

- [ ] **Step 1: 创建 `handlers/handlers.h`**

```c
/**
 * @file handlers.h
 * @brief Shared helpers and per-module registration for the HTTPS API
 *
 * web_server.c owns the server lifecycle; each handler module owns its own
 * URI table and registers itself. Registration ORDER matters: the device
 * module registers the /api/peripherals wildcard routes and must therefore
 * be registered last, or it swallows the exact paths.
 */

#ifndef HANDLERS_H
#define HANDLERS_H

#include <esp_err.h>
#include <esp_http_server.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send a JSON document as the response body
 *
 * Takes ownership of @p json and deletes it, on success and on failure.
 *
 * @param status HTTP status: 200, 400, 404 or 500 (anything else becomes 500)
 */
esp_err_t api_send_json(httpd_req_t *req, cJSON *json, int status);

/**
 * @brief Send {"error": msg} with the given status
 */
esp_err_t api_send_error(httpd_req_t *req, const char *msg, int status);

/* Registration order in web_server_start():
 *   root → node → network → config → system → ota → cert → wifi → device(last)
 */
esp_err_t node_handler_register(httpd_handle_t server);
esp_err_t network_handler_register(httpd_handle_t server);
esp_err_t config_handler_register(httpd_handle_t server);
esp_err_t device_handler_register(httpd_handle_t server);
esp_err_t system_handler_register(httpd_handle_t server);
esp_err_t ota_handler_register(httpd_handle_t server);
esp_err_t cert_handler_register(httpd_handle_t server);
esp_err_t wifi_handler_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* HANDLERS_H */
```

- [ ] **Step 2: 创建 `handlers/handlers_common.c`**

```c
/**
 * @file handlers_common.c
 * @brief Shared HTTP response helpers (see handlers.h)
 */

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"

static const char *TAG = "http_api";

static const char *status_text(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    default:  return "500 Internal Server Error";
    }
}

esp_err_t api_send_json(httpd_req_t *req, cJSON *json, int status)
{
    if (req == NULL) {
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }

    char *str = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);

    if (str == NULL) {
        httpd_resp_set_status(req, status_text(500));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"out of memory\"}", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, str, strlen(str));
    free(str);
    return err;
}

esp_err_t api_send_error(httpd_req_t *req, const char *msg, int status)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return api_send_json(req, NULL, 500);
    }
    cJSON_AddStringToObject(json, "error", msg ? msg : "error");
    return api_send_json(req, json, status);
}
```

- [ ] **Step 3: 创建 `handlers/node_handler.c`**

从 `web_server.c` 迁入 `api_node_handler()`，改为 `static`，并新增注册函数：

```c
/**
 * @file node_handler.c
 * @brief GET/PUT /api/node — node identity
 */

#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "node_config.h"
#include "app_info.h"
#include "config_apply.h"
#include "device_manager.h"

/* api_node_handler() moves here verbatim from web_server.c, with two changes:
 *   - send_json(...)      -> api_send_json(...)
 *   - send_error(...)     -> api_send_error(...)
 *   - json_set_string(..) -> json_set_string(..) from json_utils.h
 */
static esp_err_t api_node_handler(httpd_req_t *req)
{
    if (req->method == HTTP_PUT) {
        char buf[256];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *node = cJSON_GetObjectItem(cfg, "node");
        if (!cJSON_IsObject(node)) {
            node = cJSON_AddObjectToObject(cfg, "node");
        }

        const char *name = json_get_string(incoming, "name", NULL);
        if (name != NULL && name[0] != '\0') {
            json_set_string(node, "name", name);
        }
        /* device_id is set at the factory and cannot change at runtime. */

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    cJSON *json = cJSON_CreateObject();
    {
        const char *id = node_config_get_device_id();
        if (strncmp(id, "espx-", 5) == 0) {
            id += 5;
        }
        cJSON_AddStringToObject(json, "device_id", id);
    }
    cJSON_AddStringToObject(json, "name", node_config_get_name());
    cJSON_AddStringToObject(json, "version", app_version());
    cJSON_AddNumberToObject(json, "peripheral_count", device_get_count());

    return api_send_json(req, json, 200);
}

esp_err_t node_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/node", .method = HTTP_GET, .handler = api_node_handler },
        { .uri = "/api/node", .method = HTTP_PUT, .handler = api_node_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 4: 创建 `handlers/network_handler.c`（完整内容）**

```c
/**
 * @file network_handler.c
 * @brief GET/PUT /api/network — MQTT / network settings
 */

#include <string.h>

#include <esp_log.h>
#include <cJSON.h>

#include "handlers.h"
#include "json_utils.h"
#include "str_utils.h"
#include "node_config.h"

static const char *TAG = "http_network";

static esp_err_t api_network_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        cJSON *cfg = node_config_get();
        cJSON *net = cfg ? cJSON_GetObjectItem(cfg, "network") : NULL;
        cJSON *out = net ? cJSON_Duplicate(net, true) : cJSON_CreateObject();
        if (cfg) cJSON_Delete(cfg);
        return api_send_json(req, out, 200);
    }

    if (req->method == HTTP_PUT) {
        char buf[512];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return api_send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return api_send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *net = cJSON_GetObjectItem(cfg, "network");
        if (!cJSON_IsObject(net)) {
            net = cJSON_AddObjectToObject(cfg, "network");
        }

        /* Walk the incoming members: strings, numbers and bools are all
         * accepted (the network section holds ports and flags too). */
        cJSON *item = incoming->child;
        while (item != NULL) {
            if (cJSON_IsString(item)) {
                json_set_string(net, item->string, item->valuestring);
            } else if (cJSON_IsNumber(item) || cJSON_IsBool(item)) {
                cJSON *dup = cJSON_Duplicate(item, true);
                cJSON_DeleteItemFromObject(net, item->string);
                cJSON_AddItemToObject(net, item->string, dup);
            }
            /* Never log secrets */
            if (str_contains(item->string, "password")) {
                ESP_LOGI(TAG, "Network config: %s = <set>", item->string);
            } else {
                ESP_LOGI(TAG, "Network config: %s = %s", item->string,
                         cJSON_IsString(item) ? item->valuestring : "<non-string>");
            }
            item = item->next;
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return api_send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return api_send_error(req, "Method not allowed", 400);
}

esp_err_t network_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/network", .method = HTTP_GET, .handler = api_network_handler },
        { .uri = "/api/network", .method = HTTP_PUT, .handler = api_network_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 5: 创建 `handlers/config_handler.c`（完整内容）**

```c
/**
 * @file config_handler.c
 * @brief GET/POST /api/config — export and apply a configuration document
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "handlers.h"
#include "config_apply.h"

/**
 * @brief POST /api/config - apply a YAML or JSON configuration document
 *
 * Body: the document itself. Same semantics as MQTT <prefix>/cmd/config.
 */
static esp_err_t api_config_apply_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        return api_send_error(req, "missing or oversized configuration body", 400);
    }

    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        return api_send_error(req, "out of memory", 500);
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return api_send_error(req, "failed to read body", 400);
        }
        received += r;
    }
    buf[received] = '\0';

    config_apply_result_t res;
    char err[128] = {0};
    esp_err_t rc = config_apply_payload(buf, &res, err, sizeof(err));
    free(buf);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", rc == ESP_OK && res.devices_failed == 0);

    cJSON *counts = cJSON_AddObjectToObject(out, "applied");
    cJSON_AddNumberToObject(counts, "added", res.devices_added);
    cJSON_AddNumberToObject(counts, "updated", res.devices_updated);
    cJSON_AddNumberToObject(counts, "removed", res.devices_removed);
    cJSON_AddNumberToObject(counts, "failed", res.devices_failed);

    cJSON_AddBoolToObject(out, "reboot_required", res.reboot_recommended);
    const char *msg = res.error[0] ? res.error : err;
    if (msg[0]) cJSON_AddStringToObject(out, "error", msg);

    return api_send_json(req, out, rc == ESP_OK ? 200 : 400);
}

/**
 * @brief GET /api/config - current configuration as JSON
 */
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *cfg = config_export();
    if (cfg == NULL) {
        return api_send_error(req, "failed to export configuration", 500);
    }
    return api_send_json(req, cfg, 200);
}

esp_err_t config_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/config", .method = HTTP_GET,  .handler = api_config_get_handler },
        { .uri = "/api/config", .method = HTTP_POST, .handler = api_config_apply_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 6: 改写 `web_server.c` 的注册与共享助手（保持可编译）**

在 `web_server.c` 中：

1. 删除已迁出的 `api_node_handler`、`api_network_handler`、`api_config_apply_handler`、`api_config_get_handler`（4 个函数）。
2. 新增 `#include "handlers/handlers.h"`。
3. 把本地 `send_json()` / `send_error()` 的**函数体**换为对共享助手的转发（函数名保留，因为尚未迁出的 device/system/ota/cert/wifi handler 仍在调用它们）：

```c
static esp_err_t send_json(httpd_req_t *req, cJSON *json, int status)
{
    return api_send_json(req, json, status);
}

static esp_err_t send_error(httpd_req_t *req, const char *msg, int status)
{
    return api_send_error(req, msg, status);
}
```

4. **保留**本地 `static json_set_string()`（`api_wifi_config_handler` 仍在使用）。它与 `json_utils.h` 签名相同，所以本文件**不** include `json_utils.h`（否则 `-Werror` 报 `static declaration follows non-static`）。
   `static mac_to_str()` 已在 Task 11 Step 6b 删除，本任务不再保留。
   `json_set_string` 连同 `send_json`/`send_error` 一起在 Task 14 Step 6 删除。
   本文件的 `#include "str_utils.h"` 由 Task 11 Step 6b 加入，Task 12 保留它。
5. **保留** `root_handler()` 与 `_binary_index_html_start/end` 符号。
6. **保留原有的 `uris[]` 表与注册循环**（不要删除这个循环），只从表里删掉已迁出的 6 条；
   然后在循环**之后**追加已迁出模块的注册调用。这样未迁出的 handler
   （`/`、device、system、ota、cert、wifi）仍由原循环注册，已迁出的 3 个模块由新调用注册。

   a) 从 `uris[]` 表中**删除**这 6 条（只是从表里删，循环本身保留）：

   ```c
        { .uri = "/api/node",        .method = HTTP_GET,  .handler = api_node_handler },
        { .uri = "/api/node",        .method = HTTP_PUT,  .handler = api_node_handler },
        { .uri = "/api/network",     .method = HTTP_GET,  .handler = api_network_handler },
        { .uri = "/api/network",     .method = HTTP_PUT,  .handler = api_network_handler },
        { .uri = "/api/config",      .method = HTTP_GET,  .handler = api_config_get_handler },
        { .uri = "/api/config",      .method = HTTP_POST, .handler = api_config_apply_handler },
   ```

   `uris[]` 中**保留** `/`、`/api/peripherals*`、`/api/peripheral/options`、
   `/api/system/*`、`/api/ota/*`、`/api/certs/info`、`/api/wifi/*`
   （这些 handler 本任务不迁出）。

   b) 在原有的 for 注册循环**之后**追加：

   ```c
    /* Migrated modules register themselves. The /api/peripherals wildcards
     * move to device_handler_register() in Task 13, which must run last. */
    node_handler_register(g_server);
    network_handler_register(g_server);
    config_handler_register(g_server);
   ```

   > 本步骤结束时，`uris[]` 循环里**不得**再有 `/api/node`、`/api/network`、`/api/config`
   > 任一条目（否则与新的 register 调用重复注册，`httpd_register_uri_handler` 返回
   > `ESP_ERR_HTTPD_HANDLER_EXISTS`）。

编译并验证 `EMBED_FILES` 符号仍链接：

```bash
idf.py build 2>&1 | tail -10
nm build/espx.elf | grep _binary_index_html_start
```

Expected: 构建成功；`nm` 输出包含 `_binary_index_html_start`。

- [ ] **Step 7: 冒烟测试（有硬件时）**

```bash
curl -k https://<device-ip>/api/node
curl -k https://<device-ip>/api/network
curl -k https://<device-ip>/api/config
curl -k https://<device-ip>/ | head -3
```

Expected: 前三个返回 JSON；`/` 返回 HTML。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(web): extract shared response helpers and node/network/config handlers"
```

---

## Task 13: 拆分 web server — device handlers（含通配分发器）

**Files:**
- Create: `main/web_server/handlers/device_handler.c`
- Modify: `main/web_server/web_server.c`（删除已迁出的设备 handler 与 URI 条目；补上 `device_handler_register()` 调用）
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `handlers.h` 的 `api_send_json` / `api_send_error`；`device_manager.h`、`device_type.h`、`json_utils.h`
- Produces: `esp_err_t device_handler_register(httpd_handle_t server)`，注册并**必须最后调用**

- [ ] **Step 1: 创建 `handlers/device_handler.c`**

迁入以下函数，全部改为 `static`，`send_json`/`send_error` → `api_send_json`/`api_send_error`：

- `api_devices_list_handler()` — `GET /api/peripherals`
- `api_device_add_handler()` — `POST /api/peripherals`
- `api_devices_reload_handler()` — `POST /api/peripherals/reload`
- `api_device_types_handler()` — `GET /api/peripheral/options`
- `api_device_dispatch_handler()` — `/api/peripherals/{id}[/{action}]`（含 `parse_device_uri()`）

`device_get_json_array()` 已在 `device_manager` 中，无需迁入。

新增注册函数（**精确路径在前，通配在后**）：

```c
esp_err_t device_handler_register(httpd_handle_t server)
{
    /* ORDER: exact paths must be registered before the wildcard patterns.
     * With httpd_uri_match_wildcard the first match wins, so a wildcard
     * registered first would swallow the /api/peripherals/reload route. */
    static const httpd_uri_t uris[] = {
        { .uri = "/api/peripherals",          .method = HTTP_GET,    .handler = api_devices_list_handler },
        { .uri = "/api/peripherals",          .method = HTTP_POST,   .handler = api_device_add_handler },
        { .uri = "/api/peripherals/reload",   .method = HTTP_POST,   .handler = api_devices_reload_handler },
        { .uri = "/api/peripheral/options",   .method = HTTP_GET,    .handler = api_device_types_handler },
        /* wildcards last */
        { .uri = "/api/peripherals/*",        .method = HTTP_GET,    .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",        .method = HTTP_POST,   .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",        .method = HTTP_DELETE, .handler = api_device_dispatch_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 2: 在 `web_server.c` 中补上最后调用，并删除已迁出的设备 URI 条目**

a) 在 Task 12 Step 6 的注册块末尾追加：

```c
    /* LAST: owns the /api/peripherals wildcard routes */
    device_handler_register(g_server);
```

b) **必须同样执行：** 从 `web_server.c` 残留的旧 `uris[]` 表中，删除本任务已迁出的 7 条设备条目，否则这些路径会被注册两次（`device_handler_register()` 返回 `ESP_ERR_HTTPD_HANDLER_EXISTS`，行为不确定）：

```c
        { .uri = "/api/peripherals",          .method = HTTP_GET,    ... }
        { .uri = "/api/peripherals",          .method = HTTP_POST,   ... }
        { .uri = "/api/peripherals/reload",   .method = HTTP_POST,   ... }
        { .uri = "/api/peripheral/options",   .method = HTTP_GET,    ... }
        { .uri = "/api/peripherals/*",        .method = HTTP_GET,    ... }
        { .uri = "/api/peripherals/*",        .method = HTTP_POST,   ... }
        { .uri = "/api/peripherals/*",        .method = HTTP_DELETE, ... }
```

保留 system / ota / cert / wifi 的条目（它们在 Task 14 才迁出）。
验证无重复注册：

```bash
grep -n 'api/peripherals' main/web_server/web_server.c   # 期望：无输出
```

- [ ] **Step 3: 编译验证**

```bash
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`，0 warning。

- [ ] **Step 4: 确认通配顺序正确（Review Focus #4）**

对照 `device_handler.c` 中的 `uris[]` 字面顺序，断言 `/api/peripherals/reload` 出现在 `/api/peripherals/*` 之前：

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
awk '/httpd_uri_t uris\[\]/,/};/' main/web_server/handlers/device_handler.c \
  | grep -n 'uri = "/api/peripherals'
```

Expected: 输出按此顺序——`/api/peripherals`（GET）、`/api/peripherals`（POST）、`/api/peripherals/reload`、然后才是 `/api/peripherals/*`。

- [ ] **Step 5: 冒烟测试（有硬件时）**

```bash
curl -k https://<ip>/api/peripherals
curl -k -X POST https://<ip>/api/peripherals/reload
curl -k https://<ip>/api/peripherals/relay_a
curl -k -X POST -d 'true' https://<ip>/api/peripherals/relay_a/write
curl -k https://<ip>/api/peripheral/options
```

Expected: 全部返回 JSON，无 `404 Nothing matches the given URI`。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(web): extract device handlers with wildcard-last registration"
```

---

## Task 14: 拆分 web server — system/ota/cert/wifi handlers

**Files:**
- Create: `main/web_server/handlers/system_handler.c`
- Create: `main/web_server/handlers/ota_handler.c`
- Create: `main/web_server/handlers/cert_handler.c`
- Create: `main/web_server/handlers/wifi_handler.c`
- Modify: `main/web_server/web_server.c`（删除已迁出 handler 与 URI 条目；补 4 个注册调用）
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `handlers.h`、`sys_info.h`、`ota_service.h`、`cert_manager.h`、`test_mode.h`、`json_utils.h`、`str_utils.h`
- Produces: `system_handler_register`、`ota_handler_register`、`cert_handler_register`、`wifi_handler_register`

- [ ] **Step 1: 创建 `handlers/system_handler.c`**

迁入 `api_system_info_handler()`、`api_system_reboot_handler()`、`api_system_testmode_handler()`，改用 `api_send_json` / `api_send_error`。新增：

```c
esp_err_t system_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/system/info",     .method = HTTP_GET,  .handler = api_system_info_handler },
        { .uri = "/api/system/reboot",   .method = HTTP_POST, .handler = api_system_reboot_handler },
        { .uri = "/api/system/testmode", .method = HTTP_POST, .handler = api_system_testmode_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 2: 创建 `handlers/ota_handler.c`**

迁入 `api_ota_status_handler()`、`api_ota_start_handler()`、`api_ota_cancel_handler()`。把 OTA 状态名数组提取为文件级常量，避免与其它文件重复定义：

```c
static const char *OTA_STATE_NAMES[] = {
    "IDLE", "CONNECTING", "DOWNLOADING", "VERIFYING",
    "APPLYING", "REBOOTING", "SUCCESS", "FAILED"
};
```

新增：

```c
esp_err_t ota_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/ota/status", .method = HTTP_GET,  .handler = api_ota_status_handler },
        { .uri = "/api/ota/start",  .method = HTTP_POST, .handler = api_ota_start_handler },
        { .uri = "/api/ota/cancel", .method = HTTP_POST, .handler = api_ota_cancel_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 3: 创建 `handlers/cert_handler.c`**

迁入 `api_certs_info_handler()`。新增：

```c
esp_err_t cert_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/certs/info", .method = HTTP_GET, .handler = api_certs_info_handler },
    };

    esp_err_t err = httpd_register_uri_handler(server, &uris[0]);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}
```

- [ ] **Step 4: 创建 `handlers/wifi_handler.c`**

迁入 `api_wifi_scan_handler()`、`api_wifi_config_handler()`、`wifi_auth_mode_str()`、`mac_to_str()` 的**调用点**（函数本身改用 `str_utils.h` 的 `mac_to_str`，删除本地定义）。新增：

```c
esp_err_t wifi_handler_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/wifi/scan",   .method = HTTP_POST, .handler = api_wifi_scan_handler },
        { .uri = "/api/wifi/config", .method = HTTP_PUT,  .handler = api_wifi_config_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

`api_wifi_config_handler()` 中清空密码的分支必须继续使用 `json_set_string(net, "wifi_password", NULL)`（依赖 Review Focus #3 的删除语义）。`api_wifi_scan_handler()` 中 `mac_to_str((uint8_t *)&ap_info[i].bssid)` 的第三个参数需补缓冲区尺寸：

```c
        char bssid[18];
        mac_to_str((const uint8_t *)&ap_info[i].bssid, bssid, sizeof(bssid));
        cJSON_AddStringToObject(net, "bssid", bssid);
```

- [ ] **Step 5: 在 `web_server.c` 补全注册顺序**

在 Task 12 Step 6 的注册块末尾，依次追加未迁出模块的注册调用：

```c
    /* Exact paths first. */
    node_handler_register(g_server);
    network_handler_register(g_server);
    config_handler_register(g_server);
    system_handler_register(g_server);
    ota_handler_register(g_server);
    cert_handler_register(g_server);
    wifi_handler_register(g_server);

    /* LAST: owns the /api/peripherals wildcard routes */
    device_handler_register(g_server);
```

- [ ] **Step 6: 清空 `web_server.c` 的遗留助手与旧 URI 表**

此时所有 handler 都已迁出，`web_server.c` 不再引用本地的
`send_json` / `send_error` / `json_set_string` / `mac_to_str`。把它们全部删除，
并删除原 `uris[]` 表中剩余的所有条目（注册已由各 `*_register()` 承担）。

确认只剩：文件头注释、`TAG`、`g_server`、`root_handler()`、`web_server_start()`、`web_server_stop()`、`web_server_is_running()`。

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
grep -c "static esp_err_t api_" main/web_server/web_server.c
wc -l main/web_server/web_server.c
```

Expected: `grep` 输出 `0`；行数显著小于拆分前的 921 行。

- [ ] **Step 7: 编译 + 全端点冒烟**

```bash
idf.py build 2>&1 | tail -10
for p in / /api/node /api/network /api/config /api/peripherals /api/peripheral/options \
         /api/system/info /api/ota/status /api/certs/info; do
  echo "== $p"; curl -k -s -o /dev/null -w '%{http_code}\n' "https://<device-ip>$p"
done
curl -k -s -o /dev/null -w 'reload=%{http_code}\n' -X POST https://<device-ip>/api/peripherals/reload
curl -k -s -o /dev/null -w 'wifi-scan=%{http_code}\n' -X POST https://<device-ip>/api/wifi/scan
```

Expected: 所有端点返回 200（或 `reload`/`wifi-scan` 的业务码），无 404。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(web): extract system, ota, cert and wifi handlers"
```

---

## Task 15: 更新架构文档以反映新结构

**Files:**
- Modify: `docs/ARCHITECTURE.md`（目录树与依赖图）
- Modify: `docs/DEVELOPMENT.md`（目录树、新增模块、构建与测试命令）
- Modify: `docs/TESTING.md`（主机测试清单增加 utils 测试）

**Interfaces:**
- Consumes: 无
- Produces: 无（文档一致性）

- [ ] **Step 1: 更新 `docs/ARCHITECTURE.md` 的目录树**

把 `main/core/` 的单一条目替换为新结构：

```
├── utils/                      json/str/nvs helpers
├── config/                     node config, config_apply, YAML parser
├── device/                     device type registry, manager, event bus
├── common/                     defaults, sys stats/info, task_util
├── web_server/
│   └── handlers/               one module per API area
```

删除 `at_service/` 条目（Task 3 已删，此处确认无遗漏）。

- [ ] **Step 2: 更新依赖图**

按 AGENT.md 第 5 节（Task 9 已更新）同步绘制：

```
config_apply → yaml | node_config | device_manager | event_bus
device_manager → device_type | event_bus | node_config
peripherals/* → device_type | device_manager
utils/* → (libc, cJSON, NVS)    # 不反向依赖任何业务模块
```

- [ ] **Step 3: 更新 `docs/DEVELOPMENT.md`**

- 目录树替换为新结构（同 Step 1）。
- 「新增驱动必须」步骤保持；新增一节「新增 API 端点」：在 `main/web_server/handlers/<area>_handler.c` 添加 handler，并在同文件的 `*_register()` 中注册；通配路由必须最后注册。
- **修正 Task 7 移动后遗留的过时路径**（约第 327 行）：「再改 `main/core/yaml.c`」→「再改 `main/config/yaml.c`」。
- 构建/测试命令区加入：

```bash
# 主机单元测试
gcc -Wall -Wextra -Werror -Imain/utils -Imanaged_components/espressif__cjson/cJSON \
    tests/json_utils_test.c main/utils/json_utils.c \
    managed_components/espressif__cjson/cJSON/cJSON.c -o /tmp/jt -lm && /tmp/jt
gcc -Wall -Wextra -Werror -Imain/utils \
    tests/str_utils_test.c main/utils/str_utils.c -o /tmp/st && /tmp/st
tests/run_yaml_tests.sh
```

- [ ] **Step 4: 更新 `docs/TESTING.md`**

在主机测试清单中新增 `tests/json_utils_test.c`、`tests/str_utils_test.c` 及其 gcc 命令；删除 Task 3 可能遗漏的 AT 条目。

- [ ] **Step 5: 断言文档与代码一致**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
grep -rn "main/core/\|core/yaml\|core/device" docs/*.md AGENT.md
```

Expected: 无输出。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "docs: describe the utils/config/device/common structure"
```

---

## Task 16: 内存基准测量与针对性优化

**Files:**
- Modify: `main/common/sys_info.c`（补充内部 RAM / PSRAM 分项与任务栈高水位）
- Create: `tools/sysinfo_baseline.py`
- Modify: `docs/ARCHITECTURE.md`（记录基线数据与结论）

**Interfaces:**
- Consumes: `esp_heap_caps.h`、`uxTaskGetStackHighWaterMark`
- Produces: `/api/system/info` 新增字段：
  - `ram.iram_free`、`ram.iram_min`、`ram.iram_total`
  - `ram.psram_free`、`ram.psram_total`
  - `tasks[]`：`{name, stack_free_bytes, priority}`

- [ ] **Step 1: 扩展 `sys_info_add()`**

在 `main/common/sys_info.c` 中，于已有 `sys_info_add()` 内追加（保持既有字段不变）：

```c
    /* Internal RAM is the pool that runs out (Wi-Fi, TLS, PSA crypto);
     * the headline free-heap figure includes PSRAM and hides it. Report both
     * separately so exhaustion is visible before it fails an allocation. */
    cJSON *ram = cJSON_AddObjectToObject(json, "ram");
    cJSON_AddNumberToObject(ram, "iram_free",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(ram, "iram_min",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(ram, "iram_total",
                            heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
#if CONFIG_SPIRAM
    cJSON_AddNumberToObject(ram, "psram_free",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(ram, "psram_total",
                            heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
#endif

    /* Per-task stack headroom. A task below ~1 KB free is about to overflow;
     * this is how a too-small stack shows up before it crashes. */
    cJSON *tasks = cJSON_AddArrayToObject(json, "tasks");
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *status = malloc(n * sizeof(TaskStatus_t));
    if (status != NULL) {
        n = uxTaskGetSystemState(status, n, NULL);
        for (UBaseType_t i = 0; i < n; i++) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "name", status[i].pcTaskName);
            /* High-water mark is in words on ESP-IDF's portmacro. */
            cJSON_AddNumberToObject(t, "stack_free_bytes",
                                    (double)status[i].usStackHighWaterMark *
                                        sizeof(StackType_t));
            cJSON_AddNumberToObject(t, "priority", status[i].uxCurrentPriority);
            cJSON_AddItemToArray(tasks, t);
        }
        free(status);
    }
```

补充 include：`<stdlib.h>`、`<freertos/FreeRTOS.h>`、`<freertos/task.h>`、`<esp_heap_caps.h>`。

- [ ] **Step 2: 创建 `tools/sysinfo_baseline.py`**

```python
#!/usr/bin/env python3
"""Sample /api/system/info and print internal-RAM / PSRAM / stack headroom.

Used to compare a refactor against a baseline. Usage:
    tools/sysinfo_baseline.py --host 192.168.1.50 --samples 10
"""

import argparse
import json
import ssl
import sys
import time
import urllib.request


def fetch(host):
    ctx = ssl._create_unverified_context()  # self-signed dev certificate
    with urllib.request.urlopen(f"https://{host}/api/system/info",
                                context=ctx, timeout=5) as r:
        return json.load(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--samples", type=int, default=5)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    rows = []
    for i in range(args.samples):
        try:
            rows.append(fetch(args.host))
        except Exception as exc:                      # noqa: BLE001
            sys.exit(f"sample {i} failed: {exc}")
        time.sleep(1)

    first = rows[0]
    ram = first.get("ram", {})
    print(f"iram_free  {ram.get('iram_free')}")
    print(f"iram_min   {ram.get('iram_min')}   (lowest seen - leak indicator)")
    print(f"iram_total {ram.get('iram_total')}")
    print(f"psram_free {ram.get('psram_free')}")
    print(f"psram_total{ram.get('psram_total')}")
    print("\ntasks (stack_free_bytes):")
    for t in sorted(first.get("tasks", []),
                    key=lambda x: x.get("stack_free_bytes", 0)):
        print(f"  {t.get('name'):<16} {t.get('stack_free_bytes')}")

    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump(rows, fh, indent=2)
        print(f"\nwrote {args.json_out}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: 编译并采集基线**

```bash
idf.py build 2>&1 | tail -5
# 烧录后：
python3 tools/sysinfo_baseline.py --host <device-ip> --samples 10 --json-out /tmp/espx-baseline.json
```

- [ ] **Step 4: 依据实测结果优化**

按数据决定，**不做无依据的改动**：

| 观察 | 处理 |
|---|---|
| 某任务 `stack_free_bytes` < 1024 | 增大该任务栈（`espx_task_create(..., stack, ...)`）或在对应 Task 中登记 |
| `iram_min` 随采样持续下降 | 存在泄漏；定位该模块的 `malloc`/`cJSON_Create*` 配对 |
| `psram_free` 使用率 < 30% | 把可移动的大缓冲区迁到 PSRAM（参照 `task_util.h` 的规则，**不得**迁移会写 flash 的任务） |
| 全部指标健康 | 不做改动，仅把基线数据记入文档 |

- [ ] **Step 5: 记录结论**

在 `docs/ARCHITECTURE.md` 的「内存预算」小节追加实测表格（设备型号、固件版本、`iram_free`/`iram_min`/`psram_free`、结论）。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(observability): report internal RAM/PSRAM split and task stack headroom"
```

---

## Task 17: 最终验收

**Files:**
- 无（验证任务）

**Interfaces:**
- Consumes: 全部前序任务
- Produces: 验收结论

- [ ] **Step 1: 全量构建**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
rm -rf build sdkconfig
idf.py reconfigure
idf.py build 2>&1 | tee /tmp/espx-build.log | tail -20
grep -ci "warning" /tmp/espx-build.log || true
```

Expected: `Project build complete`；warning 计数为 0。

- [ ] **Step 2: 主机测试三件套**

```bash
tests/run_yaml_tests.sh
gcc -Wall -Wextra -Werror -Imain/utils -Imanaged_components/espressif__cjson/cJSON \
    tests/json_utils_test.c main/utils/json_utils.c \
    managed_components/espressif__cjson/cJSON/cJSON.c -o /tmp/jt -lm && /tmp/jt
gcc -Wall -Wextra -Werror -Imain/utils \
    tests/str_utils_test.c main/utils/str_utils.c -o /tmp/st && /tmp/st
```

Expected: `ALL YAML TESTS PASSED`、`json_utils: all tests passed`、`str_utils: all tests passed`。

- [ ] **Step 3: 设备回归**

```bash
python3 tools/espx_test.py --port /dev/ttyUSB0 all
python3 tools/network_tests.py --device-ip <ip> --local-ip <ip> --patch <p>
```

Expected: 全部通过。

- [ ] **Step 4: 契约与残留断言**

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx
echo "--- AT residue (expect none) ---"
grep -rn "at_service\|ESPX_AT" main/ tools/ docs/ README*.md | grep -v superpowers || echo OK
echo "--- core/ residue (expect none) ---"
grep -rn "main/core/\|\"core\"" main/CMakeLists.txt docs/*.md AGENT.md || echo OK
echo "--- NVS literals (expect 4 matches) ---"
grep -Fn 'NVS_NAMESPACE "espx_node"' main/config/node_config.c
grep -Fn 'NVS_KEY_CONFIG "config"' main/config/node_config.c
grep -Fn 'NVS_NAMESPACE "espx_devices"' main/device/device_manager.c
grep -Fn 'NVS_KEY_CONFIG "config"' main/device/device_manager.c
echo "--- directory layout ---"
ls main/ | sort
```

Expected: 前两组 `OK`；NVS 四行全部匹配；`ls` 含 `common config device utils`，不含 `core at_service`。

- [ ] **Step 5: Commit（如有文档收尾改动）**

```bash
git add -A
git commit -m "chore: final acceptance pass for the AT removal and module refactor"
```

---

## 验收标准（对应 spec 第 10 节）

- [ ] 编译 0 error / 0 warning（Task 17 Step 1）
- [ ] `tests/run_yaml_tests.sh` 通过，路径已更新（Task 7、17）
- [ ] `json_utils` / `str_utils` 主机测试通过（Task 4、5、17）
- [ ] `python3 tools/espx_test.py --port <port> all` 通过（Task 10、17）
- [ ] `grep` 无 `at_service` / `ESPX_AT` 残留（Task 1、3、17）
- [ ] 固件 `.bin` 体积因 AT 移除而减小（Task 1、17）
- [ ] `/api/system/info` 返回内部 RAM / PSRAM 分项与任务栈高水位（Task 16）
- [ ] 所有 Web API 端点逐一可用（Task 12、13、14）
- [ ] `docs/ARCHITECTURE.md`、`docs/DEVELOPMENT.md`、`AGENT.md` 反映新结构（Task 9、15）
- [ ] README / README.zh-CN / USAGE / TESTING / DEPLOYMENT 无 AT 残留（Task 3）
- [ ] NVS 命名空间与键名字面量未变（Task 10、17）
