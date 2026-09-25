# BSP (Board Support Package) 组件库

ESP32-S3 嵌入式开发板支持包，包含各类硬件驱动组件，采用面向对象设计（句柄模式）。

---

## 目录结构

```
bsp/
├── actuator_manager/   # 执行器管理器
├── button/            # 按键驱动
├── buzzer/            # 蜂鸣器驱动
├── dht11/             # DHT11/DHT22 温湿度传感器
├── hc595/             # 74HC595 移位寄存器
├── ads1115/           # ADS1115 16位 ADC
├── ina226/            # INA226 电流/电压监控
├── ir_rx/             # 红外接收驱动
├── ir_tx/             # 红外发射驱动
├── led_indicator/     # LED 指示灯
├── mcp4725/           # MCP4725 12位 DAC
├── relay/             # 继电器驱动
├── sensor_manager/    # 传感器管理器
├── tja1050/           # TJA1050 CAN 收发器
└── ws2812/            # WS2812 RGB LED
```

---

## 组件列表

| 组件 | 协议/接口 | 说明 |
|------|----------|------|
| [led_indicator](#1-led_indicator-led-指示灯) | GPIO/RMT | LED 状态指示 + WS2812 控制 |
| [ws2812](#2-ws2812-rgb-led) | RMT | WS2812 RGB LED 灯带驱动 |
| [relay](#3-relay-继电器) | GPIO | 继电器控制 (高/低电平有效) |
| [hc595](#4-hc595-移位寄存器) | GPIO | 74HC595 级联输出扩展 |
| [buzzer](#5-buzzer-蜂鸣器) | GPIO/LEDC | 有源/无源蜂鸣器 |
| [ir_tx](#6-ir_tx-红外发射) | RMT | NEC/RC5/Sony 协议发射 |
| [ir_rx](#7-ir_rx-红外接收) | RMT | NEC/RC5 协议解码 |
| [button](#8-button-按键) | GPIO | 按键去抖 + 长按/双击检测 |
| [tja1050](#9-tja1050-can-收发器) | GPIO | CAN 总线收发器 |
| [dht11](#10-dht11-温湿度传感器) | GPIO | DHT11/DHT22 单线通信 |
| [mcp4725](#11-mcp4725-dac) | I2C | 12位 DAC 输出 |
| [ads1115](#12-ads1115-adc) | I2C | 16位 4通道 ADC |
| [ina226](#13-ina226-电流电压监控) | I2C | 高侧电流/功率监测 |
| [actuator_manager](#14-actuator_manager-执行器管理) | - | 执行器统一管理接口 |
| [sensor_manager](#15-sensor_manager-传感器管理) | - | 传感器统一管理接口 |

---

## 详细说明

### 1. led_indicator (LED 指示灯)

LED 状态指示器，支持 GPIO 蓝色 LED 和 WS2812 RGB LED 共享引脚。

**文件**: `led_indicator/led_indicator.h`

```c
// 初始化
void led_indicator_init(int8_t gpio_num);

// 设置状态
void led_indicator_set_status(led_status_t status);
void led_indicator_set_pattern(led_pattern_t pattern, led_color_t color);

// 任务循环
void led_indicator_task(void);

// 独立控制
void blue_led_set(bool on);
```

**状态枚举**:
```c
typedef enum {
    LED_STATUS_BOOT,           // 启动中 - 蓝色闪烁
    LED_STATUS_NORMAL,         // 正常运行 - 绿色呼吸
    LED_STATUS_ERROR,          // 系统错误 - 红色快闪
    LED_STATUS_WIFI_CONNECTING, // WiFi连接中
    LED_STATUS_MQTT_CONNECTED, // MQTT已连接
    // ... 更多状态
} led_status_t;
```

---

### 2. ws2812 (RGB LED)

WS2812/WS2812B RGB LED 驱动，支持 1-256 个灯珠。

**文件**: `ws2812/ws2812.h`

```c
// 创建/销毁
ws2812_handle_t ws2812_create(const ws2812_config_t *config);
void ws2812_delete(ws2812_handle_t handle);
int ws2812_init(ws2812_handle_t handle);

// 控制
int ws2812_set_pixel(ws2812_handle_t handle, uint16_t index, const ws2812_color_t *color);
int ws2812_set_all(ws2812_handle_t handle, const ws2812_color_t *color);
int ws2812_set_pixel_rgb(ws2812_handle_t handle, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
int ws2812_set_all_rgb(ws2812_handle_t handle, uint8_t r, uint8_t g, uint8_t b);
int ws2812_set_brightness(ws2812_handle_t handle, uint8_t brightness);
int ws2812_clear(ws2812_handle_t handle);
int ws2812_show(ws2812_handle_t handle);  // 刷新显示

// 颜色转换
void ws2812_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, ws2812_color_t *color);
```

**配置示例**:
```c
ws2812_config_t cfg = {
    .gpio_num = 48,
    .led_count = 8,
    .brightness = 200
};
ws2812_handle_t ws = ws2812_create(&cfg);
ws2812_init(ws);
ws2812_set_all_rgb(ws, 255, 0, 0);  // 全红
ws2812_show(ws);
```

---

### 3. relay (继电器)

继电器驱动，支持高/低电平有效配置。

**文件**: `relay/relay.h`

```c
// 创建/销毁
relay_handle_t relay_create(const relay_config_t *config);
void relay_delete(relay_handle_t handle);
int relay_init(relay_handle_t handle);

// 控制
int relay_on(relay_handle_t handle);
int relay_off(relay_handle_t handle);
int relay_toggle(relay_handle_t handle);
int relay_set(relay_handle_t handle, bool on);
int relay_get_state(relay_handle_t handle, bool *state);
```

**配置**:
```c
typedef struct {
    int8_t gpio_num;                  // GPIO 引脚
    relay_active_level_t level;        // RELAY_ACTIVE_HIGH / RELAY_ACTIVE_LOW
    bool default_state;                // 默认状态
} relay_config_t;
```

---

### 4. hc595 (移位寄存器)

74HC595 级联移位寄存器驱动，最多支持 8 片级联 (64 位输出)。

**文件**: `hc595/hc595.h`

```c
// 创建/销毁
hc595_handle_t hc595_create(const hc595_config_t *config);
void hc595_delete(hc595_handle_t handle);
int hc595_init(hc595_handle_t handle);

// 控制
int hc595_set_bit(hc595_handle_t handle, uint8_t bit, bool state);
int hc595_set_value(hc595_handle_t handle, uint32_t value);
int hc595_clear(hc595_handle_t handle);
int hc595_set_all(hc595_handle_t handle);
int hc595_update(hc595_handle_t handle);      // 锁存输出
int hc595_write(hc595_handle_t handle, uint32_t value);  // 设置+锁存
int hc595_write_bit(hc595_handle_t handle, uint8_t bit, bool state);  // 单bit+锁存
```

**接线**:
```
ESP32      74HC595
GPIO5  --> SER     (数据)
GPIO6  --> RCLK    (锁存时钟)
GPIO7  --> SRCLK   (移位时钟)
GPIO8  --> SRCLR   (清零，可选)
```

---

### 5. buzzer (蜂鸣器)

支持有源蜂鸣器 (GPIO) 和无源蜂鸣器 (PWM 音调)。

**文件**: `buzzer/buzzer.h`

```c
// 创建/销毁
buzzer_handle_t buzzer_create(const buzzer_config_t *config);
void buzzer_delete(buzzer_handle_t handle);
int buzzer_init(buzzer_handle_t handle);

// 控制
int buzzer_on(buzzer_handle_t handle);
int buzzer_off(buzzer_handle_t handle);
int buzzer_set_freq(buzzer_handle_t handle, uint32_t freq);
int buzzer_tone(buzzer_handle_t handle, uint32_t freq, uint32_t duration_ms);
int buzzer_beep(buzzer_handle_t handle, uint32_t on_ms, uint32_t off_ms, uint8_t count);
int buzzer_play_note(buzzer_handle_t handle, uint8_t note, uint32_t duration_ms);

// 音符宏
#define BUZZER_NOTE_C4  60
#define BUZZER_NOTE_D4  62
#define BUZZER_NOTE_E4  64
#define BUZZER_NOTE_F4  65
#define BUZZER_NOTE_G4  67
#define BUZZER_NOTE_A4  69
#define BUZZER_NOTE_B4  71
#define BUZZER_NOTE_C5  72

// 频率转换
uint32_t buzzer_note_to_freq(uint8_t note);
```

**示例**:
```c
buzzer_config_t cfg = { .gpio_num = 15, .type = BUZZER_TYPE_PASSIVE };
buzzer_handle_t bz = buzzer_create(&cfg);
buzzer_init(bz);

// 哔哔声
buzzer_beep(bz, 200, 100, 3);

// 播放音符
buzzer_play_note(bz, BUZZER_NOTE_C4, 500);  // 播放 C4 音符 500ms
```

---

### 6. ir_tx (红外发射)

NEC/RC5/Sony SIRC 协议红外发射。

**文件**: `ir_tx/ir_tx.h`

```c
// 创建/销毁
ir_tx_handle_t ir_tx_create(const ir_tx_config_t *config);
void ir_tx_delete(ir_tx_handle_t handle);
int ir_tx_init(ir_tx_handle_t handle);

// 发送
int ir_tx_send_raw(ir_tx_handle_t handle, const uint32_t *timings, uint16_t count);
int ir_tx_send_nec(ir_tx_handle_t handle, uint16_t address, uint8_t command);
int ir_tx_send_nec_extended(ir_tx_handle_t handle, uint16_t address, uint8_t command);
int ir_tx_send_rc5(ir_tx_handle_t handle, uint8_t address, uint8_t command);
int ir_tx_send_sony(ir_tx_handle_t handle, uint8_t address, uint8_t command, uint8_t extended);
int ir_tx_send_nec_repeat(ir_tx_handle_t handle);
```

---

### 7. ir_rx (红外接收)

NEC/RC5 协议红外解码，支持事件队列和回调。

**文件**: `ir_rx/ir_rx.h`

```c
// 创建/销毁
ir_rx_handle_t ir_rx_create(const ir_rx_config_t *config);
void ir_rx_delete(ir_rx_handle_t handle);
int ir_rx_init(ir_rx_handle_t handle);
int ir_rx_start(ir_rx_handle_t handle);
int ir_rx_stop(ir_rx_handle_t handle);

// 接收
typedef struct {
    ir_protocol_t protocol;
    uint16_t address;
    uint8_t command;
    uint32_t timestamp;
} ir_command_t;

int ir_rx_wait_command(ir_rx_handle_t handle, ir_command_t *cmd, uint32_t timeout_ms);
int ir_rx_check_command(ir_rx_handle_t handle, ir_command_t *cmd);

// 回调
typedef void (*ir_rx_callback_t)(const ir_command_t *cmd, void *user_data);
int ir_rx_register_callback(ir_rx_handle_t handle, ir_rx_callback_t callback, void *user_data);
```

---

### 8. button (按键)

按键驱动，支持去抖、长按检测、双击检测，内置 Button Manager 管理多个按键。

**文件**: `button/button.h`

```c
// 单按键
button_handle_t button_create(const button_config_t *config);
void button_delete(button_handle_t handle);
int button_init(button_handle_t handle);
void button_poll(button_handle_t handle);  // 周期性调用

// 事件
typedef enum {
    BUTTON_EVENT_PRESSED,
    BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_CLICKED,
    BUTTON_EVENT_LONG_PRESSED,
    BUTTON_EVENT_LONG_RELEASED,
    BUTTON_EVENT_DOUBLE_CLICK,
} button_event_type_t;

int button_wait_event(button_handle_t handle, button_event_t *event, uint32_t timeout_ms);
int button_check_event(button_handle_t handle, button_event_t *event);

// 回调
typedef void (*button_callback_t)(const button_event_t *event, void *user_data);
int button_register_callback(button_handle_t handle, button_callback_t callback, void *user_data);

// 多按键管理器
button_manager_handle_t button_manager_create(const button_manager_config_t *config);
void button_manager_delete(button_manager_handle_t handle);
button_handle_t button_manager_add_button(button_manager_handle_t handle, const button_config_t *config);
void button_manager_poll(button_manager_handle_t handle);
int button_manager_wait_event(button_manager_handle_t handle, button_event_t *event, uint32_t timeout_ms);
```

**配置**:
```c
typedef struct {
    uint8_t button_id;
    int8_t gpio_num;
    button_active_level_t active_level;      // BUTTON_ACTIVE_LOW / HIGH
    button_pull_mode_t pull_mode;            // BUTTON_PULL_UP / DOWN / NONE
    uint32_t debounce_ms;                   // 去抖时间 (默认50ms)
    uint32_t long_press_ms;                 // 长按阈值 (默认1000ms)
    uint32_t double_click_ms;               // 双击间隔 (默认300ms)
} button_config_t;
```

---

### 9. tja1050 (CAN 收发器)

TJA1050 CAN 总线收发器，支持 NORMAL/STANDBY/SILENT 模式。

**文件**: `tja1050/tja1050.h`

```c
// 创建/销毁
tja1050_handle_t tja1050_create(const tja1050_config_t *config);
void tja1050_delete(tja1050_handle_t handle);
int tja1050_init(tja1050_handle_t handle);

// 控制
int tja1050_set_mode(tja1050_handle_t handle, tja1050_mode_t mode);
int tja1050_get_mode(tja1050_handle_t handle, tja1050_mode_t *mode);

// 模式
typedef enum {
    TJA1050_MODE_NORMAL,   // 正常模式
    TJA1050_MODE_STANDBY,  // 低功耗待机
    TJA1050_MODE_SILENT,   // 静默/监听模式
} tja1050_mode_t;
```

---

### 10. dht11 (温湿度传感器)

DHT11/DHT12/DHT21/DHT22 单线温湿度传感器。

**文件**: `dht11/dht11.h`

```c
// 创建/销毁
dht_handle_t dht_create(const dht_config_t *config);
void dht_delete(dht_handle_t handle);
int dht_init(dht_handle_t handle);

// 读取
typedef struct {
    float temperature;    // 摄氏温度
    float humidity;        // 相对湿度 %
    uint32_t timestamp;
    bool valid;
} dht_data_t;

int dht_read(dht_handle_t handle, dht_data_t *data);
int dht_read_temperature(dht_handle_t handle, float *temperature);
int dht_read_humidity(dht_handle_t handle, float *humidity);

// 传感器类型
typedef enum {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_DHT12,
    DHT_TYPE_DHT21,
    DHT_TYPE_DHT22,
} dht_type_t;
```

---

### 11. mcp4725 (DAC)

MCP4725 12 位 DAC，I2C 接口，支持内部/外部参考电压。

**文件**: `mcp4725/mcp4725.h`

```c
// 创建/销毁
mcp4725_handle_t mcp4725_create(const mcp4725_config_t *config);
void mcp4725_delete(mcp4725_handle_t handle);
int mcp4725_init(mcp4725_handle_t handle);

// 控制
int mcp4725_set_value(mcp4725_handle_t handle, uint16_t value, bool update_now);  // 0-4095
int mcp4725_set_voltage(mcp4725_handle_t handle, float voltage, bool update_now);  // 0-2.048V
int mcp4725_get_voltage(mcp4725_handle_t handle, float *voltage);
int mcp4725_save_to_eeprom(mcp4725_handle_t handle, uint16_t value);  // 保存到 EEPROM
```

**地址选项**:
```c
typedef enum {
    MCP4725_ADDR_A00 = 0x60,  // A0 = GND
    MCP4725_ADDR_A01 = 0x61,  // A0 = VCC
} mcp4725_addr_t;
```

---

### 12. ads1115 (ADC)

ADS1115 16 位 4 通道 ADC，支持单端和差分输入。

**文件**: `ads1115/ads1115.h`

```c
// 创建/销毁
ads1115_handle_t ads1115_create(const ads1115_config_t *config);
void ads1115_delete(ads1115_handle_t handle);
int ads1115_init(ads1115_handle_t handle);

// 读取
typedef struct {
    int16_t raw;           // 原始值
    float voltage;         // 电压值
    ads1115_mux_t mux;     // 通道
    uint32_t timestamp;
    bool valid;
} ads1115_data_t;

int ads1115_read(ads1115_handle_t handle, ads1115_mux_t mux, ads1115_data_t *data);
int ads1115_read_channel(ads1115_handle_t handle, uint8_t channel, float *voltage);  // 0-3
int ads1115_read_differential(ads1115_handle_t handle, ads1115_mux_t mux, float *voltage);

// 可编程增益
typedef enum {
    ADS1115_PGA_6_144V,  // ±6.144V
    ADS1115_PGA_4_096V,  // ±4.096V
    ADS1115_PGA_2_048V,  // ±2.048V (默认)
    ADS1115_PGA_1_024V,  // ±1.024V
    ADS1115_PGA_0_512V,  // ±0.512V
    ADS1115_PGA_0_256V,  // ±0.256V
} ads1115_pga_t;

// 数据率
typedef enum {
    ADS1115_DR_8_SPS,
    ADS1115_DR_16_SPS,
    ADS1115_DR_32_SPS,
    ADS1115_DR_64_SPS,
    ADS1115_DR_128_SPS,  // 默认
    ADS1115_DR_250_SPS,
    ADS1115_DR_475_SPS,
    ADS1115_DR_860_SPS,
} ads1115_dr_t;
```

---

### 13. ina226 (电流电压监控)

INA226 高侧电流/电压/功率监控，I2C 接口。

**文件**: `ina226/ina226.h`

```c
// 创建/销毁
ina226_handle_t ina226_create(const ina226_config_t *config);
void ina226_delete(ina226_handle_t handle);
int ina226_init(ina226_handle_t handle);

// 读取
typedef struct {
    float bus_voltage;     // 总线电压 (V)
    float shunt_voltage;   // 分流电压 (mV)
    float current_amp;    // 电流 (A)
    float power_watt;     // 功率 (W)
    uint32_t timestamp;
    bool valid;
} ina226_data_t;

int ina226_read(ina226_handle_t handle, ina226_data_t *data);
int ina226_read_bus_voltage(ina226_handle_t handle, float *voltage);
int ina226_read_shunt_voltage(ina226_handle_t handle, float *voltage);
int ina226_read_current(ina226_handle_t handle, float *current);
int ina226_read_power(ina226_handle_t handle, float *power);

// 校准
int ina226_set_calibration(ina226_handle_t handle, float r_shunt, float max_current);
```

**连接方式**:
```
V+ ----[R_shunt]----+---- V- ----> 负载
                      |
                    INA226
                    V+  V-
                      |
                    GND
```

---

### 14. actuator_manager (执行器管理)

执行器统一管理接口，通过函数指针注册和控制。

**文件**: `actuator_manager/actuator_manager.h`

```c
// 初始化
int actuator_manager_init(void);

// 注册
typedef int (*actuator_set_fn)(int value);

typedef struct {
    char name[32];
    actuator_type_t type;      // ACTUATOR_TYPE_LED / RELAY / MOTOR / SERVO / CUSTOM
    actuator_set_fn set_fn;
    int min_value;
    int max_value;
} actuator_config_t;

int actuator_register(const actuator_config_t *config);

// 控制
int actuator_set(const char *name, int value);
int actuator_get(const char *name, int *value);
```

---

### 15. sensor_manager (传感器管理)

传感器统一管理接口，支持数据校准。

**文件**: `sensor_manager/sensor_manager.h`

```c
// 初始化
int sensor_manager_init(void);

// 注册
typedef int (*sensor_read_fn)(float *value);

typedef struct {
    char name[32];
    sensor_type_t type;        // SENSOR_TYPE_TEMPERATURE / HUMIDITY / DOOR / ...
    sensor_read_fn read_fn;
    char unit[8];              // "°C", "%RH", etc.
    float calibration_offset;
    float calibration_scale;
} sensor_config_t;

int sensor_register(const sensor_config_t *config);

// 读取
int sensor_read(const char *name, float *value);
int sensor_read_all(sensor_data_t *data, int max_count, int *actual_count);

// 校准
int sensor_set_calibration(const char *name, float offset, float scale);
```

---

## 驱动对比

### GPIO 类
| 驱动 | 引脚 | 特点 |
|------|------|------|
| `relay` | 单 GPIO | 开关控制 |
| `buzzer` | 单 GPIO | 有源/无源 |
| `hc595` | 3-4 GPIO | 扩展输出 |
| `dht11` | 单 GPIO | 单线通信 |

### RMT 类
| 驱动 | 说明 |
|------|------|
| `ws2812` | RGB LED 时序 |
| `ir_tx` | 红外发射 |
| `ir_rx` | 红外接收 |

### I2C 类
| 驱动 | 地址 | 说明 |
|------|------|------|
| `mcp4725` | 0x60-0x61 | 12位 DAC |
| `ads1115` | 0x48-0x4B | 16位 ADC |
| `ina226` | 0x40-0x45 | 功率监测 |

---

## 使用建议

### 初始化顺序
```c
// 1. 驱动创建
ws2812_handle_t ws = ws2812_create(&ws_cfg);
relay_handle_t r = relay_create(&relay_cfg);
buzzer_handle_t bz = buzzer_create(&buzzer_cfg);

// 2. 驱动初始化
ws2812_init(ws);
relay_init(r);
buzzer_init(bz);

// 3. 使用
ws2812_set_all_rgb(ws, 255, 0, 0);
relay_on(r);
buzzer_beep(bz, 100, 100, 2);

// 4. 销毁
ws2812_delete(ws);
relay_delete(r);
buzzer_delete(bz);
```

### 资源释放
所有驱动支持 `*_delete()` 自动释放资源，包括:
- GPIO 引脚复位
- RMT 通道释放
- 内存释放

### 错误处理
```c
ws2812_handle_t ws = ws2812_create(&cfg);
if (ws == NULL) {
    ESP_LOGE(TAG, "Failed to create WS2812");
    return;
}

int ret = ws2812_init(ws);
if (ret != 0) {
    ESP_LOGE(TAG, "WS2812 init failed: %d", ret);
}
```

---

## 扩展开发

### 添加新驱动
1. 在 `components/bsp/` 下创建目录
2. 实现 `*_create()`, `*_init()`, `*_delete()` 接口
3. 使用 `idf_component_register()` 注册组件
4. 遵循命名约定: `{driver}.h`, `{driver}.c`, `CMakeLists.txt`

### 面向对象模式
```c
// 句柄结构体 (不透明指针)
typedef struct driver_handle_s *driver_handle_t;

// 公共接口
driver_handle_t driver_create(const driver_config_t *config);
void driver_delete(driver_handle_t handle);
int driver_init(driver_handle_t handle);
```

---

## 许可证

继承 ESP-IDF 开源协议。
