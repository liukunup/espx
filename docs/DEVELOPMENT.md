# ESPX 开发手册

环境搭建、代码结构、如何新增外设驱动、调试手段。

---

## 1. 环境

```bash
# ESP-IDF（本项目在 v6.1 上验证）
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3
. ~/esp/esp-idf/export.sh

# 主机侧工具
pip install pyserial 'detools>=0.49.0' paho-mqtt
# 可选：EMQX 初始化与 AT 手工调试
brew install mosquitto            # 或用 tools/mini_mqtt_broker.py
```

```bash
idf.py set-target esp32s3     # sdkconfig.defaults 已固定 CONFIG_IDF_TARGET，删掉 sdkconfig 也不会跑偏
idf.py build
idf.py -p /dev/cu.usbserial-XXXX -b 460800 flash monitor
```

### 构建规范（硬性）

* **0 error / 0 warning**。`-Werror` 生效，警告即失败。
* 目标芯片固定在 `sdkconfig.defaults` 的 `CONFIG_IDF_TARGET`。
* 证书、网页等资源以**项目文件**维护，构建时用 CMake `EMBED_FILES` 嵌入，
  **不得把内容写进 C 代码**。

---

## 2. 代码结构

```
main/
├── app_main.c                   启动顺序（有两条硬性顺序约束，见下）
├── Kconfig.projbuild            全部可配置项
├── core/
│   ├── device_type.{c,h}        驱动目录（类型注册）
│   ├── device_manager.{c,h}     运行时设备实例 + NVS 持久化
│   ├── node_config.{c,h}        设备身份 + 网络配置
│   ├── event_bus.{c,h}          发布/订阅
│   ├── yaml.{c,h}               YAML 子集 → cJSON（不依赖 ESP-IDF）
│   └── config_apply.{c,h}       配置语义（唯一入口）
├── peripherals/                 驱动实现（dht11/button/relay/shiftreg_595/ws2812）
├── mqtt_client/                 连接、命令处理、周期上报
├── web_server/                  HTTPS + REST + WebSocket + 静态页面
├── cert_manager/                证书（certs/ 下的 PEM 文件）
├── wifi_prov/                   BLE / SoftAP 配网 + 预置凭据直连
├── net_services/                NTP、mDNS 聚合
├── at_service/                  串口 AT 指令
├── ota_service/                 差分 OTA
├── mfg_provision/               工厂预置
├── test_mode/                   产线自检控制台
└── web_server/web_files/        index.html（构建时嵌入）

components/led_driver/           可选状态灯（默认关闭）
tools/                           主机侧脚本
tests/                           主机单元测试
docs/                            文档
```

### 依赖只能向下

```
        config_apply            配置语义（唯一真相入口）
       /     |      \
   mqtt     https    console/AT   通道层：只解析 + 转发 + 回执
       \     |      /
        yaml (可选)              YAML → cJSON，不依赖 ESP-IDF
             |
  device_manager · node_config · event_bus
             |
       device_type 目录
             |
        peripherals 驱动
             |
          ESP-IDF HAL
```

* `yaml.c` 只依赖 cJSON 与 libc —— **必须可主机单元测试**，任何 ESP-IDF 依赖都不许进。
* 通道层（MQTT/HTTPS/AT/WS）**不含配置语义**，只做解析 → 调用 `config_apply()` → 回执。
* 驱动不反向依赖 `device_manager`（`device_manager` 通过函数指针调用驱动）。

### 两条硬性启动顺序（都曾是真实 bug）

```c
nvs_flash_init();                     // ① 必须在 test_mode_check_trigger() 之前
                                      //    否则 NVS 标志读不到，长按触发静默失效
test_mode_start_longpress_watchdog(); // ② 必须在 wifi_prov_start() 之前
                                      //    wifi_prov_start() 之后是阻塞等待，
                                      //    未配网设备（正是产线场景）永远进不去测试模式
```

其余服务**不得阻塞在 Wi-Fi 上**：MQTT 先启动并自行重试，需要 IP 的服务
（NTP、mDNS）在 `wifi_status_task` 里拿到 IP 后再启动。

---

## 3. 新增一个外设驱动

以「一位数码管」为例，共 3 步，**不需要改 core**。

### 第 1 步：实现驱动

`main/peripherals/seg7.c`：

```c
#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <cJSON.h>

#include "seg7.h"
#include "device_manager.h"

static const char *TAG = "seg7";

typedef struct {
    int gpio_a, gpio_b, gpio_c, gpio_d, gpio_e, gpio_f, gpio_g, gpio_dp;
    int active_level;
    int digit;              /* 0-9, -1 = blank */
} seg7_data_t;

static esp_err_t seg7_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *pin = cJSON_GetObjectItem(config, "data_gpio");
    if (!cJSON_IsNumber(pin)) {
        ESP_LOGE(TAG, "missing data_gpio");
        return ESP_ERR_INVALID_ARG;
    }

    seg7_data_t *d = calloc(1, sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;

    d->active_level = 1;
    cJSON *al = cJSON_GetObjectItem(config, "active_level");
    if (cJSON_IsNumber(al)) d->active_level = al->valueint;
    d->digit = -1;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin->valueint),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    dev->driver_data = d;
    ESP_LOGI(TAG, "seg7 '%s' on GPIO%d", dev->id, pin->valueint);
    return ESP_OK;
}

static esp_err_t seg7_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t seg7_read(device_t *dev, cJSON *value)
{
    seg7_data_t *d = dev->driver_data;
    if (!d) return ESP_ERR_INVALID_STATE;
    cJSON_AddNumberToObject(value, "digit", d->digit);
    return ESP_OK;
}

static esp_err_t seg7_write(device_t *dev, const cJSON *value)
{
    seg7_data_t *d = dev->driver_data;
    if (!d) return ESP_ERR_INVALID_STATE;

    int digit;
    if (cJSON_IsNumber(value))            digit = value->valueint;
    else if (cJSON_IsObject(value)) {
        cJSON *v = cJSON_GetObjectItem(value, "digit");
        if (!cJSON_IsNumber(v)) return ESP_ERR_INVALID_ARG;
        digit = v->valueint;
    } else return ESP_ERR_INVALID_ARG;

    if (digit < -1 || digit > 9) return ESP_ERR_INVALID_ARG;
    d->digit = digit;
    /* … 点灯逻辑 … */
    return ESP_OK;
}

static esp_err_t seg7_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "data_gpio", 6);
    cJSON_AddNumberToObject(config, "active_level", 1);
    return ESP_OK;
}

static esp_err_t seg7_validate_config(const cJSON *config)
{
    cJSON *pin = cJSON_GetObjectItem(config, "data_gpio");
    if (!cJSON_IsNumber(pin)) return ESP_ERR_INVALID_ARG;
    if (pin->valueint < 0 || pin->valueint > 48) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static const device_type_t seg7_driver = {
    .name = "seg7",
    .description = "single digit 7-segment display",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE
                  | DEVICE_CAPABILITY_NOTIFY,
    .init = seg7_init,
    .deinit = seg7_deinit,
    .read = seg7_read,
    .write = seg7_write,
    .get_default_config = seg7_default_config,
    .validate_config = seg7_validate_config,
};

esp_err_t seg7_driver_register(void)
{
    return device_type_register(&seg7_driver);
}
```

### 第 2 步：注册

`main/peripherals/peripherals.c`：

```c
#include "seg7.h"
...
ESP_ERROR_CHECK(seg7_driver_register());
```

### 第 3 步：加入构建

`main/CMakeLists.txt` 的 `espx_sources` 里加一行 `"peripherals/seg7.c"`。

### 完成

```bash
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' -d '
devices:
  - id: disp1
    type: seg7
    config: {data_gpio: 6, active_level: 1}
'
```

MQTT、AT、WebSocket、自检**全自动可用** —— 因为它们都基于
`device_type` 的能力位与 `device_manager`，不需要为每种外设写专门代码。

### 驱动编写要点

| 要点 | 说明 |
|---|---|
| 能力位 | 如实声明；`PERIODIC` 会让值自动进入 MQTT `sensors` 主题 |
| `get_default_config` | **必须实现**，配置端据此得知字段 |
| `validate_config` | 能校验就校验；拒绝非法参数优于把硬件配坏 |
| `read`/`write` | 不需要就留 `NULL`，调用方会得到 `ESP_ERR_NOT_SUPPORTED` |
| `tick` | 周期任务（100 ms 节拍），自行按 `interval_ms` 限频 |
| 不阻塞 | `tick` 在共享任务里跑，长阻塞会影响其他驱动 |
| 无策略 | 不决定何时上报、是否告警、如何命名 —— 那是配置与云端的事 |

---

## 4. 调试

### 日志等级

```bash
idf.py menuconfig     # Component config → Log output → Default log level → Debug
```

### 配置下发排查

```bash
HOST=espx-84c7bb772e74.local
curl -k -X POST https://$HOST/api/config -H 'Content-Type: text/yaml' --data-binary @x.yaml
```

串口会打印：

```
I (…) config_apply:   network.mqtt_broker = mqtt://192.168.1.10:1883
I (…) config_apply:   network.mqtt_password = <set>
I (…) config_apply: config applied: +2 ~0 -1 (failed 0), reboot to apply network changes
```

失败时：

```
E (…) config_apply:   device 'out16': ESP_ERR_INVALID_ARG
I (…) config_apply: config applied: +1 ~0 -0 (failed 1)
```

### 让日志带上真实时间

NTP 同步后日志时间戳才有意义；同步前的时间戳是相对启动时间。查看：

```bash
curl -k https://$HOST/api/system/info | grep -E 'time|epoch'
```

### 主机单元测试（不需要设备）

YAML 解析器被特意设计为不依赖 ESP-IDF，因此可直接在主机上跑：

```bash
tests/run_yaml_tests.sh
```

输出：

```
== yaml: core syntax ==
ALL PASS (0 failure(s))
== yaml: flow collections ==
ALL PASS (0 failure(s))
ALL YAML TESTS PASSED
```

新增语法支持时，先在 `tests/yaml_test_*.c` 里加断言并看它失败，再改
`main/core/yaml.c`。

### 串口日志抓取

```bash
./tools/verify_device.sh listen 20      # 复位并抓 20 秒日志到 dist/
```

### AT 指令手工调试

AT 在 UART1（GPIO4/5）。用第二个 USB-TTL 接上，或临时改到 UART0：

```
CONFIG_ESPX_AT_UART_NUM=0
```

> UART0 与日志/产线控制台共用，输出会交错，仅用于临时排查。

### 堆与性能

```bash
curl -k https://$HOST/api/system/info | python3 -m json.tool
```

关注 `free_heap` / `min_free_heap`。PSRAM 已启用（8MB），但请留意
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`：mbedTLS 的握手缓冲走内部 RAM，
内部堆紧张会导致 TLS 握手失败（表现为连接被重置）。

---

## 5. 内存与性能（实测数据）

这些数字来自 ESP32-S3 R16N8 实测，改动 TLS/HTTP 相关配置时请对照。

| 指标 | 数值 |
|---|---|
| 内部 RAM 总量 | **345 KB**（这是会耗尽的那个池；PSRAM 8MB 另计） |
| 启动后内部 RAM 空闲 | 约 180 KB（配网模式下更少） |
| 每个 TLS 会话内部 RAM | 约 25 KB（4KB in + 4KB out + 握手状态） |
| `max_open_sockets=4` 并发压力最低点 | 约 40 KB，之后可恢复 |
| TLS 握手（ECDSA P-256, 240MHz） | 首个约 0.5 s |
| 同连接后续请求（keep-alive） | 12–30 ms |
| TLS 握手（改用 RSA-2048 时） | 约 1.5 s（**不要用 RSA**） |

据此得出的三条硬性经验：

1. **用 ECDSA 证书**（`tools/gen_certs.sh` 默认）。RSA-2048 私钥运算慢三个数量级，
   是"网页打开很慢"的直接原因。
2. **TLS 缓冲放到 PSRAM**（`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`），并把
   `MBEDTLS_SSL_IN_CONTENT_LEN` 降到 4 KB。默认 16 KB 是为大文件传输准备的。
3. **`max_open_sockets` 不要贪大**。每个 socket 都是一整个 TLS 会话；
   内部 RAM 耗尽后服务器会拒绝**所有**新连接直到重启。

### 诊断手段

```bash
curl -k https://$HOST/api/system/info | python3 -m json.tool
```

关注 `ram.internal_free` 与 `ram.internal_min_free`（低水位）。串口每 30 秒也会打印：

```
I (…) app_main: heap: internal free 129000, internal min 40000, total free 8537000
```

**只看 `free_heap` 会漏掉问题** —— 它包含 PSRAM，而 PSRAM 几乎总是空闲的。

## 6. 提交前检查

```bash
# 1. 清理构建，确认 0 error / 0 warning
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build 2>&1 | grep -E "error:|warning:"

# 2. 主机单元测试
tests/run_yaml_tests.sh

# 3. 设备离线回归（启动 / 测试模式 / 硬件自检）
python3 tools/espx_test.py --port <串口> all

# 4. 网络回归（需要设备有 IP）
python3 tools/network_tests.py --device-ip <ip> --local-ip <本机ip> --patch <patch>

# 5. 差分补丁自校验
python3 tools/make_delta_patch.py --base <base.bin> --new <new.bin> --out /tmp/t.patch
```

行为变更同步更新 `docs/ARCHITECTURE.md`，新增用例同步更新 `docs/TESTING.md`。

---

## 6. 环境陷阱（macOS）

### 6.1 本地网络隐私（Local Network Privacy）会拦住部分 Python

macOS 15+ 要求应用获得「本地网络」权限。**用 miniconda / ~/.espressif 里的 Python
连同一局域网内的设备会得到 `EHOSTUNREACH`，而 `curl` 和 `nc` 正常**，即使它们
访问的是同一个 IP:端口。系统自带的 `/usr/bin/python3` 可正常访问。

现象：

```bash
curl -sk https://192.168.1.57/api/node          # 200
python3 -c "import socket; socket.create_connection(('192.168.1.57',443))"
# OSError: [Errno 65] No route to host
/usr/bin/python3 -c "import socket; socket.create_connection(('192.168.1.57',443))"   # OK
```

处理：网络测试用系统 Python。

```bash
/usr/bin/python3 -m pip install --user paho-mqtt pyserial
/usr/bin/python3 tools/network_tests.py --device-ip … --local-ip …
```

（注意：`EHOSTUNREACH` 也可能真的是网络问题。先按 6.2 排查，再怀疑权限。）

### 6.2 Wi-Fi 链路首包丢失

ESP32 关闭 modem sleep 之前，空闲后首个请求常有数百毫秒延迟甚至丢包，
表现为偶发的 `EHOSTUNREACH` / 超时。固件默认已 `WIFI_PS_NONE`；
测试脚本也对传输层错误做了重试（但不重试 HTTP 错误——那是真实回答）。

## 7. 常见编译问题

| 现象 | 原因与处理 |
|---|---|
| `Failed to resolve component 'mqtt'` | 本地模块目录名与外部组件重名。本项目用 `mqtt_client/` + `espx_mqtt_client.h` 规避 |
| `CONFIG_ESPX_xxx undeclared` | 布尔 Kconfig 未置位时不是 C 宏；C 代码里要用 `#ifdef`，不要 `if (CONFIG_…)` |
| `httpd_ws_frame_t unknown` | 未启用 `CONFIG_HTTPD_WS_SUPPORT` |
| `CONFIG_IDF_TARGET` 变成 esp32 | 删了 `sdkconfig` 但没固定 target；`sdkconfig.defaults` 中必须有 `CONFIG_IDF_TARGET="esp32s3"` |
| 分区表溢出 | `check_sizes.py` 报 app 分区太小；`partitions.csv` 已给 3×4MB，注意 `otadata` 不可省 |
| `esp_ota_set_boot_partition` 失败 | 分区表缺 `otadata` |
| TLS 握手每次都重置 | 检查 `cert_len` 是否**包含**结尾 NUL（mbedTLS 的 PEM 要求） |
| `EMBED_FILES` 符号名不对 | 名字为 `_binary_<路径中/替换为_>_start`，如 `_binary_server_crt_start` |
| `error: '/*' within comment` | 注释里出现 `/api/devices/*` 这种序列；`/*` 会提前结束注释，改写措辞 |
| 通配符 URI 一直 404 | **默认 `uri_match_fn == NULL` 时只做精确字符串比较**，通配符必须显式 `config.httpd.uri_match_fn = httpd_uri_match_wildcard` |
| `httpd_ws_frame_t` unknown | 未启用 `CONFIG_HTTPD_WS_SUPPORT` |
| 布尔 Kconfig 在 C 里报未声明 | 未置位的布尔 Kconfig 不是 C 宏，C 代码要用 `#ifdef` 而不是 `if (CONFIG_…)` |
| HTTPS 运行一段时间后彻底拒绝连接 | 内部 RAM 被并发 TLS 会话耗尽（PSA_ERROR_INSUFFICIENT_MEMORY）。降低 `max_open_sockets`、缩小 `MBEDTLS_SSL_*_CONTENT_LEN`、并把 mbedTLS 缓冲移到 PSRAM |
