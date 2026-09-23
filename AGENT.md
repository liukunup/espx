# ESP32-S3 IoT Firmware Agent Guide

## 项目概述

- **项目路径**: `/Users/liukunup/Documents/repo/GitHub/espx`
- **目标芯片**: ESP32-S3 (QFN56, 8MB PSRAM)
- **MAC地址**: `c0:4e:30:3a:c3:74`
- **串口端口**: `/dev/cu.usbmodem14201`
- **框架**: ESP-IDF v6.1

## 硬件配置

| 功能 | GPIO | 说明 |
|------|------|------|
| 蓝色 LED | GPIO21 | 活跃低电平驱动 |
| WS2812 RGB LED | GPIO21 | 与蓝色 LED 共享引脚 |
| 恢复出厂设置 | GPIO12 | 启动时检测，低电平有效 |

### LED 指示灯状态

| 状态 | 颜色 | 模式 |
|------|------|------|
| 启动中 | 蓝色 | 慢闪 |
| 正常运行 | 绿色 | 呼吸灯 |
| 系统错误 | 红色 | 快闪 |
| WiFi 连接中 | 蓝色 | 慢闪 |
| WiFi 已连接 | 绿色 | 常亮 |
| 配网开始 | 黄色 | 慢闪 |
| MQTT 连接中 | 青色 | 慢闪 |

## 编译和烧录

### 环境变量

```bash
export PATH=/usr/local/Cellar/cmake/4.3.4/bin:$PATH
export IDF_PATH=~/.espressif/v6.1/esp-idf
export IDF_TOOLS_PATH=~/.espressif
export IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf6.1_py3.13_env
export PATH=$IDF_TOOLS_PATH/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$IDF_TOOLS_PATH/tools/ninja/1.12.1:$PATH
export CMAKE_MAKE_PROGRAM=$IDF_TOOLS_PATH/tools/ninja/1.12.1/ninja
```

### 编译步骤

```bash
cd /Users/liukunup/Documents/repo/GitHub/espx/build
rm -rf *  # 清理构建目录（可选）
cmake -G Ninja -DIDF_TARGET=esp32s3 -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### 烧录步骤

```bash
# 1. 烧录 bootloader
esptool --port /dev/cu.usbmodem14201 write-flash 0x0 build/bootloader/bootloader.bin

# 2. 烧录分区表
esptool --port /dev/cu.usbmodem14201 write-flash 0x8000 build/partition_table/partition-table.bin

# 3. 烧录应用程序
esptool --port /dev/cu.usbmodem14201 -b 921600 write-flash 0x10000 build/sample_project_cpp.bin
```

### 快速编译烧录

```bash
# 编译
export PATH=/usr/local/Cellar/cmake/4.3.4/bin:$PATH
export IDF_PATH=~/.espressif/v6.1/esp-idf
export IDF_TOOLS_PATH=~/.espressif
export IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf6.1_py3.13_env
export PATH=$IDF_TOOLS_PATH/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$IDF_TOOLS_PATH/tools/ninja/1.12.1:$PATH
cd build && ninja

# 烧录
esptool --port /dev/cu.usbmodem14201 -b 921600 write-flash 0x10000 build/sample_project_cpp.bin
```

## 组件架构

```
┌─────────────────────────────────────────────────────────────┐
│                        app_main.c                           │
│  • GPIO12 工厂恢复检测  • 组件初始化  • 主任务循环        │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│led_indicator  │   │network_mgr   │   │wifi_prov     │
│ • LED 状态   │   │ • WiFi 连接  │   │ • AP 配网    │
│ • WS2812    │   │ • 事件处理   │   │ • HTTP 服务器 │
└───────────────┘   └───────────────┘   └───────────────┘
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│emqx_client    │   │ota_manager    │   │remote_cmd     │
│ • MQTT 发布   │   │ • OTA 升级    │   │ • 命令处理    │
│ • 订阅主题   │   │ • 版本检查    │   │ • JSON 解析   │
└───────────────┘   └───────────────┘   └───────────────┘
        │
        ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│config_manager │   │health_monitor │   │sensor_mgr     │
│ • NVS 存储   │   │ • 心跳监控    │   │ • 传感器数据   │
│ • WiFi 凭证  │   │ • 重连逻辑    │   │               │
└───────────────┘   └───────────────┘   └───────────────┘
```

## 核心组件

### 1. led_indicator (LED 指示灯)

**文件**: `components/led_indicator/led_indicator.c`

**API**:
```c
void led_indicator_init(int8_t gpio_num);
void led_indicator_set_status(led_status_t status);
void led_indicator_set_pattern(led_pattern_t pattern, led_color_t color);
void led_indicator_task(void);
void blue_led_set(bool on);           // 独立控制蓝色 LED
void ws2812_set_color(uint32_t color); // 独立控制 WS2812 RGB
void ws2812_off(void);                // 关闭 WS2812
```

### 2. wifi_provisioning (WiFi 配网)

**文件**: `components/wifi_provisioning/wifi_provisioning.c`

**流程**:
1. 启动 SoftAP (热点名称: `ESP32-XXXX`)
2. 运行 HTTP 服务器
3. 接收 WiFi 凭证
4. 保存到 NVS 并重启

### 3. network_manager (网络管理)

**文件**: `components/network_manager/network_manager.c`

**API**:
```c
int network_connect(const char *ssid, const char *password);
int network_wait_connected(uint32_t timeout_ms);
bool network_is_connected(void);
```

### 4. emqx_client (MQTT 客户端)

**文件**: `components/emqx_client/emqx_client.c`

**功能**:
- 连接 EMQX MQTT Broker
- 发布遥测数据
- 订阅命令主题

### 5. ota_manager (OTA 升级)

**文件**: `components/ota_manager/ota_manager.c`

**功能**:
- 检查固件版本
- 从服务器下载新固件
- 执行 OTA 升级

## 恢复出厂设置

### GPIO12 工厂恢复流程

```
1. 设备上电
2. 检测 GPIO12 状态
3. 如果 GPIO12 被按住:
   ├── 计时 5 秒 (红色快闪提示)
   ├── 5 秒后进入等待释放状态 (绿色闪烁)
   └── 释放后执行恢复
       ├── 红色快闪 1 秒
       ├── 擦除 NVS 分区
       └── 重启设备
4. 如果 GPIO12 未被按住:
   └── 正常启动
```

### 关键代码位置

**文件**: `main/app_main.c`

```c
#define FACTORY_RESET_GPIO GPIO_NUM_12

static void check_factory_reset(void) {
    gpio_reset_pin(FACTORY_RESET_GPIO);
    gpio_set_direction(FACTORY_RESET_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(FACTORY_RESET_GPIO);
    
    // Phase 1: 等待 5 秒按下
    // Phase 2: 等待释放
    // Phase 3: 擦除 NVS 并重启
}
```

## 调试和日志

### 串口配置

- **波特率**: 115200
- **数据位**: 8
- **停止位**: 1
- **流控**: 无

### 日志级别

在 `sdkconfig` 中配置:
```
CONFIG_LOG_DEFAULT_LEVEL_INFO=y      # 默认 INFO 级别
CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y  # 最大 VERBOSE
```

### 查看日志

```bash
# 使用 esptool 读取
python3 -c "
import serial, time
ser = serial.Serial('/dev/cu.usbmodem14201', 115200, timeout=3)
time.sleep(2)
data = b''
while ser.in_waiting:
    data += ser.read()
print(data.decode('utf-8', errors='replace'))
"
```

## 常见问题

### 1. 编译超时

如果 `ninja` 命令超时，确保:
- 使用 cmake 4.3.4: `export PATH=/usr/local/Cellar/cmake/4.3.4/bin:$PATH`
- 正确设置 `CMAKE_MAKE_PROGRAM`

### 2. 串口无输出

- 检查 USB 连接
- 尝试断开并重新连接设备
- 使用 esptool 验证固件已烧录

### 3. 恢复出厂循环

确保 GPIO12 检测逻辑包含"等待释放"步骤，避免用户按住按钮不放导致循环重启。

## 项目文件结构

```
espx/
├── main/
│   ├── app_main.c          # 应用入口
│   └── CMakeLists.txt
├── components/
│   ├── led_indicator/      # LED 指示灯
│   ├── wifi_provisioning/  # WiFi 配网
│   ├── network_manager/    # 网络管理
│   ├── emqx_client/       # MQTT 客户端
│   ├── ota_manager/        # OTA 升级
│   ├── remote_cmd/        # 远程命令
│   ├── config_manager/    # 配置管理
│   ├── health_monitor/    # 健康监控
│   ├── sensor_manager/    # 传感器管理
│   └── actuator_manager/  # 执行器管理
├── components/             # ESP-IDF 组件
├── build/                 # 构建输出
├── partitions.csv          # 分区表
├── sdkconfig             # ESP-IDF 配置
└── AGENT.md              # 本文件
```

## 后续开发建议

1. **添加传感器驱动**: 实现 `sensor_manager` 组件
2. **完善 MQTT 协议**: 定义遥测和命令主题格式
3. **增加安全特性**: TLS 证书验证、OTA 签名验证
4. **单元测试**: 完善 `tests/unit/` 目录下的测试用例
