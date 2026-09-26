# ESPX 部署与运维手册

产线烧录、服务端准备（EMQX）、安全加固、升级策略、故障排查。

---

## 1. 部署形态

```
                        ┌──────────────────────────┐
   浏览器 ─── wss/https ─┤  ESPX 节点                │
   （单页管理界面）       │   ESP32-S3 R16N8          │
                        │   16MB Flash / 8MB PSRAM   │
   MQTT broker ─────────┤                            │
   (EMQX)               │  设备驱动（配置决定）       │
                        │  NTP · mDNS · AT(UART1)    │
   OTA 服务器 ── http ──┤  差分 OTA                  │
   (patch 文件)          └──────────────────────────┘
                                    │
   宿主 MCU ───── UART1 AT ─────────┘
```

节点只需能访问 MQTT broker 与 OTA 服务器；管理界面与 AT 都是本地能力。

---

## 2. 分区布局（16MB）

| 分区 | 类型 | 偏移 | 大小 | 用途 |
|---|---|---|---|---|
| `nvs` | data/nvs | 0x009000 | 24K | 配网凭据、节点与设备配置 |
| `otadata` | data/ota | 0x00F000 | 8K | **OTA 必需**，记录启动槽位 |
| `phy_init` | data/phy | 0x011000 | 4K | RF 校准 |
| `factory` | app | 0x020000 | 4M | 出厂固件 |
| `ota_0` | app | 0x420000 | 4M | OTA 槽 A |
| `ota_1` | app | 0x820000 | 4M | OTA 槽 B |
| `certs` | data/fat | 0xC20000 | 16K | 预留：运行时证书 |
| `mfg_data` | data/nvs | 0xC24000 | 16K | 工厂预置数据 |

`otadata` 不可省略 —— 没有它 `esp_ota_set_boot_partition()` 会失败，OTA 无法切槽。

---

## 3. 服务端准备（EMQX）

### 3.1 关键前提

| # | 项目 | 说明 |
|---|---|---|
| 1 | TCP 监听器 1883 可达 | Docker 需映射 `-p 1883:1883`，防火墙放行 |
| 2 | 认证（Authentication） | EMQX 5/6 **默认不开认证 = 允许匿名**；开启后需为设备建用户名/密码 |
| 3 | **授权（Authorization）** | **EMQX 6.3 默认 `hardened` 安全档 + `no_match = deny`：客户端能连上，但所有 pub/sub 都被拒** —— 必须显式放行 |
| 4 | retain 可用 | `state` 用 retained 消息，确认 `retain_available = true`（默认 true） |
| 5 | Client ID 唯一 | 设备用 `espx-<MAC>`，唯一；勿用同一 ID 双开（会互踢） |
| 6 | MQTT 版本 | 设备用 3.1.1，EMQX 支持 |
| 7 | TLS（可选） | 先用明文 1883 最省事；自签名 broker 证书设备侧目前无 CA 配置入口 |

> 第 3 条是最容易踩的坑。它表现为"MQTT 连上了但没有数据"，日志里
> `MQTT connected` 后面看不到任何上报。

### 3.2 一键初始化

```bash
tools/emqx_init.py --host <emqx-host> --admin-pass '<dashboard密码>' \
                   --device-user espx --device-pass '<设备密码>' \
                   --topic-prefix plant/line1 \
                   --dry-run          # 先看计划，不改动
```

确认无误后：

```bash
tools/emqx_init.py --host <emqx-host> --admin-pass '<dashboard密码>' \
                   --device-user espx --device-pass '<设备密码>' \
                   --topic-prefix plant/line1 \
                   --apply --json
```

脚本是**幂等且只做追加**的，会：

1. 用 dashboard 账号登录 REST API 取 token
2. 报告 MQTT 监听器状态
3. 创建 `password_based` / `built_in_database` 认证器，并创建/更新设备用户
4. 添加 `built_in_database` 授权源与 ACL 规则
   （发布 `plant/line1/#`，订阅 `plant/line1/cmd/#` 与 `plant/line1/ota/#`）
5. **用 paho 做真实 MQTT 往返验证**：以设备身份连接、订阅、发布，逐项报告
6. 打印可直接下发给设备的网络配置

输出示例：

```
Applying
  authenticator: reusing password_based:built_in_database
  user         : 'espx' password updated
  authz source : built_in_database already present
  ACL rules    : 3 added (3 total)

Verification (real MQTT round trip)
  [PASS] CONNECT accepted
  [PASS] SUBSCRIBE plant/line1/cmd/# granted
  [PASS] PUBLISH plant/line1/state accepted

RESULT: OK — the device user can connect, subscribe and publish
```

回滚：

```bash
tools/emqx_init.py --host <emqx-host> --admin-pass '<pw>' \
                   --device-user espx --undo
```

> 注意：创建认证器会**关闭匿名访问**。若该 broker 上还有别的匿名客户端，
> 加 `--no-authn` 只配 ACL 规则。

### 3.3 权限收敛建议

脚本给出的 ACL 遵循最小权限：设备只能发布自己的前缀、只能订阅自己的命令前缀。
生产环境进一步收紧：

- 用 `${clientid}` 占位符替代固定前缀，做到"只能发自己的主题"
- ACL 里显式 `{deny, all}` 收尾，并把 `authorization.no_match` 保持 `deny`
- 设备密码按批次或按台唯一，避免一台被攻破波及全网

---

## 4. 产线流程

### 4.1 单台

```bash
# 1. 擦净并烧录
idf.py erase-flash
idf.py -p <串口> -b 460800 flash

# 2. 长按 BOOT 3 秒进入测试模式，写入本台配置
cfg {network: {wifi_ssid: PlantNet, wifi_password: "<密码>",
               mqtt_broker: "mqtt://broker.plant:1883",
               mqtt_username: espx, mqtt_password: "<密码>",
               mqtt_topic_prefix: plant/line1}}
mfg set {"node":{"device_id":"espx-0001","name":"Line-1"},
         "devices":[{"id":"temp_in","type":"dht11","config":{"gpio":4,"interval_ms":5000}},
                    {"id":"relay_a","type":"relay","config":{"gpio":5,"active_level":1}}]}

# 3. 硬件自检
test all            # 逐项 PASS/FAIL，输出设备

# 4. 应用并结束
mfg apply
exit
```

`mfg_data` 里的预置数据**只应用一次**，应用后即清除，避免重刷主固件时重复覆盖。

### 4.2 批量

```bash
python3 tools/espx_test.py --port <串口> all      # 每台跑一遍回归
```

### 4.3 烧录前预置（可选）

若需在烧录阶段就带上凭据，可在 `mfg_data` 分区预写 JSON
（NVS 命名空间 `factory`，键 `config`）。开机时自动应用一次。

---

## 5. 首次上电（不预置凭据时）

默认 **BLE 配网**：设备不创建任何开放 AP。

```
I (…) BLE_INIT: Bluetooth MAC: 84:c7:bb:77:2e:76
I (…) NimBLE: GAP procedure initiated: advertise;
I (…) network_prov_mgr: Provisioning started with service name : ESPX_772E74
I (…) QRCODE: {"ver":"v1","name":"ESPX_772E74","pop":"abcd1234","transport":"ble"}
```

用 **ESP BLE Prov** App 扫描 `ESPX_772E74`，输入 PoP（`CONFIG_ESPX_PROV_POP`），
再填 Wi-Fi 账号密码。

只需 BLE 客户端不可用的场合才切到 SoftAP：

```
CONFIG_ESPX_PROV_TRANSPORT_SOFTAP=y
```

> SoftAP 模式下，配置界面在配网期间对无线电范围内的任何人可达
> （已知限制 9）。

---

## 6. 安全加固清单

按优先级排列。

| # | 项目 | 现状 | 生产要求 |
|---|---|---|---|
| 1 | **REST API 鉴权** | **无鉴权** | 暴露到不可信网络前必须加。建议 HTTPS Basic / Bearer token，在 `web_server.c` 统一前置校验 |
| 2 | 证书 | 仓库内开发自签名证书 | 替换为自有 PKI；或按台签发并写入 `certs` 分区 |
| 3 | BLE PoP | 默认 `abcd1234`，且默认打印进二维码 | 按批次更换；置 `CONFIG_ESPX_PROV_SHOW_POP_IN_QR=n` |
| 4 | 配网传输 | BLE（不暴露 AP） | 保持 BLE；如必须 SoftAP，仅在受控场所配网 |
| 5 | Wi-Fi 凭据 | NVS 明文 | 启用 NVS 加密，或改用每台 PSK |
| 6 | MQTT | 明文 1883（建议起步用） | 上 TLS（8883）；设备侧用公有 CA 可直接校验 |
| 7 | OTA 来源 | 明文 HTTP + 基线 SHA-256 校验 | 上 HTTPS，并对补丁做签名校验 |
| 8 | 安全启动 / Flash 加密 | 未启用 | 需要防物理提取时启用 |
| 9 | 串口 AT | 无鉴权，UART1 | 若宿主 MCU 可信则无所谓；否则加 `AT+PWD` 之类的口令，或物理隔离 |
| 10 | 调试日志 | INFO | 生产降到 WARN，且确保不打印密码（当前已对 password/key 打 `<set>`） |

生成新证书：

```bash
tools/gen_certs.sh espx.local 3650      # 然后 idf.py build
```

---

## 7. 升级策略

### 7.1 差分 OTA

```bash
# 基线必须是设备"当前正在运行"的那份固件
tools/make_delta_patch.py --base dist/current.bin \
                          --new  dist/next.bin \
                          --out  dist/current_to_next.patch

# 放上 HTTP 服务
python3 -m http.server 8000 --directory dist
```

下发：

```bash
curl -k -X POST https://$HOST/api/ota/start \
     -H 'Content-Type: application/json' \
     -d '{"url":"http://192.168.1.20:8000/current_to_next.patch"}'
```

或 MQTT / AT：

```bash
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/ota" -m '{"action":"start","url":"http://…/p.patch"}'
# AT+OTASTART="http://…/p.patch"
```

跟随进度：

```bash
curl -k https://$HOST/api/ota/status | python3 -m json.tool
# AT+OTASTATUS?
```

### 7.2 安全属性

| 机制 | 效果 |
|---|---|
| 补丁头含基线 SHA-256 | 设备与**当前运行固件**比对，不匹配直接拒绝 → 错版本补丁无法刷坏设备 |
| 双槽 + `otadata` | 新固件写入另一槽，失败可回退 |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | 新固件启动后由 `ota_service_mark_valid()` 确认；启用前崩溃会自动回滚 |
| 芯片 ID 校验 | 补丁不适用于本芯片时在写入前拒绝 |

实测压缩率：1.25MB 固件 → 20–75KB 补丁（95%–98%）。

### 7.3 灰度与回滚

- 单台先行 → 小批量 → 全量。补丁基线各不相同，**每台只能用与自身匹配的补丁**，
  所以灰度时要按固件版本分组生成补丁。
- 回滚：设备启动失败会自动回到旧槽（应用层校验通过前不调
  `ota_service_mark_valid()`）；也可手动 `esp_ota_set_boot_partition()` 切换。

---

## 8. 运维

### 8.1 发现设备

```bash
# mDNS（推荐）
ping espx-84c7bb772e74.local
curl -k https://espx-84c7bb772e74.local/api/system/info

# 或串口 AT
AT+CIFSR
```

### 8.2 健康检查

```bash
curl -k https://$HOST/api/system/info | python3 -c '
import json,sys
d=json.load(sys.stdin)
print("fw", d.get("version"), "ip", d.get("ip"), "rssi", d.get("wifi_rssi"))
print("uptime", d.get("uptime"), "free_heap", d.get("free_heap"))
print("time", d.get("time"), "synced", d.get("time_synced"))
print("ws_clients", d.get("ws_clients"))'
```

关注 `free_heap` 长期走势（泄漏）与 `wifi_rssi`（<-75dBm 时链路不稳，
TLS 握手容易超时）。

### 8.3 远程配置

```bash
mosquitto_pub -h $BROKER -t "$PREFIX/cmd/config" -f new.yaml
mosquitto_sub -h $BROKER -t "$PREFIX/config/result" -C 1
```

回执里 `reboot_required: true` 说明改了网络参数，需重启生效。

---

## 9. 故障排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| MQTT 连上但无数据 | EMQX 授权默认拒绝（见 3.1 第 3 条） | 跑 `tools/emqx_init.py --apply`；用它的真实性往返验证确认 |
| `MQTT connected` 一直不出现 | broker 不可达 / 用户密码错 | 串口看 `Network: broker=… user=…`；AT+MQTTCONN? 查询 |
| HTTPS 每次连接被重置 | 证书长度未含结尾 NUL | 已修复；若自行改动 `cert_manager` 请保持 `cert_len` 含 NUL |
| 浏览器提示证书错误 | 自签名证书 | 正常，点信任；或换成自有 PKI |
| 设备进了下载模式而非测试模式 | 用了 strapping 引脚做触发（GPIO0 上电拉低 = ROM 下载模式） | `CONFIG_MFG_TEST_GPIO=-1`（默认），改用长按 BOOT |
| 长按 BOOT 无反应 | NVS 未初始化就读标志 / 看门狗启动太晚 | 已修复；见 DEVELOPMENT.md 第 2 节的顺序约束 |
| 未配网设备进不去测试模式 | 服务阻塞在 Wi-Fi 等待 | 已修复；看门狗在 `wifi_prov_start()` 之前启动 |
| OTA 报 "built for a different firmware version" | 补丁基线与设备当前固件不符 | 用设备当前运行的固件作为 `--base` 重新生成 |
| `esp_ota_set_boot_partition` 失败 | 分区表缺 `otadata` | 检查 `partitions.csv` |
| WebSocket 显示 `poll` | WS 未连上，界面自动退化轮询 | 看 `ws_clients`；确认 `CONFIG_HTTPD_WS_SUPPORT=y` 且 `max_open_sockets` 够用 |
| `https://…local` 打不开 | Windows 缺 Bonjour / Android 支持不稳 | 用 IP |
| 设备掉线 | Wi-Fi 信号弱 / AP 不稳 | 看 `wifi_rssi`；MQTT 会自动重连，无需人工干预 |
| 配置下发 `ok:false` | 参数非法（如引脚越界、类型错误） | 看 `error` 字段定位到具体设备 id |

### 9.1 看门狗与崩溃

```
E (…) task_wdt: Task watchdog got triggered
E (…) Guru Meditation Error: Core 0 panic'ed (…)
```

抓完整日志：

```bash
./tools/verify_device.sh listen 30
```

关注 `Backtrace:` 与 `ELF file SHA256`，用 `idf.py monitor` 的
`addr2line` 自动解符号。

---

## 10. 验收清单

上线前逐项确认：

- [ ] `idf.py build` 0 error / 0 warning
- [ ] `tests/run_yaml_tests.sh` 全通过
- [ ] `tools/espx_test.py all` 全 PASS（启动 / 测试模式 / 硬件自检）
- [ ] `tools/network_tests.py` 全 PASS（HTTPS API / YAML 配置 / MQTT / 差分 OTA）
- [ ] EMQX 侧：认证 + ACL 已配，`emqx_init.py` 的真实往返验证 PASS
- [ ] 证书已替换为自有 PKI
- [ ] BLE PoP 已按批次更换，二维码不再内嵌 PoP
- [ ] REST API 鉴权已加（若设备暴露在不可信网络）
- [ ] 断电中断 OTA 后设备仍能正常启动（T4.2）
- [ ] 故意做一个坏固件验证自动回滚（T4.4）
- [ ] 设备与 broker 的 MQTT 走 TLS（生产要求）
- [ ] 批量产线流程走通一次完整闭环
