# ESPX 外设驱动扩展规范

> **Spec ID:** 2025-09-27-peripherals-design
> **Author:** ESPX Agent
> **Status:** Draft → Pending Review
> **Implements:** TJA1050 (CAN), MCP4725 (DAC), ADS1115 (ADC), INA226 (功率监测), 蜂鸣器

---

## 1. 概述与目标

为 ESPX 固件新增 5 个外设驱动，遵循现有 `device_type_t` + `device_manager` 架构，无需修改核心代码。新增外设通过配置声明即可使用，自动获得 MQTT、WebSocket、AT、REST API 等所有通道的完整支持。

**设计约束：**
- 所有驱动实现为 `main/peripherals/` 下的独立 `.c/.h` 文件对
- 通过 `peripherals_register_all()` 统一注册
- 驱动之间无耦合，各自独立初始化所需总线（I2C/TWAI/GPIO）
- 不在驱动层做策略决策（不决定何时上报、如何告警）
- 驱动 `tick` 不阻塞，轮询任务每 100ms 调用一次

---

## 2. TJA1050 CAN 驱动

### 2.1 硬件说明

TJA1050 是 CAN 收发器，需配合 ESP32-S3 内置 TWAI 控制器使用。ESP32-S3 支持标准 CAN 2.0（ISO 11898），最高 1 Mbps。

### 2.2 设备类型

- **类型名：** `can`
- **能力位：** `READ | WRITE | NOTIFY`

### 2.3 配置项

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `tx_gpio` | int | 必填 | TWAI TX 引脚（GPIO 号） |
| `rx_gpio` | int | 必填 | TWAI RX 引脚（GPIO 号） |
| `bitrate` | int | 500000 | 波特率（125k/250k/500k/1M） |
| `tx_queue_size` | int | 5 | 发送队列深度 |
| `rx_queue_size` | int | 10 | 接收队列深度 |

### 2.4 数据格式

**写入 (WRITE):**
```json
{
  "id": 1234,
  "data": [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08],
  "ext": false,
  "rtr": false
}
```
- `id`: CAN 帧 ID（11-bit 标准帧或 29-bit 扩展帧）
- `data`: 数据字节数组，最多 8 字节
- `ext`: 是否为扩展帧（默认 false）
- `rtr`: 是否为远程帧（默认 false）

**读取 (READ / NOTIFY):**
```json
{
  "id": 1234,
  "data": [0x01, 0x02],
  "ext": false,
  "rtr": false,
  "timestamp_ms": 123456
}
```

### 2.5 实现要点

- 使用 `driver/twai.h` 内置 TWAI 驱动
- 初始化时配置 GPIO 引脚复用为 TWAI 功能
- `tick` 中非阻塞检查接收队列（`twai_receive`），有帧则通过事件总线发布
- 发送失败返回 `ESP_ERR_NOT_SUPPORTED` 以外的错误码
- 支持多帧并发发送（队列缓冲）

### 2.6 文件

- `main/peripherals/tja1050.c`
- `main/peripherals/tja1050.h`

---

## 3. MCP4725 DAC 驱动

### 3.1 硬件说明

MCP4725 是 12 位 I2C DAC，分辨率 4096 级，输出电压 = (DACA / 4095) × Vref，Vref 通常为 3.3V（板载）或外部基准。

### 3.2 设备类型

- **类型名：** `mcp4725`
- **能力位：** `WRITE | READ`

### 3.3 配置项

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `sda_gpio` | int | 必填 | I2C SDA 引脚 |
| `scl_gpio` | int | 必填 | I2C SCL 引脚 |
| `i2c_addr` | int | 0x60 | I2C 从机地址 |
| `vref_mv` | int | 3300 | 参考电压（mV） |
| `scl_freq` | int | 400000 | I2C 时钟频率（Hz） |

### 3.4 数据格式

**写入 (WRITE):**
```json
4095
```
或
```json
{ "value": 4095 }
```
- 值范围 0-4095，对应 0-Vref

**读取 (READ):**
```json
{ "value": 2048, "voltage_mv": 1650.0, "vref_mv": 3300 }
```

### 3.5 实现要点

- 使用 `esp_idf_i2c.h`（新建基础设施，见第 8 节）初始化 I2C 总线
- 写入时发送两个字节：`DAC_data = value << 4`（12 位左对齐）
- `read` 返回本地缓存值（不读回 DAC，节省 I2C 交互）
- 支持 EEPROM 写入命令（`{ "eeprom": true, "value": 2048 }`），掉电保持

### 3.6 文件

- `main/peripherals/mcp4725.c`
- `main/peripherals/mcp4725.h`

---

## 4. ADS1115 ADC 驱动

### 4.1 硬件说明

ADS1115 是 16 位 4 通道差分 ADC，支持 860 SPS，支持 PGA 可编程增益，可测量 ±256mV 至 ±6.144V。

### 4.2 设备类型

- **类型名：** `ads1115`
- **能力位：** `READ | PERIODIC | NOTIFY`

### 4.3 配置项

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `sda_gpio` | int | 必填 | I2C SDA 引脚 |
| `scl_gpio` | int | 必填 | I2C SCL 引脚 |
| `i2c_addr` | int | 0x48 | I2C 从机地址（0x48-0x4B） |
| `channel` | int | 0 | 测量通道（0-3） |
| `gain` | int | 1 | PGA 增益（0=2/3x, 1=1x, 2=2x, 4=4x, 8=8x） |
| `rate` | int | 128 | 采样率 SPS（8/16/32/64/128/250/475/860） |
| `interval_ms` | int | 1000 | 周期上报间隔（ms） |
| `scl_freq` | int | 400000 | I2C 时钟频率（Hz） |

### 4.4 数据格式

**读取 (READ / PERIODIC / NOTIFY):**
```json
{
  "raw": 12345,
  "mv": 187.5,
  "channel": 0,
  "gain": 1,
  "rate": 128
}
```
- `raw`: 有符号 16 位 ADC 原始值
- `mv`: 换算后的电压值（mV），计算公式：`(raw / 32768.0) * (Vref / gain)`

### 4.5 实现要点

- 使用 `esp_idf_i2c.h` 基础设施
- 支持单次转换模式（驱动主动触发转换，等待完成后读取）
- `tick` 中按 `interval_ms` 限频读取
- 支持差分测量配置（channel 配对：0-1, 0-3, 1-3, 2-3），通过 `diff_channel` 配置项选择

### 4.6 文件

- `main/peripherals/ads1115.c`
- `main/peripherals/ads1115.h`

---

## 5. INA226 功率监测驱动

### 5.1 硬件说明

INA226 是高端双向电流/功率监测器，通过 I2C 接口，测量总线电压、负载电压、计算电流和功率。需外接分流电阻（Rshunt）。

### 5.2 设备类型

- **类型名：** `ina226`
- **能力位：** `READ | PERIODIC | NOTIFY`

### 5.3 配置项

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `sda_gpio` | int | 必填 | I2C SDA 引脚 |
| `scl_gpio` | int | 必填 | I2C SCL 引脚 |
| `i2c_addr` | int | 0x40 | I2C 从机地址（0x40-0x4F） |
| `r_shunt` | float | 10.0 | 分流电阻值（mΩ） |
| `max_current_ma` | int | 1000 | 最大预期电流（mA），用于校准 |
| `bus_voltage_range` | int | 16 | 总线电压范围（16 或 36V） |
| `avg_mode` | int | 3 | 平均次数（0=1, 1=4, 2=16, 3=64, 4=128, 5=256, 6=512, 7=1024） |
| `bus_conv_time` | int | 3 | 总线电压转换时间（0-7 对应 140μs-8.244ms） |
| `shunt_conv_time` | int | 3 | 分流电压转换时间（0-7 同上） |
| `interval_ms` | int | 1000 | 周期上报间隔（ms） |
| `scl_freq` | int | 400000 | I2C 时钟频率（Hz） |

### 5.4 数据格式

**读取 (READ / PERIODIC / NOTIFY):**
```json
{
  "bus_voltage_mv": 5000.0,
  "shunt_voltage_uv": 500.0,
  "current_ma": 50.0,
  "power_mw": 250.0
}
```

### 5.5 实现要点

- 使用 `esp_idf_i2c.h` 基础设施
- 初始化时计算并写入校准寄存器：`Cal = 0.00512 / (Current_LSB × Rshunt)`
  - `Current_LSB = MaxCurrent / 32768`
- 支持触发式和连续模式读取
- `tick` 中按 `interval_ms` 限频读取

### 5.6 文件

- `main/peripherals/ina226.c`
- `main/peripherals/ina226.h`

---

## 6. 蜂鸣器驱动

### 6.1 硬件说明

无源蜂鸣器需要 PWM 方波驱动，可通过改变频率和占空比控制音调和音量。本驱动使用 LEDC 通道输出 PWM。

### 6.2 设备类型

- **类型名：** `buzzer`
- **能力位：** `WRITE | READ`

### 6.3 配置项

| Key | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gpio` | int | 必填 | 蜂鸣器控制 GPIO |
| `frequency` | int | 2000 | 默认频率（Hz），范围 100-10000 |
| `duty` | int | 50 | 默认占空比（%），范围 0-100 |
| `auto_off_ms` | int | 0 | 自动关闭时间（ms），0=不自动关闭 |

### 6.4 数据格式

**写入 (WRITE):**
```json
{ "on": true }
```
```json
{ "on": true, "frequency": 4000, "duty": 80 }
```
```json
{ "on": true, "duration_ms": 500 }
```
```json
{ "on": false }
```

- `on`: 开启/关闭蜂鸣器
- `frequency`: 临时设置频率（Hz）
- `duty`: 临时设置占空比（%）
- `duration_ms`: 开启后自动关闭的时间（ms）

**读取 (READ):**
```json
{ "on": true, "frequency": 2000, "duty": 50, "remaining_ms": 300 }
```

### 6.5 实现要点

- 使用 LEDC 低速通道（低速模式下可动态改变频率）
- `buzzer_data_t` 中保存当前状态和定时器句柄
- `auto_off_ms` 使用 `esp_timer` 单次定时器实现
- 关闭时将占空比设为 0，而非关闭 LEDC（避免频繁配置开销）
- 写入相同状态时无操作（幂等）

### 6.6 文件

- `main/peripherals/buzzer.c`
- `main/peripherals/buzzer.h`

---

## 7. I2C 总线基础设施

### 7.1 设计背景

MCP4725、ADS1115、INA226 都需要 I2C 总线。为了代码简洁和资源效率，采用**每个设备独立管理自己的 I2C 总线初始化**的方式（同用户选择 B）。每个设备初始化时检查总线是否已初始化，避免重复初始化。

### 7.2 模块设计

新建 `main/peripherals/esp_idf_i2c.h`（不建 `.c`，直接内联到各驱动）：

```c
// 主要接口
esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz, i2c_port_t *out_port);
esp_err_t esp_idf_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t esp_idf_i2c_read(uint8_t addr, uint8_t *data, size_t len);
```

内部维护一个全局 I2C 总线表（最多 2 个 I2C 端口），按 (sda, scl) 唯一标识总线。

---

## 8. 构建集成

### 8.1 文件清单

新增文件：
```
main/peripherals/tja1050.c
main/peripherals/tja1050.h
main/peripherals/mcp4725.c
main/peripherals/mcp4725.h
main/peripherals/ads1115.c
main/peripherals/ads1115.h
main/peripherals/ina226.c
main/peripherals/ina226.h
main/peripherals/buzzer.c
main/peripherals/buzzer.h
main/peripherals/esp_idf_i2c.h   # I2C 基础设施头文件
```

修改文件：
```
main/peripherals/peripherals.c   # 新增 5 个驱动的注册调用
main/CMakeLists.txt              # 新增 5 个 .c 文件到编译列表
docs/ARCHITECTURE.md             # 新增外设文档
docs/TESTING.md                  # 新增测试用例
```

### 8.2 Kconfig 扩展

在 `main/Kconfig.projbuild` 中新增各外设的使能开关（默认全部打开），允许用户在 menuconfig 中禁用不需要的外设以节省空间。

### 8.3 README 更新

在 `README.md` 的外设表格中新增 5 个外设类型。

---

## 9. 验收标准

1. 全部 5 个驱动通过编译，0 error / 0 warning
2. 每个驱动支持 `init`、`deinit`、`read`、`write`、`get_default_config`、`validate_config`
3. 配置文件示例可正确初始化设备
4. 主机单元测试（yaml_test）不受影响
5. 文档同步更新（ARCHITECTURE.md、TESTING.md、README.md）

---

## 10. 驱动编写规范（约束）

| 约束 | 说明 |
|---|---|
| 能力位如实声明 | `PERIODIC` 会让值自动进入 MQTT `sensors` 主题 |
| `get_default_config` 必须实现 | 配置端据此得知字段 |
| `validate_config` 能校验就校验 | 拒绝非法参数优于把硬件配坏 |
| `read`/`write` 不需要就留 NULL | 调用方会得到 `ESP_ERR_NOT_SUPPORTED` |
| `tick` 不阻塞 | 轮询任务 100ms 节拍，长阻塞影响其他驱动 |
| 无策略 | 不决定何时上报、是否告警、如何命名 |
| 内存自管理 | `init` 中 `calloc`，`deinit` 中 `free` |
