# ESP32 IoT 固件开发指南

本文档介绍如何在此项目中开发新功能。

## 目录

- [开发环境](#开发环境)
- [编译烧录](#编译烧录)
- [添加新组件](#添加新组件)
- [组件示例：LED指示灯](#组件示例led指示灯)
- [调试技巧](#调试技巧)

---

## 开发环境

### 工具链

```bash
# ESP-IDF v6.1
export IDF_PATH=/Users/liukunup/.espressif/v6.1/esp-idf
export IDF_PYTHON_ENV_PATH=/Users/liukunup/.espressif/python_env/idf6.0_py3.13_env
export PATH=/Users/liukunup/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$IDF_PATH/tools:$PATH
```

### 激活环境

```bash
cd /path/to/espx
source /Users/liukunup/.espressif/python_env/idf6.0_py3.13_env/bin/activate
```

---

## 编译烧录

### 1. 清理并配置

```bash
# 切换目标芯片 (esp32/esp32s3/esp32c3/esp32c6)
idf.py set-target esp32s3

# 或手动配置
rm -rf build
mkdir build
cp sdkconfig.defaults.esp32s3 build/sdkconfig
cd build
cmake -G 'Unix Makefiles' -DIDF_TARGET=esp32s3 ..
```

### 2. 编译

```bash
# 在 build 目录下
make -j8
```

### 3. 烧录

```bash
# 完整烧录（bootloader + 分区表 + 固件）
python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
  --chip esp32s3 --port /dev/tty.usbmodem14201 --baud 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 8MB --flash-freq 80m \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/esp32-iot-firmware.bin

# 仅烧录固件（更快）
python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
  --chip esp32s3 --port /dev/tty.usbmodem14201 --baud 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 8MB --flash-freq 80m \
  0x10000 build/esp32-iot-firmware.bin

# 擦除Flash
python3 $IDF_PATH/components/esptool_py/esptool/esptool.py \
  --chip esp32s3 --port /dev/tty.usbmodem14201 erase_flash
```

### 4. 监控日志

```bash
python3 $IDF_PATH/tools/idf_monitor.py \
  -p /dev/tty.usbmodem14201 --toolchain-prefix xtensa-esp32s3-elf- \
  --target esp32s3 build/esp32-iot-firmware.elf
```

---

## 添加新组件

### 1. 创建组件目录

```
components/
  └── my_component/
        ├── CMakeLists.txt
        ├── my_component.h
        └── my_component.c
```

### 2. 编写 CMakeLists.txt

```cmake
idf_component_register(
    SRCS "my_component.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_driver_gpio    # 依赖的组件
        freertos
)
```

### 3. 编写头文件 (.h)

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 枚举类型
typedef enum {
    MY_STATE_IDLE = 0,
    MY_STATE_RUNNING,
} my_state_t;

// 函数声明
void my_component_init(void);
void my_component_set_state(my_state_t state);
bool my_component_is_ready(void);

#ifdef __cplusplus
}
#endif
```

### 4. 编写源文件 (.c)

```c
#include "my_component.h"
#include "esp_log.h"

static const char *TAG = "my_component";
static my_state_t s_state = MY_STATE_IDLE;

void my_component_init(void) {
    ESP_LOGI(TAG, "Initializing...");
    s_state = MY_STATE_IDLE;
}

void my_component_set_state(my_state_t state) {
    s_state = state;
    ESP_LOGI(TAG, "State changed to %d", state);
}

bool my_component_is_ready(void) {
    return s_state == MY_STATE_IDLE;
}
```

### 5. 在 app_main.c 中使用

```c
#include "my_component.h"

void app_init(void) {
    // 初始化组件
    my_component_init();
}
```

### 6. 重新编译

修改 `main/CMakeLists.txt`，添加新组件：

```cmake
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        log_system
        my_component  # 添加这里
        # ... 其他组件
)
```

---

## 组件示例：LED指示灯

### 功能需求

- 使用WS2812 RGB LED显示状态
- 支持多种灯语：常亮、闪烁、呼吸灯
- 分时复用GPIO21（同时接蓝色LED和WS2812）

### 实现要点

#### 1. 分时复用GPIO

```c
// GPIO模式（控制蓝色LED，低电平点亮）
static void switch_to_gpio_mode(void) {
    rmt_disable(s_rmt_channel);
    gpio_reset_pin(s_led_gpio);
    gpio_set_direction(s_led_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_led_gpio, 1);  // 高电平灭灯
}

// RMT模式（控制WS2812）
static void switch_to_rmt_mode(void) {
    gpio_reset_pin(s_led_gpio);
    rmt_enable(s_rmt_channel);
    s_ws2812_mode = true;
}
```

#### 2. RMT驱动WS2812

```c
// 初始化RMT通道
rmt_tx_channel_config_t tx_chan_config = {
    .gpio_num = 21,
    .clk_src = RMT_CLK_SRC_APB,
    .resolution_hz = 80 * 1000 * 1000,  // 80MHz
    .mem_block_symbols = 64,
};
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel));

// 创建字节编码器
rmt_bytes_encoder_config_t encoder_config = {
    .bit0 = {.duration0 = 30, .level0 = 1, .duration1 = 80, .level1 = 0},  // T0H+T0L
    .bit1 = {.duration0 = 60, .level0 = 1, .duration1 = 50, .level1 = 0},  // T1H+T1L
};
ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_config, &s_bytes_encoder));
```

#### 3. 灯语模式

```c
typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,      // 1Hz
    LED_BLINK_FAST,      // 4Hz
    LED_BREATHE,         // 呼吸灯
} led_pattern_t;

static uint8_t get_brightness(void) {
    switch (s_pattern) {
        case LED_OFF:    return 0;
        case LED_ON:     return 255;
        case LED_BLINK_SLOW:
            return (s_tick_count % 1000) < 500 ? 255 : 0;
        case LED_BREATHE: {
            uint32_t t = s_tick_count % 2000;
            return (t < 1000) ? (t * 255 / 1000) : ((2000 - t) * 255 / 1000);
        }
        default: return 0;
    }
}
```

---

## 调试技巧

### 1. 串口日志

```bash
# 实时查看日志
idf.py monitor

# 或使用 esptool
python3 $IDF_PATH/tools/idf_monitor.py -p /dev/tty.usbmodem14201

# 过滤特定标签
idf.py monitor --tag "wifi_provisioning"
```

### 2. 日志级别

```c
#include "esp_log.h"

ESP_LOGV(TAG, "Verbose: %d", value);  // Verbose
ESP_LOGD(TAG, "Debug: %d", value);    // Debug
ESP_LOGI(TAG, "Info: %d", value);    // Info
ESP_LOGW(TAG, "Warn: %d", value);    // Warning
ESP_LOGE(TAG, "Error: %d", value);   // Error
```

### 3. 常见问题

#### 编译错误：No such file or directory

检查 `CMakeLists.txt` 中的 `REQUIRES` 是否包含所需组件。

```cmake
# 错误
REQUIRES driver

# 正确（ESP-IDF v6.1）
REQUIRES esp_driver_gpio esp_driver_rmt
```

#### 芯片不匹配

烧录时 `--chip` 参数必须与实际芯片匹配：
- ESP32 → `--chip esp32`
- ESP32-S3 → `--chip esp32s3`
- ESP32-C3 → `--chip esp32c3`

#### Flash烧录失败

```bash
# 擦除整个Flash
esptool.py --chip esp32s3 erase_flash

# 检查串口是否被占用
lsof /dev/tty.usbmodem14201
```

### 4. GDB调试

```bash
# 启动OpenOCD
openocd -f interface/esp-usb.cfg -f target/esp32s3.cfg

# 在另一个终端
xtensa-esp32s3-elf-gdb build/esp32-iot-firmware.elf
```

---

## 代码规范

### 命名约定

- 变量：`s_` 前缀表示静态变量
- 函数：`module_name_` 前缀
- 枚举值：`MODULE_STATE_` 前缀

```c
static int8_t s_led_gpio = -1;
static bool s_initialized = false;

void led_indicator_init(int8_t gpio_num);
typedef enum {
    LED_STATUS_BOOT = 0,
    LED_STATUS_NORMAL,
} led_status_t;
```

### 头文件保护

```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 代码

#ifdef __cplusplus
}
#endif
```

---

## 下一步

- 添加传感器驱动
- 实现MQTT通信
- 添加OTA升级功能
- 集成云平台

祝开发愉快！🚀
