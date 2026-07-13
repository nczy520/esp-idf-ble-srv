# Python CLI 客户端使用说明 v1.2.1

## 目录

- [简介](#简介)
- [环境要求](#环境要求)
- [安装依赖](#安装依赖)
- [命令行参数](#命令行参数)
- [命令详解](#命令详解)
- [使用示例](#使用示例)
- [OTA 升级指南](#ota-升级指南)
- [LED 控制指南](#led-控制指南)
- [WiFi 配网指南](#wifi-配网指南)
- [常见问题](#常见问题)
- [注意事项](#注意事项)

---

## 简介

`tools/client.py` 是 ESP32 BLE 设备管理器的命令行客户端，用于通过蓝牙低功耗（BLE）与运行 ble_srv 固件的 ESP32 设备进行通信。支持设备信息查询、固件 OTA 升级、WiFi 配网、LED 控制等功能。

**版本**: 1.2.1

---

## 环境要求

- **操作系统**: Windows 10/11、macOS 10.15+、Linux（需 BlueZ）
- **Python**: 3.8 或更高版本
- **蓝牙**: 支持 BLE（蓝牙 4.0+）的适配器
- **固件**: 设备端需烧录 ble_srv v1.2.1 或兼容版本固件

---

## 安装依赖

```bash
pip install bleak>=0.20.0
```

验证安装：

```bash
python tools/client.py --version
```

正常输出：
```
client.py 1.2.1
```

---

## 命令行参数

### 通用参数

| 参数 | 缩写 | 说明 |
|------|------|------|
| `--help` | `-h` | 显示帮助信息 |
| `--version` | `-v` | 显示版本号 |
| `--device <名称>` | `-d` | 设备名称前缀（不指定则扫描选择） |
| `--timeout <秒>` | | 扫描超时时间（默认 5 秒） |

### 命令列表（使用 `-c <命令>` 指定）

| 命令 | 说明 | 必需参数 |
|------|------|----------|
| `scan` | 扫描 BLE 设备 | - |
| `info` | 读取设备综合信息 | - |
| `memory` | 读取内存详细信息 | - |
| `cpu` | 读取 CPU 详细信息 | - |
| `flash` | 读取 Flash 详细信息 | - |
| `partition` | 读取所有分区信息 | - |
| `restart` | 重启设备 | - |
| `ota-bt` | 蓝牙 OTA 固件升级 | `-f <固件文件>` |
| `ota-url` | URL OTA 固件升级 | `--url <URL>` |
| `wifi-status` | 查看 WiFi 连接状态 | - |
| `wifi-connect` | 连接 WiFi | `--ssid <SSID>` |
| `wifi-disconnect` | 断开 WiFi 连接 | - |
| `wifi-forget` | 清除保存的 WiFi 凭据 | - |
| `ntp-sync` | NTP 时间同步 | - |
| `led-on` | 打开 LED | - |
| `led-off` | 关闭 LED | - |
| `led-color` | 设置 LED 颜色 | `--color <十六进制RGB>` |
| `led-status` | 查看 LED 状态 | - |
| `led-effect` | 设置 LED 特效 | `--effect <类型>` |

### OTA 参数

| 参数 | 说明 |
|------|------|
| `-f, --firmware <路径>` | OTA 固件文件路径（蓝牙 OTA 用） |
| `--url <URL>` | 固件 URL 地址（URL OTA 用） |

### WiFi 参数

| 参数 | 说明 |
|------|------|
| `--ssid <SSID>` | WiFi 名称（必需） |
| `--password <密码>` | WiFi 密码（可选，默认空） |

### LED 参数

| 参数 | 说明 |
|------|------|
| `--color <颜色>` | LED 颜色（十六进制 RGB，如 FF0000=红） |
| `--effect <特效>` | LED 特效类型 |
| `--speed <速度>` | LED 特效速度（1-255，默认 50，数值越小越快） |

**LED 特效类型**:
- `none` - 无特效
- `breath` - 呼吸灯
- `blink` - 闪烁
- `rainbow` - 彩虹
- `strobe` - 频闪

---

## 命令详解

### 1. 扫描设备 (scan)

扫描周围的 BLE 设备并列出匹配的设备。

```bash
python tools/client.py scan [--timeout 5]
```

- 不指定 `-d` 时列出所有 BLE 设备
- 指定 `-d <前缀>` 时只列出名称匹配前缀的设备
- RSSI 值表示信号强度（越接近 0 信号越强）
- 跨平台支持：Windows (WinRT) 和 macOS (CoreBluetooth)

### 2. 设备信息 (info)

读取设备综合信息，包括芯片型号、MAC 地址、固件版本、运行时间、温度等。

```bash
python tools/client.py -d BLE-SRV info
```

信息包含：
- 芯片型号、核心数、 revision
- MAC 地址
- 固件版本
- CPU 频率
- 芯片温度（如支持）
- 运行时间（系统 uptime）
- 上次重启原因

### 3. 内存信息 (memory)

读取详细的内存使用情况。

```bash
python tools/client.py -d BLE-SRV memory
```

信息包含：
- 堆内存总大小/空闲大小/最小空闲
- 内部内存使用情况
- SPI RAM 使用情况（如支持）

### 4. CPU 信息 (cpu)

读取 CPU 详细信息。

```bash
python tools/client.py -d BLE-SRV cpu
```

信息包含：
- CPU 频率
- CPU 核心数
- CPU 使用率
- 任务数量
- 芯片 revision
- IDF 版本
- 运行时间

### 5. Flash 信息 (flash)

读取 Flash 详细信息。

```bash
python tools/client.py -d BLE-SRV flash
```

信息包含：
- Flash 总大小/空闲大小
- Flash 速度
- 分区数量
- 当前运行分区名称

### 6. 分区信息 (partition)

读取所有分区的详细信息。

```bash
python tools/client.py -d BLE-SRV partition
```

每个分区显示：
- 分区名称
- 分区类型
- 起始地址
- 大小
- 标签/子类型

### 7. 重启设备 (restart)

发送重启命令让设备重启。

```bash
python tools/client.py -d BLE-SRV restart
```

**注意**: 发送命令后设备会立即重启，蓝牙连接会断开。

---

## OTA 升级指南

### 蓝牙 OTA (ota-bt)

通过蓝牙传输固件进行升级。

```bash
python tools/client.py -d BLE-SRV ota-bt -f build/ble_srv_example.bin
```

**传输过程**:
1. 读取固件文件，解析版本号、计算 CRC32
2. 发送 OTA 启动命令，等待设备确认
3. 使用 12 包滑动窗口协议传输数据
4. 每 12 包等待设备 ACK 确认
5. 传输完成后发送校验命令
6. 校验通过后发送应用命令
7. 设备 3 秒后自动重启

**进度显示**:
```
[==============================] 100%  45.2KB/s ETA:  -- | 写入: 1289.6KB/1289.6KB
```

### URL OTA (ota-url)

让设备从指定 URL 下载固件进行升级。

```bash
python tools/client.py -d BLE-SRV ota-url --url http://example.com/firmware.bin
```

**前提条件**:
- 设备必须先连接 WiFi
- URL 必须是 HTTP 或 HTTPS 地址
- 设备必须能访问该 URL

**注意**: URL 最大长度为 256 字节。

---

## LED 控制指南

### 开关 LED

```bash
# 打开 LED
python tools/client.py -d BLE-SRV led-on

# 关闭 LED
python tools/client.py -d BLE-SRV led-off
```

### 设置颜色

使用十六进制 RGB 格式设置 LED 颜色：

```bash
# 红色
python tools/client.py -d BLE-SRV led-color --color FF0000

# 绿色
python tools/client.py -d BLE-SRV led-color --color 00FF00

# 蓝色
python tools/client.py -d BLE-SRV led-color --color 0000FF

# 白色
python tools/client.py -d BLE-SRV led-color --color FFFFFF

# 橙色
python tools/client.py -d BLE-SRV led-color --color FFA500
```

**颜色格式**: RRGGBB（红、绿、蓝各 2 位十六进制，00-FF）

### 设置特效

```bash
# 呼吸灯（速度默认 50）
python tools/client.py -d BLE-SRV led-effect --effect breath

# 快速闪烁（速度 20）
python tools/client.py -d BLE-SRV led-effect --effect blink --speed 20

# 慢速彩虹（速度 100）
python tools/client.py -d BLE-SRV led-effect --effect rainbow --speed 100

# 关闭特效
python tools/client.py -d BLE-SRV led-effect --effect none
```

### 查看 LED 状态

```bash
python tools/client.py -d BLE-SRV led-status
```

---

## WiFi 配网指南

### 连接 WiFi

```bash
# 有密码的网络
python tools/client.py -d BLE-SRV wifi-connect --ssid MyWiFi --password 12345678

# 开放网络（无密码）
python tools/client.py -d BLE-SRV wifi-connect --ssid FreeWiFi
```

**注意**:
- SSID 最大长度 32 字节
- 密码最大长度 64 字节
- WiFi 凭据会保存在 NVS 中，重启后自动连接

### 查看 WiFi 状态

```bash
python tools/client.py -d BLE-SRV wifi-status
```

显示：
- 连接状态
- SSID
- IP 地址
- 信号强度（RSSI）

### 断开 WiFi

```bash
python tools/client.py -d BLE-SRV wifi-disconnect
```

### 清除 WiFi 凭据

```bash
python tools/client.py -d BLE-SRV wifi-forget
```

清除 NVS 中保存的 WiFi 密码，重启后不会自动连接。

### NTP 时间同步

```bash
python tools/client.py -d BLE-SRV ntp-sync
```

触发 NTP 时间同步（需要设备已连接 WiFi）。

---

## 使用示例

### 完整工作流示例

```bash
# 1. 扫描设备
python tools/client.py scan

# 2. 查看设备信息
python tools/client.py -d BLE-SRV info

# 3. 连接 WiFi（URL OTA 需要）
python tools/client.py -d BLE-SRV wifi-connect --ssid MyWiFi --password 12345678

# 4. 查看 WiFi 状态确认连接成功
python tools/client.py -d BLE-SRV wifi-status

# 5. 设置 LED 为蓝色呼吸灯
python tools/client.py -d BLE-SRV led-color --color 0000FF
python tools/client.py -d BLE-SRV led-effect --effect breath --speed 60

# 6. 蓝牙 OTA 升级
python tools/client.py -d BLE-SRV ota-bt -f build/ble_srv_example.bin

# 7. OTA 完成后设备会自动重启
```

### 批量操作示例

```bash
# 读取所有系统信息
python tools/client.py -d BLE-SRV info
python tools/client.py -d BLE-SRV memory
python tools/client.py -d BLE-SRV cpu
python tools/client.py -d BLE-SRV flash

# 查看所有分区
python tools/client.py -d BLE-SRV partition
```

---

## 常见问题

### Q: 扫描不到设备？

**A**: 请检查：
1. 设备已上电并正在广播（BLE-SRV-XXXXXX）
2. 电脑蓝牙已开启
3. Windows：在设置中允许应用访问蓝牙
4. macOS：在系统设置 → 隐私与安全性 → 蓝牙中允许终端访问
5. Linux：确保 bluetoothd 服务正在运行，且用户在 bluetooth 组中
6. 尝试靠近设备，避免信号干扰

### Q: 连接失败？

**A**: 可能原因：
1. 设备距离过远或信号差
2. 设备已被其他设备连接
3. 设备广播名前缀不匹配
4. Windows 蓝牙缓存问题：尝试重启蓝牙或电脑
5. 使用 `use_cached=False`（客户端已默认使用）

### Q: OTA 传输中出现 ACK 超时？

**A**: 这是正常的丢包重传机制，客户端会自动重传。如果连续超时：
1. 确保设备靠近电脑
2. 关闭其他蓝牙设备（如蓝牙耳机、鼠标）
3. 避免 WiFi 和蓝牙同时使用同一频段
4. 重启设备后再次尝试

### Q: OTA 提示"固件版本相同"或"远程固件版本更旧"？

**A**: 版本检查机制：
- `VERSION_SAME (0x0C)`: 设备已有相同版本，拒绝升级
- `VERSION_DOWNGRADE (0x0B)`: 尝试刷入更旧版本，拒绝升级
- 如需强制刷入相同版本，需修改固件版本号后重新编译

### Q: OTA 后设备不工作？

**A**:
1. **每次 OTA 建议重启设备**
2. OTA 失败需要重启设备后再次 OTA
3. 如果设备变砖，需要通过串口重新烧录固件

### Q: LED 颜色不对？

**A**: 确认使用 RGB 顺序（RRGGBB）：
- FF0000 = 红色
- 00FF00 = 绿色
- 0000FF = 蓝色
- WS2812 灯带颜色顺序可在固件 menuconfig 中配置

### Q: URL OTA 失败提示"网络未连接"？

**A**: URL OTA 需要设备先连接 WiFi：
1. 先用 `wifi-connect` 命令连接 WiFi
2. 用 `wifi-status` 确认已获取 IP 地址
3. 确保 URL 地址正确且设备能访问

### Q: Ctrl+C 中止 OTA 后设备状态异常？

**A**:
1. 客户端会发送中止命令
2. 设备会进入 ABORTED 状态
3. 300ms 后自动重置为 IDLE
4. 建议重启设备后再次 OTA

---

## 注意事项

### 重要安全提示

1. **OTA 升级过程中不要断开蓝牙连接或断电**，否则可能导致固件损坏
2. **每次 OTA 后建议重启设备**，确保新固件正常加载
3. **OTA 失败必须重启设备后再次尝试**，不要连续重试
4. 保留一个可用的串口烧录方式以备设备变砖时恢复

### 蓝牙连接注意

5. 设备名使用前缀匹配，`-d BLE` 会匹配所有以 BLE 开头的设备
6. 不指定 `-d` 参数时会扫描并列出所有设备供选择
7. Windows 系统必须使用 `use_cached=False`（客户端已默认处理）
8. 连接新设备前会自动清理旧连接并等待 300ms

### OTA 传输注意

9. 蓝牙 OTA 使用 12 包滑动窗口 ACK 协议保证可靠性
10. 传输速度取决于 MTU 和信号质量，典型速度 20-50KB/s
11. 固件传输完成后进行 CRC32 校验，确保数据完整性
12. 按 **Ctrl+C** 可安全中止正在进行的 OTA 传输
13. OTA 成功应用后设备会在 3 秒后自动重启

### LED 使用注意

14. LED 颜色为 RGB 格式（RRGGBB），不是 GRB
15. 特效速度范围 1-255，数值越小变化越快
16. 不使用特效时设置为 `none` 以节省功耗

### WiFi 使用注意

17. WiFi 密码保存在 NVS 中，重启后自动连接
18. 使用 `wifi-forget` 可清除保存的凭据
19. URL OTA 前必须先确认 WiFi 已连接成功
20. NTP 同步需要 WiFi 连接

### 其他

21. 命令执行失败返回非零退出码，可用于脚本判断
22. 分区信息读取需要逐分区查询，设备较多时耗时较长
23. 温度传感器不是所有芯片都支持，不支持时会显示提示
