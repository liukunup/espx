# ESPX 固件设计规范

**版本：** 1.0.0
**日期：** 2025-09-26
**硬件：** ESP32-S3 R16N8
**目标：** 企业级 IoT 设备固件，支持 HTTPS Web 管理、MQTT 通信、差分 OTA、工厂预配置、生产线测试

---

## 1. 系统概述

### 1.1 功能特性

| 类别 | 功能 | 状态 |
|------|------|------|
| **配网** | Wi-Fi Provisioning (QR码) | 已有 |
| **通信** | MQTT Client | 新增 |
| **升级** | Delta OTA (差分升级) | 新增 |
| **管理** | HTTPS Web Server + WebSocket | 新增 |
| **配置** | 参数管理 (NVS + RAM) | 新增 |
| **安全** | 自签名证书 | 新增 |
| **制造** | Manufacturing Provisioning | 新增 |
| **测试** | 生产线测试模式 | 新增 |

### 1.2 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3 R16N8                          │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                      Application                         │  │
│  │  ┌──────────┐  ┌───────────┐  ┌──────────┐             │  │
│  │  │ Wi-Fi    │  │ HTTPS     │  │ MQTT     │             │  │
│  │  │ Prov     │  │ Server    │  │ Client   │             │  │
│  │  └──────────┘  └───────────┘  └──────────┘             │  │
│  │  ┌──────────┐  ┌───────────┐  ┌──────────┐             │  │
│  │  │ OTA      │  │ Param     │  │ Test     │             │  │
│  │  │ Service  │  │ Store     │  │ Mode     │             │  │
│  │  └──────────┘  └───────────┘  └──────────┘             │  │
│  │  ┌──────────┐  ┌───────────┐  ┌──────────┐             │  │
│  │  │ MFG      │  │ Cert      │  │ LED      │             │  │
│  │  │ Provision│  │ Manager   │  │ Driver   │             │  │
│  │  └──────────┘  └───────────┘  └──────────┘             │  │
│  └──────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    ESP-IDF / FreeRTOS                     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 组件依赖

```
dependencies:
  espressif/qrcode: ^0.2.0          # 已有
  espressif/network_provisioning: ^1.2.4  # 已有
  espressif/led_indicator: ^2.1.2    # 已有
  espressif/i2c_bus: ^1.5.2          # 已有
  espressif/esp_delta_ota: ^1.1.4    # 新增
  espressif/mqtt: ^1.1.0             # 新增
  espressif/esp_https_server: ^1.1.4 # 新增
```

---

## 2. 分区表设计

### 2.1 分区布局

| 名称 | 类型 | 子类型 | 大小 | 说明 |
|------|------|--------|------|------|
| nvs | data | nvs | 0x6000 | NVS 存储 |
| phy_init | data | phy | 0x1000 | PHY 初始化数据 |
| factory | app | factory | 0x100000 | 出厂固件 |
| ota_0 | app | ota_0 | 0x100000 | OTA 分区 A |
| ota_1 | app | ota_1 | 0x100000 | OTA 分区 B |
| certs | data | fat | 0x4000 | 证书存储 |
| mfg_data | data | nvs | 0x4000 | 工厂预配置数据 |

### 2.2 OTA 策略

- **双分区循环升级**：factory → ota_0/ota_1
- **差分升级优先**：使用 esp_delta_ota 生成和应用差分包
- **回滚机制**：新固件启动失败自动回滚到上一个分区
- **启动校验**：启动前验证固件完整性

---

## 3. 模块设计

### 3.1 模块列表

| 模块 | 路径 | 职责 |
|------|------|------|
| wifi_prov | main/wifi_prov/ | Wi-Fi 配网（已有） |
| led_driver | components/led_driver/ | LED 控制驱动 |
| param_store | main/param_store/ | 参数注册、读写、持久化 |
| cert_manager | main/cert_manager/ | 证书生成、存储、加载 |
| web_server | main/web_server/ | HTTPS + WebSocket + 静态页面 |
| ota_service | main/ota_service/ | 差分 OTA 下载、校验、升级 |
| mqtt_client | main/mqtt_client/ | MQTT 连接、订阅、发布 |
| mfg_provision | main/mfg_provision/ | 工厂预配置加载 |
| test_mode | main/test_mode/ | 生产线测试 |

### 3.2 参数存储 (param_store)

#### 3.2.1 参数定义结构

```c
typedef enum {
    PARAM_TYPE_INT,       // 整数
    PARAM_TYPE_FLOAT,     // 浮点数
    PARAM_TYPE_STRING,    // 字符串
    PARAM_TYPE_BOOL,     // 布尔值
} param_type_t;

typedef enum {
    PARAM_ACCESS_RO,      // 只读
    PARAM_ACCESS_RW,      // 读写
} param_access_t;

typedef struct {
    const char *key;          // 参数键名
    param_type_t type;        // 数据类型
    param_access_t access;    // 访问权限
    const void *default_val;  // 默认值
    const void *min;          // 最小值（数值类型）
    const void *max;          // 最大值（数值类型）
    const char *description;  // 参数描述
} param_def_t;
```

#### 3.2.2 参数定义表

| 键名 | 类型 | 权限 | 默认值 | 说明 |
|------|------|------|--------|------|
| **系统参数（只读）** |
| device_name | string | RO | "ESPX" | 设备名称 |
| device_id | string | RO | - | 设备唯一ID（MAC） |
| chip_model | string | RO | "ESP32-S3" | 芯片型号 |
| chip_revision | string | RO | - | 芯片版本 |
| firmware_ver | string | RO | - | 固件版本 |
| build_time | string | RO | - | 构建时间 |
| **运行时参数（读写）** |
| wifi_ssid | string | RW | "" | Wi-Fi SSID |
| wifi_password | string | RW | "" | Wi-Fi 密码 |
| mqtt_broker | string | RW | "mqtt://localhost:1883" | MQTT Broker URL |
| mqtt_username | string | RW | "" | MQTT 用户名 |
| mqtt_password | string | RW | "" | MQTT 密码 |
| mqtt_client_id | string | RW | "espx-{chip_id}" | MQTT 客户端ID |
| mqtt_topic_prefix | string | RW | "espx/{device_id}" | MQTT 主题前缀 |
| ota_server_url | string | RW | "" | OTA 服务器 URL |
| **业务参数（框架预留）** |
| param_int_001 | int | RW | 0 | 业务参数1 |
| param_float_001 | float | RW | 0.0 | 业务参数2 |
| param_str_001 | string | RW | "" | 业务参数3 |
| param_bool_001 | bool | RW | false | 业务参数4 |

#### 3.2.3 持久化策略

| 参数类别 | 存储位置 | 持久化时机 |
|----------|----------|------------|
| 只读参数 | RAM | 无需持久化 |
| 运行时参数 | NVS | 值变更时自动保存 |
| 业务参数 | NVS | 值变更时自动保存 |

### 3.3 证书管理 (cert_manager)

#### 3.3.1 证书类型

| 证书 | 用途 | 存储位置 |
|------|------|----------|
| Server Certificate | HTTPS 服务器证书 | NVS (certs分区) |
| Server Private Key | HTTPS 私钥 | NVS (certs分区) |
| CA Certificate | MQTT 客户端证书（可选） | NVS (certs分区) |

#### 3.3.2 证书生命周期

```
首次启动 → 生成自签名证书 → 存储到NVS
后续启动 → 从NVS加载证书
证书更新 → 通过Web界面触发重新生成
```

#### 3.3.3 证书规格

- **算法：** RSA 2048-bit
- **有效期：** 10年
- **CN：** ESPX-{chip_id}
- **SAN：** espx-{chip_id}.local

### 3.4 HTTPS Web 服务器 (web_server)

#### 3.4.1 端点设计

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | / | Web 管理界面 (HTML) |
| GET | /api/system/info | 系统信息 |
| GET | /api/params | 获取所有参数 |
| GET | /api/params/{key} | 获取单个参数 |
| PUT | /api/params/{key} | 更新单个参数 |
| POST | /api/params/batch | 批量更新参数 |
| GET | /api/wifi/status | Wi-Fi 连接状态 |
| POST | /api/wifi/connect | 发起Wi-Fi连接 |
| POST | /api/wifi/scan | 扫描Wi-Fi网络 |
| GET | /api/ota/status | OTA 升级状态 |
| POST | /api/ota/start | 开始OTA升级 |
| POST | /api/ota/cancel | 取消OTA升级 |
| GET | /api/mqtt/status | MQTT 连接状态 |
| POST | /api/mqtt/reconnect | 重新连接MQTT |
| POST | /api/system/reboot | 重启设备 |
| GET | /api/certs/info | 证书信息 |
| POST | /api/certs/regenerate | 重新生成证书 |
| GET | /static/* | 静态资源 |
| WS | /ws | WebSocket 实时推送 |

#### 3.4.2 Web 界面布局

```
┌─────────────────────────────────────────────────────────────┐
│  ESPX Device Manager                              [连接状态]│
├─────────────────────────────────────────────────────────────┤
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │ 系统    │ │ 参数    │ │ 网络    │ │ OTA     │           │
│  │ 信息    │ │ 配置    │ │ 设置    │ │ 升级    │           │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘           │
├─────────────────────────────────────────────────────────────┤
│  [内容区域 - 根据选中Tab显示不同内容]                         │
│                                                             │
│  Tab: 系统信息                                               │
│  ├─ 设备名称: ESPX-001                                      │
│  ├─ 固件版本: 1.0.0                                        │
│  ├─ 芯片型号: ESP32-S3                                      │
│  ├─ MAC地址: XX:XX:XX:XX:XX:XX                             │
│  └─ 运行时间: 12345秒                                       │
│                                                             │
│  Tab: 参数配置                                               │
│  ├─ Wi-Fi SSID: [_______________]                           │
│  ├─ MQTT Broker: [_______________]                         │
│  ├─ OTA URL: [_______________]                             │
│  └─ [保存配置]                                              │
│                                                             │
│  Tab: OTA升级                                               │
│  ├─ 当前版本: 1.0.0                                         │
│  ├─ 服务器URL: [_______________]                           │
│  └─ [检查更新] [开始升级]                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 3.5 OTA 服务 (ota_service)

#### 3.5.1 升级流程

```
1. Web触发OTA → 检查服务器版本
2. 下载差分包manifest → 获取差分包URL
3. 下载差分包 → ESP32存储到OTA分区
4. 差分校验 → 验证完整性
5. 设置启动分区 → 标记新固件
6. 重启 → 自动进入新固件
7. 启动成功 → 更新完成
8. 启动失败 → 回滚到旧固件
```

#### 3.5.2 差分包格式

```json
{
  "version": "1.0.1",
  "from_version": "1.0.0",
  "target": "esp32s3",
  "size": 102400,
  "checksum": "sha256:...",
  "url": "https://server/ota/espx_v1.0.1.patch",
  "full_size": 1048576,
  "full_checksum": "sha256:..."
}
```

#### 3.5.3 OTA 状态机

```
IDLE → CHECKING → DOWNLOADING → VERIFYING → APPLYING → REBOOTING
                ↓
            NO_UPDATE
  ↓
FAILED (可重试)
```

### 3.6 MQTT 客户端 (mqtt_client)

#### 3.6.1 主题结构

| 方向 | 主题 | 说明 |
|------|------|------|
| 发布 | {prefix}/status/online | 设备上线 |
| 发布 | {prefix}/status/heartbeat | 心跳 |
| 发布 | {prefix}/telemetry | 遥测数据 |
| 发布 | {prefix}/attributes | 属性上报 |
| 订阅 | {prefix}/commands/# | 命令接收 |
| 订阅 | {prefix}/attributes/set | 属性设置 |
| 订阅 | {prefix}/ota/command | OTA 命令 |

#### 3.6.2 默认配置

| 参数 | 默认值 |
|------|--------|
| Broker | mqtt://localhost:1883 |
| QoS | 1 |
| Keep Alive | 60s |
| Reconnect Interval | 5s |
| Clean Session | false |

### 3.7 Manufacturing Provisioning (mfg_provision)

#### 3.7.1 预置数据

| 键名 | 类型 | 说明 |
|------|------|------|
| mfg_wifi_ssid | string | 默认Wi-Fi SSID |
| mfg_wifi_password | string | 默认Wi-Fi密码 |
| mfg_mqtt_broker | string | 默认MQTT Broker |
| mfg_ota_url | string | 默认OTA服务器 |
| mfg_device_serial | string | 设备序列号 |
| mfg_cert_ca | string | CA证书 (Base64) |
| mfg_cert_client | string | 客户端证书 (Base64) |
| mfg_key_client | string | 客户端私钥 (Base64) |

#### 3.7.2 加载策略

```
首次启动 → 检测mfg_data分区
    ├─ 有数据 → 加载到param_store → 清除mfg_data
    └─ 无数据 → 跳过，使用默认值
```

### 3.8 测试模式 (test_mode)

#### 3.8.1 触发条件

- 上电时检测指定GPIO引脚电平
- 默认GPIO0（BOOT按钮）低电平触发
- 检测时长：上电后500ms内判断

#### 3.8.2 测试项目

| 测试项 | 方法 | 判定标准 |
|--------|------|----------|
| LED 闪烁 | GPIO控制LED周期性闪烁 | 视觉确认 |
| 按键测试 | 检测GPIO输入 | 短按/长按响应 |
| Wi-Fi 连接 | 连接到预置SSID | 获取IP成功 |
| MQTT 连接 | 连接到预置Broker | 连接成功 |
| 串口通信 | 接收测试命令返回OK | 交互正常 |

#### 3.8.3 测试界面

```
┌─────────────────────────────────────────────────────────────┐
│              ESPX Manufacturing Test Mode                   │
├─────────────────────────────────────────────────────────────┤
│  [1] LED Test          [待测试] [通过] [失败]                │
│  [2] Button Test       [待测试] [通过] [失败]                │
│  [3] Wi-Fi Test        [待测试] [通过] [失败]                │
│  [4] MQTT Test         [待测试] [通过] [失败]                │
│  [5] UART Test         [待测试] [通过] [失败]                │
├─────────────────────────────────────────────────────────────┤
│  Serial Command:                                             │
│  > led on/off - LED控制                                      │
│  > button test - 按键测试                                    │
│  > wifi connect <ssid> <password> - Wi-Fi连接               │
│  > mqtt test - MQTT测试                                      │
│  > pass <n> - 标记第N项测试通过                               │
│  > fail <n> - 标记第N项测试失败                               │
│  > exit - 退出测试模式重启                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. 文件结构

```
espx/
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── idf_component.yml
│   ├── app_main.c
│   ├── app/
│   │   ├── app.c
│   │   └── app.h
│   ├── wifi_prov/              # 已有
│   ├── param_store/           # 新增
│   │   ├── param_store.c
│   │   └── param_store.h
│   ├── cert_manager/          # 新增
│   │   ├── cert_manager.c
│   │   └── cert_manager.h
│   ├── web_server/            # 新增
│   │   ├── web_server.c
│   │   ├── web_server.h
│   │   ├── handlers/
│   │   │   ├── system_handler.c
│   │   │   ├── params_handler.c
│   │   │   ├── wifi_handler.c
│   │   │   ├── ota_handler.c
│   │   │   └── mqtt_handler.c
│   │   └── web_files/         # 静态网页
│   │       ├── index.html
│   │       ├── style.css
│   │       ├── app.js
│   │       └── assets/
│   ├── ota_service/           # 新增
│   │   ├── ota_service.c
│   │   └── ota_service.h
│   ├── mqtt_client/           # 新增
│   │   ├── mqtt_client.c
│   │   └── mqtt_client.h
│   ├── mfg_provision/         # 新增
│   │   ├── mfg_provision.c
│   │   └── mfg_provision.h
│   └── test_mode/             # 新增
│       ├── test_mode.c
│       └── test_mode.h
├── components/
│   └── led_driver/            # 新增
│       ├── led_driver.c
│       └── led_driver.h
├── docs/
│   └── superpowers/
│       └── specs/
│           └── 2025-09-26-espx-design.md
├── partitions.csv
├── sdkconfig
└── CMakeLists.txt
```

---

## 5. 配置项 (Kconfig)

```kconfig
# ESPX Configuration

menu "ESPX Settings"

    menu "Device"
        config DEVICE_NAME
            string "Device Name"
            default "ESPX"
        config FIRMWARE_VERSION
            string "Firmware Version"
            default "1.0.0"
    endmenu

    menu "Manufacturing"
        config MFG_DATA_PARTITION
            bool "Enable Manufacturing Data"
            default n
        config MFG_TEST_GPIO
            int "Test Mode GPIO"
            default 0
    endmenu

    menu "Wi-Fi"
        config WIFI_DEFAULT_SSID
            string "Default Wi-Fi SSID"
        config WIFI_DEFAULT_PASSWORD
            string "Default Wi-Fi Password"
            password
    endmenu

    menu "MQTT"
        config MQTT_BROKER_URL
            string "Default MQTT Broker"
            default "mqtt://localhost:1883"
        config MQTT_RECONNECT_INTERVAL
            int "MQTT Reconnect Interval (seconds)"
            default 5
    endmenu

    menu "OTA"
        config OTA_SERVER_URL
            string "Default OTA Server URL"
        config OTA_CHECK_INTERVAL
            int "OTA Check Interval (minutes)"
            default 60
    endmenu

    menu "HTTPS Server"
        config HTTPS_SERVER_PORT
            int "HTTPS Port"
            default 443
        config ENABLE_HTTP_REDIRECT
            bool "Redirect HTTP to HTTPS"
            default y
    endmenu

endmenu
```

---

## 6. 安全考虑

| 项目 | 措施 |
|------|------|
| HTTPS | TLS 1.2+，自签名证书 |
| 参数存储 | 敏感参数（密码/密钥）加密存储 |
| OTA | 签名校验 + SHA256校验 |
| MQTT | TLS支持 + 用户名/密码认证 |
| 调试接口 | 生产版本禁用串口调试日志 |
| 安全启动 | 启用 Secure Boot（可选） |

---

## 7. 实现计划

### 阶段一：基础框架
1. 更新分区表
2. 添加 esp_https_server 依赖
3. 创建模块目录结构
4. 实现 param_store 核心
5. 实现 cert_manager

### 阶段二：Web 服务
1. 实现 web_server 基础框架
2. 实现 HTTP API 处理器
3. 编写 Web 静态页面
4. 实现 WebSocket 推送
5. 实现参数配置页面

### 阶段三：MQTT 集成
1. 实现 mqtt_client 模块
2. 实现主题订阅/发布
3. 集成参数到 MQTT
4. 实现命令处理

### 阶段四：OTA 升级
1. 实现 ota_service 模块
2. 集成差分OTA
3. 实现 Web OTA 界面
4. 实现回滚机制

### 阶段五：制造支持
1. 实现 mfg_provision 模块
2. 实现 test_mode 模块
3. 编写测试界面

### 阶段六：集成测试
1. Wi-Fi 配网集成
2. 参数持久化测试
3. MQTT 通信测试
4. OTA 升级测试
5. 生产测试流程验证

---

## 8. 验收标准

| 项目 | 验收条件 |
|------|----------|
| HTTPS Web | 可通过浏览器访问，证书警告后可正常显示 |
| 参数配置 | 可修改运行时参数，重启后保持 |
| MQTT | 可连接Broker，订阅/发布消息正常 |
| OTA | 可从服务器下载差分包并升级成功 |
| 证书管理 | 可重新生成证书并生效 |
| 工厂预配置 | 预置数据可正确加载 |
| 测试模式 | 按键触发正常，各测试项可执行 |
| Wi-Fi 配网 | QR码配网正常工作 |
