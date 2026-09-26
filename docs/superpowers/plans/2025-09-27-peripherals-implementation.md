# ESPX 外设驱动扩展实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 ESPX 固件新增 5 个外设驱动（TJA1050/MCP4725/ADS1115/INA226/蜂鸣器），遵循现有 `device_type_t` + `device_manager` 架构，0 error / 0 warning，文档同步更新。

**Architecture:** 遵循现有驱动架构，每个外设独立实现为 `.c/.h` 文件对，通过 `peripherals_register_all()` 统一注册。I2C 外设（MCP4725/ADS1115/INA226）共享新建的 `esp_idf_i2c.h` 基础设施，各自独立管理 I2C 总线初始化。CAN 驱动使用 ESP32-S3 原生 TWAI 控制器。蜂鸣器使用 LEDC PWM 输出。

**Tech Stack:** ESP-IDF v6.1, ESP32-S3, C, cJSON, driver/twai.h, driver/gpio.h, driver/ledc.h, driver/i2c.h

**Spec:** `docs/superpowers/specs/2025-09-27-peripherals-design.md`

---

## Global Constraints

- 全部驱动 0 error / 0 warning（`-Werror` 生效）
- 驱动文件位于 `main/peripherals/`
- 通过 `peripherals_register_all()` 统一注册，不修改 `core/`
- `tick` 不阻塞（100ms 节拍共享任务）
- `init` 中 `calloc`，`deinit` 中 `free`
- `get_default_config` 必须实现

---

## Review Focus

| 失败模式 | 预期行为 | 所属任务 |
|---|---|---|
| I2C 地址错误或总线未初始化 | `read`/`write` 返回错误码而非崩溃 | Task 2/3/4 |
| CAN 帧格式错误 | `write` 返回 `ESP_ERR_INVALID_ARG` | Task 1 |
| ADS1115 gain 值超出范围 | `validate_config` 拒绝并返回错误 | Task 4 |
| 蜂鸣器 `auto_off_ms` 期间设备被删除 | `deinit` 取消定时器避免崩溃 | Task 6 |
| 多个 I2C 设备共用总线时的竞态 | I2C 总线表按 (sda, scl) 去重，同总线不加锁（ESP-IDF I2C driver 内部已加锁） | Task 2/3/4 |

---

## File Structure

```
main/peripherals/
├── esp_idf_i2c.h          # 新建：I2C 基础设施（头文件，函数内联实现）
├── tja1050.c/h            # 新建：CAN 驱动
├── mcp4725.c/h            # 新建：DAC 驱动
├── ads1115.c/h            # 新建：ADC 驱动
├── ina226.c/h             # 新建：功率监测驱动
├── buzzer.c/h             # 新建：蜂鸣器驱动
├── peripherals.c          # 修改：新增 5 个驱动的注册调用
├── shiftreg_595.c/h       # 已存在
├── ws2812.c/h             # 已存在
└── ...
main/CMakeLists.txt        # 修改：新增 5 个 .c 文件
README.md                  # 修改：外设表格新增 5 行
docs/ARCHITECTURE.md       # 修改：外设文档
docs/TESTING.md            # 修改：新增测试用例
```

---

## Task Decomposition

### Task 1: I2C 总线基础设施

**Files:**
- Create: `main/peripherals/esp_idf_i2c.h`

**Interfaces:**
- Produces: `esp_idf_i2c_init(sda_gpio, scl_gpio, freq_hz, out_port)`, `esp_idf_i2c_write(addr, data, len)`, `esp_idf_i2c_read(addr, data, len)`, `esp_idf_i2c_close(port)`

- [ ] **Step 1: Write esp_idf_i2c.h**

```c
#ifndef ESP_IDF_I2C_H
#define ESP_IDF_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>
#include <driver/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C bus if not already initialized for this (sda, scl) pair.
 *
 * Maintains a global registry of initialized buses. Repeated calls with the same
 * (sda, scl) return the existing port without re-initializing.
 *
 * @param sda_gpio SDA GPIO number
 * @param scl_gpio SCL GPIO number
 * @param freq_hz Clock frequency in Hz (e.g. 400000)
 * @param out_port Output pointer for the I2C port number
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz, i2c_port_t *out_port);

/**
 * @brief Write data to I2C device
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param data Data buffer
 * @param len Data length in bytes
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len);

/**
 * @brief Read data from I2C device
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param data Output buffer
 * @param len Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_read(i2c_port_t port, uint8_t addr, uint8_t *data, size_t len);

/**
 * @brief Write and then read I2C device (combined format)
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param wdata Write buffer
 * @param wlen Write length
 * @param rdata Read output buffer
 * @param rlen Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write_read(i2c_port_t port, uint8_t addr,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif // ESP_IDF_I2C_H
```

- [ ] **Step 2: Write esp_idf_i2c.c (implementation, place in esp_idf_i2c.h as static inline for simplicity)**

实际上，将实现放在 `.h` 中作为 static inline 函数会导致链接问题。为每个驱动独立创建 `.c` 文件又会导致重复代码。最佳方案是创建 `esp_idf_i2c.c` + `esp_idf_i2c.h`。

创建 `main/peripherals/esp_idf_i2c.c`：

```c
/**
 * @file esp_idf_i2c.c
 * @brief Shared I2C bus infrastructure
 */

#include "esp_idf_i2c.h"
#include <string.h>
#include <esp_log.h>

static const char *TAG = "i2c_bus";

#define MAX_I2C_PORTS 2
static struct {
    bool in_use;
    int sda_gpio;
    int scl_gpio;
    i2c_port_t port;
} g_i2c_buses[MAX_I2C_PORTS] = {0};

static bool gpio_pair_match(int sda1, int scl1, int sda2, int scl2)
{
    return (sda1 == sda2 && scl1 == scl2);
}

esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz, i2c_port_t *out_port)
{
    if (out_port == NULL) return ESP_ERR_INVALID_ARG;

    // Check if already initialized
    for (int i = 0; i < MAX_I2C_PORTS; i++) {
        if (g_i2c_buses[i].in_use &&
            gpio_pair_match(g_i2c_buses[i].sda_gpio, g_i2c_buses[i].scl_gpio,
                           sda_gpio, scl_gpio)) {
            *out_port = g_i2c_buses[i].port;
            ESP_LOGD(TAG, "Reusing I2C port %d for SDA=%d SCL=%d", i, sda_gpio, scl_gpio);
            return ESP_OK;
        }
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_I2C_PORTS; i++) {
        if (!g_i2c_buses[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "No free I2C port slots");
        return ESP_ERR_NO_MEM;
    }

    i2c_port_t port = (i2c_port_t)slot;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz,
    };

    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0));

    g_i2c_buses[slot] = (typeof(g_i2c_buses[0])){
        .in_use = true,
        .sda_gpio = sda_gpio,
        .scl_gpio = scl_gpio,
        .port = port,
    };

    *out_port = port;
    ESP_LOGI(TAG, "I2C port %d initialized: SDA=%d SCL=%d %dHz", port, sda_gpio, scl_gpio, freq_hz);
    return ESP_OK;
}

esp_err_t esp_idf_i2c_write(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, addr << 1, true));
    if (len > 0) {
        ESP_ERROR_CHECK(i2c_master_write(cmd, data, len, true));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t esp_idf_i2c_read(i2c_port_t port, uint8_t addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (addr << 1) | 1, true));
    if (len > 0) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t esp_idf_i2c_write_read(i2c_port_t port, uint8_t addr,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, addr << 1, true));
    if (wlen > 0) {
        ESP_ERROR_CHECK(i2c_master_write(cmd, wdata, wlen, true));
    }
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (addr << 1) | 1, true));
    if (rlen > 0) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, rdata, rlen, I2C_MASTER_LAST_NACK));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}
```

- [ ] **Step 3: Update CMakeLists.txt**

在 `espx_sources` 中新增：`"peripherals/esp_idf_i2c.c"`

- [ ] **Step 4: Commit**

```bash
git add main/peripherals/esp_idf_i2c.h main/peripherals/esp_idf_i2c.c main/CMakeLists.txt
git commit -m "feat: add shared I2C bus infrastructure"
```

---

### Task 2: TJA1050 CAN 驱动

**Files:**
- Create: `main/peripherals/tja1050.c`
- Create: `main/peripherals/tja1050.h`
- Modify: `main/peripherals/peripherals.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `device_type_t`, `device_manager.h`, `driver/twai.h`
- Produces: `tja1050_driver_register()`

- [ ] **Step 1: Write tja1050.h**

```c
#ifndef TJA1050_H
#define TJA1050_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tja1050_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // TJA1050_H
```

- [ ] **Step 2: Write tja1050.c**

关键实现要点：
- `init`: 解析 `tx_gpio`/`rx_gpio`/`bitrate`，配置 GPIO 复用，启动 TWAI
- `deinit`: 停止 TWAI，释放资源
- `read`: 从 TWAI 接收队列取帧，返回 `{id, data, ext, rtr, timestamp_ms}`
- `write`: 构造 TWAI 消息并发送，支持标准/扩展帧
- `tick`: 非阻塞轮询接收队列，有帧则通过 `event_bus_publish` 发布
- `get_default_config`: 默认 `bitrate=500000, tx_queue_size=5, rx_queue_size=10`
- `validate_config`: 校验 GPIO 范围和 bitrate 有效值

```c
/**
 * @file tja1050.c
 * @brief TJA1050 CAN (TWAI) driver
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "tja1050.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "tja1050";

typedef struct {
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    uint32_t bitrate;
    int tx_queue_size;
    int rx_queue_size;
    QueueHandle_t rx_queue;
    bool initialized;
} tja1050_data_t;

static esp_err_t tja1050_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *tx_gpio_node = cJSON_GetObjectItem(config, "tx_gpio");
    cJSON *rx_gpio_node = cJSON_GetObjectItem(config, "rx_gpio");
    cJSON *bitrate_node = cJSON_GetObjectItem(config, "bitrate");

    if (!cJSON_IsNumber(tx_gpio_node) || !cJSON_IsNumber(rx_gpio_node)) {
        return ESP_ERR_INVALID_ARG;
    }

    tja1050_data_t *data = calloc(1, sizeof(tja1050_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->tx_gpio = (gpio_num_t)tx_gpio_node->valueint;
    data->rx_gpio = (gpio_num_t)rx_gpio_node->valueint;
    data->bitrate = cJSON_IsNumber(bitrate_node) ? bitrate_node->valueint : 500000;
    data->tx_queue_size = 5;
    data->rx_queue_size = 10;

    cJSON *txq = cJSON_GetObjectItem(config, "tx_queue_size");
    cJSON *rxq = cJSON_GetObjectItem(config, "rx_queue_size");
    if (cJSON_IsNumber(txq)) data->tx_queue_size = txq->valueint;
    if (cJSON_IsNumber(rxq)) data->rx_queue_size = rxq->valueint;

    data->rx_queue = xQueueCreate(data->rx_queue_size, sizeof(twai_message_t));
    if (!data->rx_queue) {
        free(data);
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << data->tx_gpio) | (1ULL << data->rx_gpio),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    if (data->bitrate == 125000) t_config = TWAI_TIMING_CONFIG_125KBITS();
    else if (data->bitrate == 250000) t_config = TWAI_TIMING_CONFIG_250KBITS();
    else if (data->bitrate == 1000000) t_config = TWAI_TIMING_CONFIG_1MBITS();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(data->tx_gpio, data->rx_gpio, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = data->tx_queue_size;
    g_config.rx_queue_len = data->rx_queue_size;

    esp_err_t err = twai_driver_install(&g_config, &t_config, &TWAI_STANDARD_CONFIGS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(err));
        vQueueDelete(data->rx_queue);
        free(data);
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        vQueueDelete(data->rx_queue);
        free(data);
        return err;
    }

    data->initialized = true;
    dev->driver_data = data;
    ESP_LOGI(TAG, "TJA1050 initialized on TX=%d RX=%d %dbps", data->tx_gpio, data->rx_gpio, data->bitrate);
    return ESP_OK;
}

static esp_err_t tja1050_deinit(device_t *dev)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    if (data->initialized) {
        twai_stop();
        twai_driver_uninstall();
        data->initialized = false;
    }
    if (data->rx_queue) {
        vQueueDelete(data->rx_queue);
    }
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t tja1050_read(device_t *dev, cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    twai_message_t msg;
    if (xQueueReceive(data->rx_queue, &msg, 0) == pdTRUE) {
        cJSON_AddNumberToObject(value, "id", msg.identifier);
        cJSON_AddBoolToObject(value, "ext", msg.extd);
        cJSON_AddBoolToObject(value, "rtr", msg.rtr);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < msg.data_length_code; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg.data[i]));
        }
        cJSON_AddItemToObject(value, "data", arr);
    }
    return ESP_OK;
}

static esp_err_t tja1050_write(device_t *dev, const cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_ERR_INVALID_STATE;

    twai_message_t msg = {0};

    cJSON *id_node = cJSON_GetObjectItem(value, "id");
    cJSON *data_node = cJSON_GetObjectItem(value, "data");
    cJSON *ext_node = cJSON_GetObjectItem(value, "ext");
    cJSON *rtr_node = cJSON_GetObjectItem(value, "rtr");

    if (!cJSON_IsNumber(id_node)) return ESP_ERR_INVALID_ARG;
    msg.identifier = id_node->valueint;
    msg.extd = cJSON_IsTrue(ext_node);
    msg.rtr = cJSON_IsTrue(rtr_node);

    if (cJSON_IsArray(data_node)) {
        msg.data_length_code = cJSON_GetArraySize(data_node);
        if (msg.data_length_code > 8) return ESP_ERR_INVALID_ARG;
        for (int i = 0; i < msg.data_length_code; i++) {
            cJSON *item = cJSON_GetArrayItem(data_node, i);
            if (cJSON_IsNumber(item)) msg.data[i] = item->valueint;
        }
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "TX id=0x%03X dlc=%d", msg.identifier, msg.data_length_code);
    }
    return err;
}

static esp_err_t tja1050_tick(device_t *dev)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_OK;

    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (uxQueueSpacesAvailable(data->rx_queue) > 0) {
            xQueueSend(data->rx_queue, &msg, 0);
            ESP_LOGD(TAG, "RX id=0x%03X dlc=%d", msg.identifier, msg.data_length_code);
        }
    }
    return ESP_OK;
}

static esp_err_t tja1050_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "tx_gpio", 6);
    cJSON_AddNumberToObject(config, "rx_gpio", 7);
    cJSON_AddNumberToObject(config, "bitrate", 500000);
    cJSON_AddNumberToObject(config, "tx_queue_size", 5);
    cJSON_AddNumberToObject(config, "rx_queue_size", 10);
    return ESP_OK;
}

static esp_err_t tja1050_validate_config(const cJSON *config)
{
    cJSON *tx = cJSON_GetObjectItem(config, "tx_gpio");
    cJSON *rx = cJSON_GetObjectItem(config, "rx_gpio");
    if (!cJSON_IsNumber(tx) || !cJSON_IsNumber(rx)) return ESP_ERR_INVALID_ARG;
    if (tx->valueint < 0 || tx->valueint > 48 || rx->valueint < 0 || rx->valueint > 48) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *br = cJSON_GetObjectItem(config, "bitrate");
    if (cJSON_IsNumber(br)) {
        int b = br->valueint;
        if (b != 125000 && b != 250000 && b != 500000 && b != 1000000) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

static const device_type_t tja1050_driver = {
    .name = "can",
    .description = "TJA1050 CAN (TWAI) transceiver",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = tja1050_init,
    .deinit = tja1050_deinit,
    .read = tja1050_read,
    .write = tja1050_write,
    .tick = tja1050_tick,
    .get_default_config = tja1050_default_config,
    .validate_config = tja1050_validate_config,
};

esp_err_t tja1050_driver_register(void)
{
    return device_type_register(&tja1050_driver);
}
```

- [ ] **Step 3: Update peripherals.c** — 在 `peripherals_register_all()` 中新增 `ESP_ERROR_CHECK(tja1050_driver_register());`

- [ ] **Step 4: Update CMakeLists.txt** — 新增 `"peripherals/tja1050.c"`

- [ ] **Step 5: Build verification**

Run: `cd /Users/liukunup/Documents/repo/GitHub/espx && idf.py build 2>&1 | grep -E "error:|warning:"`
Expected: 0 errors, 0 warnings

- [ ] **Step 6: Commit**

```bash
git add main/peripherals/tja1050.c main/peripherals/tja1050.h main/peripherals/peripherals.c main/CMakeLists.txt
git commit -m "feat: add TJA1050 CAN driver (twai)"
```

---

### Task 3: MCP4725 DAC 驱动

**Files:**
- Create: `main/peripherals/mcp4725.c`
- Create: `main/peripherals/mcp4725.h`
- Modify: `main/peripherals/peripherals.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_idf_i2c.h`, `device_type.h`
- Produces: `mcp4725_driver_register()`

- [ ] **Step 1: Write mcp4725.h**

```c
#ifndef MCP4725_H
#define MCP4725_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcp4725_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // MCP4725_H
```

- [ ] **Step 2: Write mcp4725.c**

关键实现：
- `init`: 解析配置，调用 `esp_idf_i2c_init`，初始化 I2C 总线
- `write`: 发送 2 字节 DAC 数据，格式：`(value << 4) & 0x0FFF`，字节 1 = 高 8 位，字节 2 = (低 4 位 << 4) | 0x00（或 EEPROM 位）。支持 `{eeprom: true}` 写 EEPROM
- `read`: 返回缓存值 `{value, voltage_mv, vref_mv}`
- `deinit`: free data，I2C 总线由框架管理（不关闭）

```c
/**
 * @file mcp4725.c
 * @brief MCP4725 12-bit DAC driver (I2C)
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <cJSON.h>

#include "mcp4725.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "mcp4725";

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    uint16_t value;      // Cached value (0-4095)
    int vref_mv;
} mcp4725_data_t;

esp_err_t mcp4725_write_value(mcp4725_data_t *data, uint16_t value, bool to_eeprom)
{
    if (value > 4095) return ESP_ERR_INVALID_ARG;

    uint8_t buf[3];
    buf[0] = to_eeprom ? 0x60 : 0x40;  // Command: write DAC / write DAC+EEPROM
    buf[1] = (value >> 4) & 0xFF;       // Upper 8 bits
    buf[2] = (value & 0x0F) << 4;       // Lower 4 bits (upper nibble), lower nibble = 0

    return esp_idf_i2c_write(data->i2c_port, data->i2c_addr, buf, 3);
}

static esp_err_t mcp4725_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    mcp4725_data_t *data = calloc(1, sizeof(mcp4725_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->i2c_addr = 0x60;
    data->vref_mv = 3300;
    data->value = 0;

    cJSON *addr = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *vref = cJSON_GetObjectItem(config, "vref_mv");
    cJSON *freq = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr)) data->i2c_addr = addr->valueint;
    if (cJSON_IsNumber(vref)) data->vref_mv = vref->valueint;
    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->i2c_port);
    if (err != ESP_OK) {
        free(data);
        return err;
    }

    dev->driver_data = data;
    ESP_LOGI(TAG, "MCP4725 init: SDA=%d SCL=%d addr=0x%02X vref=%dmV",
             sda->valueint, scl->valueint, data->i2c_addr, data->vref_mv);
    return ESP_OK;
}

static esp_err_t mcp4725_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t mcp4725_read(device_t *dev, cJSON *value)
{
    mcp4725_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "value", data->value);
    double voltage_mv = (double)data->value / 4095.0 * data->vref_mv;
    cJSON_AddNumberToObject(value, "voltage_mv", voltage_mv);
    cJSON_AddNumberToObject(value, "vref_mv", data->vref_mv);
    return ESP_OK;
}

static esp_err_t mcp4725_write(device_t *dev, const cJSON *value)
{
    mcp4725_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    uint16_t dac_value;
    bool to_eeprom = false;

    if (cJSON_IsObject(value)) {
        cJSON *eeprom = cJSON_GetObjectItem(value, "eeprom");
        if (cJSON_IsTrue(eeprom)) to_eeprom = true;
        cJSON *v = cJSON_GetObjectItem(value, "value");
        if (!cJSON_IsNumber(v)) return ESP_ERR_INVALID_ARG;
        dac_value = v->valueint;
    } else if (cJSON_IsNumber(value)) {
        dac_value = value->valueint;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (dac_value > 4095) return ESP_ERR_INVALID_ARG;

    esp_err_t err = mcp4725_write_value(data, dac_value, to_eeprom);
    if (err == ESP_OK) {
        data->value = dac_value;
        ESP_LOGD(TAG, "MCP4725 set: %d (%.2fmV)", dac_value,
                 (double)dac_value / 4095.0 * data->vref_mv);
    }
    return err;
}

static esp_err_t mcp4725_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x60);
    cJSON_AddNumberToObject(config, "vref_mv", 3300);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static const device_type_t mcp4725_driver = {
    .name = "mcp4725",
    .description = "MCP4725 12-bit DAC (I2C)",
    .capabilities = DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_READ,
    .init = mcp4725_init,
    .deinit = mcp4725_deinit,
    .read = mcp4725_read,
    .write = mcp4725_write,
    .get_default_config = mcp4725_default_config,
};

esp_err_t mcp4725_driver_register(void)
{
    return device_type_register(&mcp4725_driver);
}
```

- [ ] **Step 3: Update peripherals.c** — 新增 `ESP_ERROR_CHECK(mcp4725_driver_register());`

- [ ] **Step 4: Update CMakeLists.txt** — 新增 `"peripherals/mcp4725.c"`

- [ ] **Step 5: Build verification**

Run: `idf.py build 2>&1 | grep -E "error:|warning:"`
Expected: 0 errors, 0 warnings

- [ ] **Step 6: Commit**

```bash
git add main/peripherals/mcp4725.c main/peripherals/mcp4725.h main/peripherals/peripherals.c main/CMakeLists.txt
git commit -m "feat: add MCP4725 DAC driver"
```

---

### Task 4: ADS1115 ADC 驱动

**Files:**
- Create: `main/peripherals/ads1115.c`
- Create: `main/peripherals/ads1115.h`
- Modify: `main/peripherals/peripherals.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_idf_i2c.h`, `device_type.h`
- Produces: `ads1115_driver_register()`

- [ ] **Step 1: Write ads1115.h**

```c
#ifndef ADS1115_H
#define ADS1115_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ads1115_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // ADS1115_H
```

- [ ] **Step 2: Write ads1115.c**

ADS1115 寄存器：
- Pointer Register: 0x00=Conversion, 0x01=Config, 0x02=Lo_thresh, 0x03=Hi_thresh
- Config Register:
  - Bits[15]: OS (1=start single conversion)
  - Bits[14:12]: MUX (000=A0-A1, 001=A0-A3, 010=A1-A3, 011=A2-A3, 100=A0-GND, 101=A1-GND, 110=A2-GND, 111=A3-GND)
  - Bits[11:9]: PGA (000=6.144V, 001=4.096V, 010=2.048V, 011=1.024V, 100=0.512V, 101=0.256V, 110=0.256V, 111=0.256V)
  - Bits[8:7]: MODE (0=continuous, 1=single-shot)
  - Bits[6:5]: DR (000=8SPS, 001=16, 010=32, 011=64, 100=128, 101=250, 110=475, 111=860)
  - Bits[4:0]: Comparator config

实现要点：
- `init`: 解析配置，初始化 I2C 总线
- `tick`: 触发单次转换 → 等待完成 → 读取结果
- `read`: 返回最近一次读数（本地缓存）
- `interval_ms` 限频使用上次读取时间判断

```c
/**
 * @file ads1115.c
 * @brief ADS1115 16-bit ADC driver (I2C)
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <cJSON.h>

#include "ads1115.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "ads1115";

// ADS1115 registers
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    int channel;          // 0-3 for single-ended
    int gain;             // PGA gain code 0-7
    int rate;             // SPS code 0-7
    int interval_ms;
    int64_t last_read_us;
    int16_t last_raw;
    double last_mv;
    // PGA full-scale voltages (mV)
    int pga_fs[8];
} ads1115_data_t;

// Default: single-ended channel n vs GND
static const uint8_t ADS1115_MUX_SINGLE[4] = {0x04, 0x05, 0x06, 0x07};

static const int ADS1115_RATE_CODES[8] = {8, 16, 32, 64, 128, 250, 475, 860};

static const int ADS1115_PGA_FS[8] = {6144, 4096, 2048, 1024, 512, 256, 256, 256};

static esp_err_t ads1115_set_config(ads1115_data_t *data)
{
    uint8_t mux = ADS1115_MUX_SINGLE[data->channel & 0x03];
    uint8_t pga = data->gain & 0x07;
    uint8_t dr = data->rate & 0x07;

    uint16_t config = (1 << 15) |   // OS: start single conversion
                      ((uint16_t)mux << 12) |
                      ((uint16_t)pga << 9) |
                      (1 << 8) |    // MODE: single-shot
                      ((uint16_t)dr << 5);

    uint8_t cmd[3] = {ADS1115_REG_CONFIG, (uint8_t)(config >> 8), (uint8_t)(config & 0xFF)};
    return esp_idf_i2c_write(data->i2c_port, data->i2c_addr, cmd, 3);
}

static esp_err_t ads1115_read_raw(ads1115_data_t *data, int16_t *out_raw)
{
    uint8_t reg = ADS1115_REG_CONVERSION;
    esp_err_t err = esp_idf_i2c_write(data->i2c_port, data->i2c_addr, &reg, 1);
    if (err != ESP_OK) return err;

    uint8_t buf[2];
    err = esp_idf_i2c_read(data->i2c_port, data->i2c_addr, buf, 2);
    if (err != ESP_OK) return err;

    *out_raw = (int16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static esp_err_t ads1115_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    ads1115_data_t *data = calloc(1, sizeof(ads1115_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->i2c_addr = 0x48;
    data->channel = 0;
    data->gain = 1;  // 1x = ±4.096V
    data->rate = 4;  // 128 SPS
    data->interval_ms = 1000;
    data->last_read_us = 0;

    cJSON *addr = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *ch = cJSON_GetObjectItem(config, "channel");
    cJSON *gain_n = cJSON_GetObjectItem(config, "gain");
    cJSON *rate_n = cJSON_GetObjectItem(config, "rate");
    cJSON *interval = cJSON_GetObjectItem(config, "interval_ms");
    cJSON *freq = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr)) data->i2c_addr = addr->valueint;
    if (cJSON_IsNumber(ch)) data->channel = ch->valueint;
    if (cJSON_IsNumber(gain_n)) data->gain = gain_n->valueint;
    if (cJSON_IsNumber(rate_n)) data->rate = rate_n->valueint;
    if (cJSON_IsNumber(interval)) data->interval_ms = interval->valueint;
    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->i2c_port);
    if (err != ESP_OK) { free(data); return err; }

    dev->driver_data = data;
    ESP_LOGI(TAG, "ADS1115 init: SDA=%d SCL=%d addr=0x%02X ch=%d gain=%d rate=%d",
             sda->valueint, scl->valueint, data->i2c_addr, data->channel, data->gain, data->rate);
    return ESP_OK;
}

static esp_err_t ads1115_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t ads1115_read(device_t *dev, cJSON *value)
{
    ads1115_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "raw", data->last_raw);
    cJSON_AddNumberToObject(value, "mv", data->last_mv);
    cJSON_AddNumberToObject(value, "channel", data->channel);
    cJSON_AddNumberToObject(value, "gain", data->gain);
    cJSON_AddNumberToObject(value, "rate", ADS1115_RATE_CODES[data->rate & 0x07]);
    return ESP_OK;
}

static esp_err_t ads1115_tick(device_t *dev)
{
    ads1115_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    int64_t now_us = esp_timer_get_time();
    int64_t interval_us = (int64_t)data->interval_ms * 1000;
    if (data->last_read_us > 0 && (now_us - data->last_read_us) < interval_us) {
        return ESP_OK;  // Not yet time to read
    }

    esp_err_t err = ads1115_set_config(data);
    if (err != ESP_OK) return err;

    // Wait for conversion: conversion time = 1 / SPS
    uint32_t conv_time_us = 1000000 / ADS1115_RATE_CODES[data->rate & 0x07];
    vTaskDelay(pdMS_TO_TICKS((conv_time_us / 1000) + 1));

    int16_t raw;
    err = ads1115_read_raw(data, &raw);
    if (err == ESP_OK) {
        data->last_raw = raw;
        double fs_mv = ADS1115_PGA_FS[data->gain & 0x07];
        data->last_mv = (raw / 32768.0) * fs_mv;
        data->last_read_us = esp_timer_get_time();
        ESP_LOGD(TAG, "ADS1115 ch%d: raw=%d mv=%.2f", data->channel, raw, data->last_mv);
    }
    return err;
}

static esp_err_t ads1115_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x48);
    cJSON_AddNumberToObject(config, "channel", 0);
    cJSON_AddNumberToObject(config, "gain", 1);
    cJSON_AddNumberToObject(config, "rate", 4);
    cJSON_AddNumberToObject(config, "interval_ms", 1000);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static esp_err_t ads1115_validate_config(const cJSON *config)
{
    cJSON *ch = cJSON_GetObjectItem(config, "channel");
    cJSON *gain = cJSON_GetObjectItem(config, "gain");
    cJSON *rate = cJSON_GetObjectItem(config, "rate");
    if (cJSON_IsNumber(ch) && (ch->valueint < 0 || ch->valueint > 3)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(gain) && (gain->valueint < 0 || gain->valueint > 7)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(rate) && (rate->valueint < 0 || rate->valueint > 7)) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static const device_type_t ads1115_driver = {
    .name = "ads1115",
    .description = "ADS1115 16-bit ADC (I2C)",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_PERIODIC | DEVICE_CAPABILITY_NOTIFY,
    .init = ads1115_init,
    .deinit = ads1115_deinit,
    .read = ads1115_read,
    .tick = ads1115_tick,
    .get_default_config = ads1115_default_config,
    .validate_config = ads1115_validate_config,
};

esp_err_t ads1115_driver_register(void)
{
    return device_type_register(&ads1115_driver);
}
```

- [ ] **Step 3: Update peripherals.c** — 新增 `ESP_ERROR_CHECK(ads1115_driver_register());`

- [ ] **Step 4: Update CMakeLists.txt** — 新增 `"peripherals/ads1115.c"`

- [ ] **Step 5: Build verification**

Run: `idf.py build 2>&1 | grep -E "error:|warning:"`
Expected: 0 errors, 0 warnings

- [ ] **Step 6: Commit**

```bash
git add main/peripherals/ads1115.c main/peripherals/ads1115.h main/peripherals/peripherals.c main/CMakeLists.txt
git commit -m "feat: add ADS1115 ADC driver"
```

---

### Task 5: INA226 功率监测驱动

**Files:**
- Create: `main/peripherals/ina226.c`
- Create: `main/peripherals/ina226.h`
- Modify: `main/peripherals/peripherals.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `esp_idf_i2c.h`, `device_type.h`
- Produces: `ina226_driver_register()`

- [ ] **Step 1: Write ina226.h**

```c
#ifndef INA226_H
#define INA226_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ina226_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // INA226_H
```

- [ ] **Step 2: Write ina226.c**

INA226 寄存器：
- 0x00: Config
- 0x01: Shunt Voltage
- 0x02: Bus Voltage
- 0x03: Power
- 0x04: Current
- 0x05: Cal = 0.00512 / (Current_LSB × Rshunt)，其中 Current_LSB = MaxCurrent / 32768
- 0x06: Mask/Enable
- 0x07: Alert

实现要点：
- `init`: 计算 Cal 寄存器值并写入，配置 average mode 和 conversion time
- `read`: 读取 shunt voltage、bus voltage，通过公式计算 current 和 power
- `tick`: 按 `interval_ms` 限频读取

```c
/**
 * @file ina226.c
 * @brief INA226 power monitor driver (I2C)
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <cJSON.h>
#include <math.h>

#include "ina226.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "ina226";

// INA226 registers
#define INA226_REG_CONFIG      0x00
#define INA226_REG_SHUNT_VOLT  0x01
#define INA226_REG_BUS_VOLT    0x02
#define INA226_REG_POWER       0x03
#define INA226_REG_CURRENT     0x04
#define INA226_REG_CAL         0x05

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    float r_shunt;         // mΩ
    float current_lsb;     // A per LSB
    float max_current_ma;  // A
    int bus_voltage_range; // 16 or 36V
    int avg_mode;          // 0-7
    int bus_conv_time;     // 0-7
    int shunt_conv_time;   // 0-7
    int interval_ms;
    int64_t last_read_us;

    double bus_voltage_mv;
    double shunt_voltage_uv;
    double current_ma;
    double power_mw;
} ina226_data_t;

static esp_err_t ina226_write_reg(ina226_data_t *data, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return esp_idf_i2c_write(data->i2c_port, data->i2c_addr, buf, 3);
}

static esp_err_t ina226_read_reg(ina226_data_t *data, uint8_t reg, uint16_t *out_val)
{
    esp_err_t err = esp_idf_i2c_write(data->i2c_port, data->i2c_addr, &reg, 1);
    if (err != ESP_OK) return err;
    uint8_t buf[2];
    err = esp_idf_i2c_read(data->i2c_port, data->i2c_addr, buf, 2);
    if (err != ESP_OK) return err;
    *out_val = (buf[0] << 8) | buf[1];
    return ESP_OK;
}

static esp_err_t ina226_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    ina226_data_t *data = calloc(1, sizeof(ina226_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->i2c_addr = 0x40;
    data->r_shunt = 10.0f;
    data->max_current_ma = 1000.0f;
    data->bus_voltage_range = 16;
    data->avg_mode = 3;      // 64 averages
    data->bus_conv_time = 3; // 1.1ms
    data->shunt_conv_time = 3;
    data->interval_ms = 1000;

    cJSON *addr = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *rsh = cJSON_GetObjectItem(config, "r_shunt");
    cJSON *maxcur = cJSON_GetObjectItem(config, "max_current_ma");
    cJSON *bvrange = cJSON_GetObjectItem(config, "bus_voltage_range");
    cJSON *avg = cJSON_GetObjectItem(config, "avg_mode");
    cJSON *bvct = cJSON_GetObjectItem(config, "bus_conv_time");
    cJSON *svct = cJSON_GetObjectItem(config, "shunt_conv_time");
    cJSON *interval = cJSON_GetObjectItem(config, "interval_ms");
    cJSON *freq = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr)) data->i2c_addr = addr->valueint;
    if (cJSON_IsNumber(rsh)) data->r_shunt = rsh->valueint;
    if (cJSON_IsNumber(maxcur)) data->max_current_ma = maxcur->valueint;
    if (cJSON_IsNumber(bvrange)) data->bus_voltage_range = bvrange->valueint;
    if (cJSON_IsNumber(avg)) data->avg_mode = avg->valueint & 0x07;
    if (cJSON_IsNumber(bvct)) data->bus_conv_time = bvct->valueint & 0x07;
    if (cJSON_IsNumber(svct)) data->shunt_conv_time = svct->valueint & 0x07;
    if (cJSON_IsNumber(interval)) data->interval_ms = interval->valueint;
    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->i2c_port);
    if (err != ESP_OK) { free(data); return err; }

    // Calculate Current_LSB: MaxCurrent / 32768
    data->current_lsb = (data->max_current_ma / 1000.0f) / 32768.0f;

    // Calculate Cal register: 0.00512 / (Current_LSB × Rshunt_mΩ)
    uint16_t cal = (uint16_t)(0.00512f / (data->current_lsb * (data->r_shunt / 1000.0f)));

    // Config: reset, then set operating mode
    uint16_t config = (1 << 15) | // RST
                      ((data->avg_mode & 0x07) << 9) |
                      ((data->bus_conv_time & 0x07) << 6) |
                      ((data->shunt_conv_time & 0x07) << 3) |
                      0x07;       // Shunt+Bus continuous

    err = ina226_write_reg(data, INA226_REG_CONFIG, config);
    if (err != ESP_OK) { free(data); return err; }

    err = ina226_write_reg(data, INA226_REG_CAL, cal);
    if (err != ESP_OK) { free(data); return err; }

    dev->driver_data = data;
    ESP_LOGI(TAG, "INA226 init: SDA=%d SCL=%d addr=0x%02X Rshunt=%.1fmΩ Cal=0x%04X",
             sda->valueint, scl->valueint, data->i2c_addr, data->r_shunt, cal);
    return ESP_OK;
}

static esp_err_t ina226_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t ina226_read(device_t *dev, cJSON *value)
{
    ina226_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "bus_voltage_mv", data->bus_voltage_mv);
    cJSON_AddNumberToObject(value, "shunt_voltage_uv", data->shunt_voltage_uv);
    cJSON_AddNumberToObject(value, "current_ma", data->current_ma);
    cJSON_AddNumberToObject(value, "power_mw", data->power_mw);
    return ESP_OK;
}

static esp_err_t ina226_tick(device_t *dev)
{
    ina226_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    int64_t now_us = esp_timer_get_time();
    int64_t interval_us = (int64_t)data->interval_ms * 1000;
    if (data->last_read_us > 0 && (now_us - data->last_read_us) < interval_us) {
        return ESP_OK;
    }

    uint16_t shunt_raw, bus_raw;
    esp_err_t err = ina226_read_reg(data, INA226_REG_SHUNT_VOLT, &shunt_raw);
    if (err != ESP_OK) return err;

    err = ina226_read_reg(data, INA226_REG_BUS_VOLT, &bus_raw);
    if (err != ESP_OK) return err;

    // Shunt voltage: raw * 2.5μV (signed)
    int16_t shunt_signed = (int16_t)shunt_raw;
    data->shunt_voltage_uv = shunt_signed * 2.5;

    // Bus voltage: raw * 1.25mV
    data->bus_voltage_mv = (bus_raw >> 3) * 1.25;

    // Current: raw * Current_LSB
    int16_t current_raw;
    err = ina226_read_reg(data, INA226_REG_CURRENT, &current_raw);
    if (err == ESP_OK) {
        data->current_ma = current_raw * data->current_lsb * 1000.0;
    }

    // Power: raw * (Current_LSB × 20)
    uint16_t power_raw;
    err = ina226_read_reg(data, INA226_REG_POWER, &power_raw);
    if (err == ESP_OK) {
        data->power_mw = power_raw * data->current_lsb * 20.0 * 1000.0;
    }

    data->last_read_us = now_us;
    ESP_LOGD(TAG, "INA226: V=%.2fmV I=%.2fmA P=%.2fmW",
             data->bus_voltage_mv, data->current_ma, data->power_mw);
    return ESP_OK;
}

static esp_err_t ina226_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x40);
    cJSON_AddNumberToObject(config, "r_shunt", 10.0);
    cJSON_AddNumberToObject(config, "max_current_ma", 1000);
    cJSON_AddNumberToObject(config, "bus_voltage_range", 16);
    cJSON_AddNumberToObject(config, "avg_mode", 3);
    cJSON_AddNumberToObject(config, "bus_conv_time", 3);
    cJSON_AddNumberToObject(config, "shunt_conv_time", 3);
    cJSON_AddNumberToObject(config, "interval_ms", 1000);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static const device_type_t ina226_driver = {
    .name = "ina226",
    .description = "INA226 power monitor (I2C)",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_PERIODIC | DEVICE_CAPABILITY_NOTIFY,
    .init = ina226_init,
    .deinit = ina226_deinit,
    .read = ina226_read,
    .tick = ina226_tick,
    .get_default_config = ina226_default_config,
};

esp_err_t ina226_driver_register(void)
{
    return device_type_register(&ina226_driver);
}
```

- [ ] **Step 3: Update peripherals.c** — 新增 `ESP_ERROR_CHECK(ina226_driver_register());`

- [ ] **Step 4: Update CMakeLists.txt** — 新增 `"peripherals/ina226.c"`

- [ ] **Step 5: Build verification**

Run: `idf.py build 2>&1 | grep -E "error:|warning:"`
Expected: 0 errors, 0 warnings

- [ ] **Step 6: Commit**

```bash
git add main/peripherals/ina226.c main/peripherals/ina226.h main/peripherals/peripherals.c main/CMakeLists.txt
git commit -m "feat: add INA226 power monitor driver"
```

---

### Task 6: 蜂鸣器驱动

**Files:**
- Create: `main/peripherals/buzzer.c`
- Create: `main/peripherals/buzzer.h`
- Modify: `main/peripherals/peripherals.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `device_type.h`, `driver/ledc.h`, `esp_timer.h`
- Produces: `buzzer_driver_register()`

- [ ] **Step 1: Write buzzer.h**

```c
#ifndef BUZZER_H
#define BUZZER_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t buzzer_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H
```

- [ ] **Step 2: Write buzzer.c**

关键实现：
- 使用 LEDC 低速通道（GPIO 输出）
- `init`: 配置 GPIO + LEDC 通道，初始化占空比 0（关闭）
- `write`: 解析 `{on, frequency, duty, duration_ms}`，动态更新 LEDC 频率和占空比，配置 `auto_off_ms` 时创建单次定时器
- `deinit`: 停止 LEDC 输出，删除定时器，释放内存
- `tick`: 检查定时器是否到期，到期则关闭蜂鸣器
- 避免重复配置（写入相同状态时跳过）

```c
/**
 * @file buzzer.c
 * @brief Passive buzzer driver (PWM via LEDC)
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "buzzer.h"
#include "device_manager.h"

static const char *TAG = "buzzer";

typedef struct {
    gpio_num_t gpio;
    ledc_channel_t channel;
    ledc_timer_t timer;
    uint32_t frequency;    // Hz
    uint8_t duty;          // %
    int auto_off_ms;
    int64_t auto_off_trigger_us;  // 0 = no timer pending
    esp_timer_handle_t auto_off_timer;
    bool on;
} buzzer_data_t;

static void buzzer_auto_off_callback(void *arg)
{
    device_t *dev = (device_t *)arg;
    buzzer_data_t *data = dev->driver_data;
    if (!data) return;

    data->on = false;
    data->auto_off_trigger_us = 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
    ESP_LOGD(TAG, "Buzzer auto-off triggered");
}

static esp_err_t buzzer_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    if (!cJSON_IsNumber(gpio_node)) return ESP_ERR_INVALID_ARG;

    buzzer_data_t *data = calloc(1, sizeof(buzzer_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->gpio = (gpio_num_t)gpio_node->valueint;
    data->frequency = 2000;
    data->duty = 50;
    data->auto_off_ms = 0;
    data->auto_off_trigger_us = 0;
    data->on = false;

    cJSON *freq = cJSON_GetObjectItem(config, "frequency");
    cJSON *duty = cJSON_GetObjectItem(config, "duty");
    cJSON *auto_off = cJSON_GetObjectItem(config, "auto_off_ms");

    if (cJSON_IsNumber(freq)) data->frequency = freq->valueint;
    if (cJSON_IsNumber(duty)) data->duty = duty->valueint;
    if (cJSON_IsNumber(auto_off)) data->auto_off_ms = auto_off->valueint;

    // Clamp frequency to valid range
    if (data->frequency < 100) data->frequency = 100;
    if (data->frequency > 10000) data->frequency = 10000;
    if (data->duty > 100) data->duty = 100;

    // Configure GPIO
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    // LEDC timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = data->frequency,
        .duty_resolution = LEDC_TIMER_10_BIT,  // 10-bit = 0-1023
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // LEDC channel
    data->channel = LEDC_CHANNEL_0;
    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = data->channel,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = data->gpio,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    // Auto-off timer
    if (data->auto_off_ms > 0) {
        esp_timer_create_args_t timer_args = {
            .callback = buzzer_auto_off_callback,
            .arg = dev,
            .name = "buzzer_auto_off",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &data->auto_off_timer));
    }

    dev->driver_data = data;
    ESP_LOGI(TAG, "Buzzer init: GPIO=%d freq=%dHz duty=%d%%", data->gpio, data->frequency, data->duty);
    return ESP_OK;
}

static esp_err_t buzzer_deinit(device_t *dev)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    // Stop and uninstall LEDC channel
    ledc_stop(LEDC_LOW_SPEED_MODE, data->channel, 0);
    ledc_del_channel(data->channel);

    // Cancel auto-off timer
    if (data->auto_off_timer) {
        esp_timer_stop(data->auto_off_timer);
        esp_timer_delete(data->auto_off_timer);
    }

    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t buzzer_read(device_t *dev, cJSON *value)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddBoolToObject(value, "on", data->on);
    cJSON_AddNumberToObject(value, "frequency", data->frequency);
    cJSON_AddNumberToObject(value, "duty", data->duty);

    if (data->auto_off_trigger_us > 0) {
        int64_t remaining = (data->auto_off_trigger_us - esp_timer_get_time()) / 1000;
        cJSON_AddNumberToObject(value, "remaining_ms", remaining > 0 ? remaining : 0);
    } else {
        cJSON_AddNumberToObject(value, "remaining_ms", 0);
    }
    return ESP_OK;
}

static esp_err_t buzzer_write(device_t *dev, const cJSON *value)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    if (!cJSON_IsObject(value)) return ESP_ERR_INVALID_ARG;

    cJSON *on_node = cJSON_GetObjectItem(value, "on");
    cJSON *freq_node = cJSON_GetObjectItem(value, "frequency");
    cJSON *duty_node = cJSON_GetObjectItem(value, "duty");
    cJSON *duration_node = cJSON_GetObjectItem(value, "duration_ms");

    if (!cJSON_IsBool(on_node)) return ESP_ERR_INVALID_ARG;
    bool on = cJSON_IsTrue(on_node);

    uint32_t freq = data->frequency;
    uint8_t duty = data->duty;
    int auto_off_ms = data->auto_off_ms;

    if (cJSON_IsNumber(freq_node)) freq = freq_node->valueint;
    if (cJSON_IsNumber(duty_node)) duty = duty_node->valueint;
    if (cJSON_IsNumber(duration_node)) auto_off_ms = duration_node->valueint;

    // Clamp
    if (freq < 100) freq = 100;
    if (freq > 10000) freq = 10000;
    if (duty > 100) duty = 100;

    // Cancel existing auto-off timer if active
    if (data->auto_off_timer && data->auto_off_trigger_us > 0) {
        esp_timer_stop(data->auto_off_timer);
        data->auto_off_trigger_us = 0;
    }

    if (on) {
        // Update frequency if changed
        if (freq != data->frequency) {
            ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
            data->frequency = freq;
        }

        // Update duty
        uint32_t duty_val = (duty * 1023) / 100;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, duty_val);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
        data->duty = duty;
        data->on = true;

        // Schedule auto-off
        if (auto_off_ms > 0 && data->auto_off_timer) {
            data->auto_off_trigger_us = esp_timer_get_time() + (int64_t)auto_off_ms * 1000;
            esp_timer_start_once(data->auto_off_timer, auto_off_ms * 1000);
        }

        ESP_LOGD(TAG, "Buzzer ON: freq=%dHz duty=%d%% auto_off=%dms", freq, duty, auto_off_ms);
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
        data->on = false;
        ESP_LOGD(TAG, "Buzzer OFF");
    }

    return ESP_OK;
}

static esp_err_t buzzer_tick(device_t *dev)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;
    // Timer expiry is handled by esp_timer callback
    return ESP_OK;
}

static esp_err_t buzzer_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "gpio", 21);
    cJSON_AddNumberToObject(config, "frequency", 2000);
    cJSON_AddNumberToObject(config, "duty", 50);
    cJSON_AddNumberToObject(config, "auto_off_ms", 0);
    return ESP_OK;
}

static const device_type_t buzzer_driver = {
    .name = "buzzer",
    .description = "Passive buzzer (PWM)",
    .capabilities = DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_READ,
    .init = buzzer_init,
    .deinit = buzzer_deinit,
    .read = buzzer_read,
    .write = buzzer_write,
    .tick = buzzer_tick,
    .get_default_config = buzzer_default_config,
};

esp_err_t buzzer_driver_register(void)
{
    return device_type_register(&buzzer_driver);
}
```

- [ ] **Step 3: Update peripherals.c** — 新增 `ESP_ERROR_CHECK(buzzer_driver_register());`

- [ ] **Step 4: Update CMakeLists.txt** — 新增 `"peripherals/buzzer.c"`

- [ ] **Step 5: Build verification**

Run: `idf.py build 2>&1 | grep -E "error:|warning:"`
Expected: 0 errors, 0 warnings

- [ ] **Step 6: Commit**

```bash
git add main/peripherals/buzzer.c main/peripherals/buzzer.h main/peripherals/peripherals.c main/CMakeLists.txt
git commit -m "feat: add passive buzzer driver (PWM)"
```

---

### Task 7: 更新文档

**Files:**
- Modify: `README.md`（外设类型表格新增 5 行）
- Modify: `docs/ARCHITECTURE.md`（外设文档节新增 5 个外设）
- Modify: `docs/TESTING.md`（测试用例新增）

- [ ] **Step 1: Update README.md**

在 `## Peripheral drivers` 表格中新增：

| Type | Capabilities | Config keys |
|---|---|---|
| `can` | read, write, notify | `tx_gpio`, `rx_gpio`, `bitrate`, `tx_queue_size`, `rx_queue_size` |
| `mcp4725` | write, read | `sda_gpio`, `scl_gpio`, `i2c_addr`, `vref_mv`, `scl_freq` |
| `ads1115` | read, periodic, notify | `sda_gpio`, `scl_gpio`, `i2c_addr`, `channel`, `gain`, `rate`, `interval_ms` |
| `ina226` | read, periodic, notify | `sda_gpio`, `scl_gpio`, `i2c_addr`, `r_shunt`, `max_current_ma`, `interval_ms` |
| `buzzer` | write, read | `gpio`, `frequency`, `duty`, `auto_off_ms` |

在 YAML 配置示例中新增示例。

- [ ] **Step 2: Update docs/ARCHITECTURE.md**

在驱动目录列表中新增 5 个外设的说明。

- [ ] **Step 3: Update docs/TESTING.md**

新增测试用例：
- CAN 帧发送/接收
- MCP4725 DAC 值设置与读取
- ADS1115 ADC 轮询读取
- INA226 功率监测读取
- 蜂鸣器开启/关闭/自动停止

- [ ] **Step 4: Commit**

```bash
git add README.md docs/ARCHITECTURE.md docs/TESTING.md
git commit -m "docs: add new peripherals to README and architecture docs"
```

---

### Task 8: 最终验证

- [ ] **Step 1: Clean build**

```bash
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build 2>&1 | grep -E "error:|warning:"
```

Expected: 0 errors, 0 warnings

- [ ] **Step 2: Host unit tests**

```bash
tests/run_yaml_tests.sh
```

Expected: ALL PASS

- [ ] **Step 3: Verify all 5 drivers are registered**

在构建输出中搜索注册日志，或检查 `device_type_count()` 返回值。

- [ ] **Step 4: Final commit**

```bash
git log --oneline -8
```
