# ESP-IDF BLE Service

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0%2B-blue)](https://docs.espressif.com/projects/esp-idf/)
[![License](https://img.shields.io/badge/license-Apache--2.0-green)](LICENSE)
[![Target](https://img.shields.io/badge/target-ESP32--S2%2FS3%2FC5%2FC6%2FH2-orange)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Version](https://img.shields.io/badge/version-2.1.0-blueviolet)](ble_srv/idf_component.yml)


基于 NimBLE 的 ESP32 BLE 服务组件，提供设备管理、OTA 固件升级、WiFi 配网、WS2812 LED 控制等功能。

**版本**: 2.1.0 | **协议栈**: NimBLE | **兼容**: ESP-IDF v5.x / v6.x

## 功能特性

- **设备信息查询** — 芯片型号、Flash 大小、MAC 地址、固件版本、CPU 频率、核心数、温度、运行时间、重启原因
- **内存/CPU/Flash/分区监控** — 堆内存使用、任务数、CPU使用率、分区列表、运行分区信息
- **OTA 固件升级** — 蓝牙/URL双模式、12包滑动窗口ACK协议、CRC32校验、断连自动重置、进度通知、版本检查、OTA日志实时推送
- **WiFi 配网** — BLE 写入 SSID/密码，NVS 持久化，凭据删除，NTP 同步
- **WS2812 LED 控制** — RGB 颜色设置、呼吸灯/闪烁/彩虹/频闪特效（非阻塞切换）
- **应用层认证** — BLE连接后通过密码写入GATT特征进行认证，认证失败主动断开
- **设备日志推送** — OTA升级过程中的详细日志通过BLE NOTIFY实时推送到客户端
- **自定义命令** — 新增0xFFEA自定义命令GATT特征（WRITE+NOTIFY），支持第三方应用扩展功能
- **设备重启** — BLE 远程重启

## 支持芯片

ESP32 / ESP32-S2 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-H2

## 快速开始

### 1. 添加组件

支持以下五种方式：

#### 方式一：直接从 GitHub 引用（推荐）

在你的项目根目录下编辑 `idf_component.yml`（若不存在则创建），添加以下依赖配置：

```yaml
dependencies:
  ble_srv:
    git: https://github.com/your-org/esp-idf-ble-srv.git
    path: ble_srv
    version: v2.1.0   # 可选：指定 tag、分支或 commit hash
```

> `version` 字段可省略，省略时默认拉取仓库默认分支的最新代码。建议指定具体 tag（如 `v2.1.0`）以保证版本可追溯。

配置完成后执行：

```bash
idf.py reconfigure
```

ESP-IDF 组件管理器会自动从 GitHub 克隆并引入 `ble_srv` 组件。

#### 方式二：通过 ESP-IDF 组件管理器引入

```bash
idf.py add-dependency "ble_srv^2.1.0"
```

该命令会自动将依赖添加到 `idf_component.yml` 中，并从 ESP-IDF 组件仓库下载。

#### 方式三：通过本地路径引用

在项目根目录的 `idf_component.yml` 中指定本地路径：

```yaml
dependencies:
  ble_srv:
    path: ../esp-idf-ble-srv/ble_srv   # 相对于项目根目录的路径
```

适用于本地开发调试场景，修改组件代码后可立即生效。

#### 方式四：通过 EXTRA_COMPONENT_DIRS 配置

在项目根目录的 `CMakeLists.txt` 中添加：

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/../esp-idf-ble-srv/ble_srv")
```

或通过环境变量设置（临时生效）：

```bash
export EXTRA_COMPONENT_DIRS="/path/to/esp-idf-ble-srv/ble_srv"
idf.py build
```

> **注意**：ESP-IDF v5.x/v6.x 不推荐在 `main/CMakeLists.txt` 中使用 `add_subdirectory`，会导致构建系统解析组件依赖时出错。应使用组件化方式接入。

#### 方式五：手动复制组件

将 `ble_srv` 目录复制到你的项目 `components/` 目录下。

```bash
cp -r /path/to/esp-idf-ble-srv/ble_srv /path/to/your-project/components/
```

适用于离线开发或需要完全隔离组件代码的场景。

### 2. 配置 menuconfig

```bash
idf.py menuconfig
```

```
Component config → BLE Service:
  [*] Enable BLE Service
      Name prefix (BLE-SRV)          # BLE 广播名前缀
  [*] Enable BLE Authentication      # 应用层认证（可选）
      PIN code (112233)              # 认证密码
  [*] Enable WiFi Provisioner        # WiFi 配网（可选）
  [*] Enable NTP Time Sync           # NTP 同步（可选）
  [*] Enable WS2812 LED Control      # LED 控制（可选）
  [*] Enable BLE OTA                 # 蓝牙OTA（默认启用）
  [*] Enable URL OTA                 # URL OTA（默认启用）
```

### 3. 初始化

```c
#include "ble_srv.h"

void app_main(void)
{
    ble_srv_init();
}
```

### 4. 编译运行

```bash
idf.py build flash monitor
```

设备将广播 `BLE-SRV-XXXXXX`（前缀 + MAC 后三字节），等待 BLE 连接。

## BLE GATT 服务

| 服务 | UUID | 功能 | 条件编译 |
|------|------|------|----------|
| Device Service | 0xFFE0 | 设备信息、重启、认证、日志、自定义命令 | 默认启用 |
| OTA Service | 0xFFD0 | 固件升级 | 默认启用 |
| WiFi Service | 0xFFC0 | WiFi 配网 | CONFIG_BLE_SRV_WIFI_PROVISIONER |
| LED Service | 0xFFB0 | LED 控制 | CONFIG_BLE_SRV_LED |

完整使用文档见：
- [docs/BLE_SRV_MODULE.md](docs/BLE_SRV_MODULE.md) — ble_srv 固件模组详细使用说明（API参考、配置选项、GATT协议、OTA机制）
- [docs/PYTHON_CLI.md](docs/PYTHON_CLI.md) — 命令行客户端详细使用说明
- [docs/PYTHON_GUI.md](docs/PYTHON_GUI.md) — 图形界面客户端详细使用说明

## Python 客户端

项目附带两个 BLE 客户端工具：

### 命令行客户端 (tools/client.py)

依赖 `bleak` 库：

```bash
pip install bleak

# 查看版本
python tools/client.py --version

# 扫描设备
python tools/client.py scan

# 查看设备信息（包含温度）
python tools/client.py -d BLE-SRV info

# 查看内存/CPU/Flash/分区信息
python tools/client.py -d BLE-SRV memory
python tools/client.py -d BLE-SRV cpu
python tools/client.py -d BLE-SRV flash
python tools/client.py -d BLE-SRV partition

# 蓝牙 OTA 升级
python tools/client.py -d BLE-SRV ota-bt -f firmware.bin

# URL OTA 升级（需先连接WiFi）
python tools/client.py -d BLE-SRV ota-url --url http://example.com/firmware.bin

# WiFi 配网
python tools/client.py -d BLE-SRV wifi-connect --ssid MyWiFi --password 12345678
python tools/client.py -d BLE-SRV wifi-status
python tools/client.py -d BLE-SRV wifi-disconnect
python tools/client.py -d BLE-SRV wifi-forget

# NTP 时间同步
python tools/client.py -d BLE-SRV ntp-sync

# LED 控制
python tools/client.py -d BLE-SRV led-on
python tools/client.py -d BLE-SRV led-off
python tools/client.py -d BLE-SRV led-color --color FF0000
python tools/client.py -d BLE-SRV led-status
python tools/client.py -d BLE-SRV led-effect --effect breath --speed 80

# 重启设备
python tools/client.py -d BLE-SRV restart

# 查看帮助
python tools/client.py -h
```

### 图形界面客户端 (tools/client_gui.py)

依赖 `flet` 和 `bleak` 库：

```bash
pip install flet bleak

# 正常启动
python tools/client_gui.py

# 查看版本
python tools/client_gui.py --version

# 调试模式（自动清理 __pycache__）
python tools/client_gui.py --debug

# 查看帮助
python tools/client_gui.py -h
```

## OTA 使用注意事项

1. **每次OTA建议重启设备**，OTA失败的话需要重启设备再次OTA
2. **蓝牙OTA**时建议关闭其他蓝牙设备以避免干扰，保持设备靠近电脑
3. **URL OTA**需要设备先连接WiFi，确保URL可访问
4. OTA过程中**不要断开蓝牙连接**，否则会导致升级失败
5. 按 **Ctrl+C** 可中止正在进行的CLI蓝牙OTA传输
6. OTA完成后设备会在3秒后自动重启应用新固件

## 文件结构

```
ble_srv/
  include/
    ble_srv.h              # 主聚合头文件
    ble_srv_gatt.h         # GATT 服务与 val_handle
    ble_srv_ota.h          # OTA 模块（公共层）
    ble_srv_ota_bt.h       # 蓝牙OTA驱动
    ble_srv_ota_url.h      # URL OTA驱动
    ble_srv_wifi.h         # WiFi 模块
    ble_srv_led.h          # LED 模块
    ble_srv_device.h       # 设备信息模块
  src/
    ble_srv_core.c         # NimBLE 初始化、GAP、广播
    ble_srv_gatt.c         # GATT 服务定义与 access callback
    ble_srv_ota.c          # OTA 公共状态机与会话管理
    ble_srv_ota_bt.c       # 蓝牙OTA驱动（滑动窗口ACK协议）
    ble_srv_ota_url.c      # URL OTA驱动
    ble_srv_device.c       # 设备信息读取
    ble_srv_wifi.c         # WiFi 配网
    ble_srv_led.c          # WS2812 LED 驱动（RMT + 非阻塞特效）
    ble_srv_ntp.c          # NTP 时间同步
  CMakeLists.txt
  Kconfig
  idf_component.yml
docs/
  PYTHON_CLI.md            # CLI客户端详细使用说明
  PYTHON_GUI.md            # GUI客户端详细使用说明
tools/
  client.py                # Python BLE 命令行客户端 v2.1.0
  client_gui.py            # Python BLE GUI 客户端入口 v2.1.0
  client/                  # CLI客户端核心模块
  client_gui/              # GUI客户端模块
examples/
  basic/                   # 最小示例项目
```

## 模块说明

| 模块 | 文件 | 职责 |
|------|------|------|
| Core | ble_srv_core.c | NimBLE 初始化、GAP 事件处理、广播管理 |
| GATT | ble_srv_gatt.c | GATT 服务表定义、READ/WRITE access callback |
| OTA Common | ble_srv_ota.c | OTA 状态机、会话互斥、版本检查、校验、应用 |
| OTA BT | ble_srv_ota_bt.c | 蓝牙OTA接收（12包滑动窗口ACK、CRC32校验） |
| OTA URL | ble_srv_ota_url.c | URL OTA（HTTP/HTTPS下载、WiFi状态检查） |
| Device | ble_srv_device.c | 芯片/内存/CPU/Flash/分区/温度信息采集 |
| WiFi | ble_srv_wifi.c | WiFi 凭据管理（NVS）、连接/断开/状态查询 |
| LED | ble_srv_led.c | WS2812 RMT 驱动、非阻塞特效任务 |
| NTP | ble_srv_ntp.c | SNTP 初始化、多服务器配置、时区设置 |

## OTA 传输协议

蓝牙OTA采用基于累积ACK的滑动窗口协议：
- 窗口大小：12包
- 客户端连续发送12包后等待设备ACK
- 设备端每接收12包发送一次累积ACK（包含已接收字节数）
- 超时未收到ACK则重传窗口内数据
- 传输完成后进行CRC32校验确保固件完整性

## 依赖

- ESP-IDF v5.x / v6.x（推荐 v6.0+）
- NimBLE（ESP-IDF 内置）
- MichMich/esp-idf-wifi-provisioner（WiFi配网组件）

## Python 客户端依赖

- bleak >= 0.20.0（BLE库）
- flet >= 0.20.0（GUI库，仅GUI客户端需要）

## 变更记录

### v2.1.0 (2026-07-22)

**设备端**:
- **锁机制重构**: 将原有 5 种分散锁（GATT_LOCK/BT_LOCK/OTA_LOCK/LED_LOCK 等）统一为单一全局递归互斥锁 `ble_srv_lock`，通过 `BLE_SRV_LOCK()/BLE_SRV_UNLOCK()` 宏使用，移除约 100 行锁管理代码
- **BLE 任务化**: 新增 `ble_srv_task.c` 和 `ble_srv_msg.h`，引入 BLE Service 任务和消息机制
- **日志级别控制**: 新增 GATT 命令 `BLE_LOG_HTTP_CMD_SET_LEVEL(0x06)`，支持 E/W/I/D/V 五级动态切换；`ble_srv_log_storage_info_t` 增加 `log_level` 字段
- **删除日志文件功能移除**: 移除 `BLE_LOG_HTTP_CMD_DELETE_ALL` 命令、`ble_srv_log_delete_all_files()` 函数及相关枚举/声明
- **LittleFS 格式化重置计数**: 格式化后重置 NVS 中的 `log_count` 为 0，下次日志文件从 `000001.log` 开始
- **时区提前设置**: `ble_srv_log_init()` 开头设置时区 `setenv("TZ", ..., 1); tzset();`，修复日志文件时间偏移 8 小时问题
- **设备实时时间**: `ble_srv_device_info_t` 结构体新增 `current_time` 字段，通过 `time()` 获取

**客户端 GUI**:
- 删除"删除所有"按钮及相关逻辑
- 新增日志级别下拉框（PopupMenuButton 实现）和标记日志输入框（复用 `BLE_LOG_HTTP_CMD_WRITE_LOG` 命令）
- 操作卡片 UI 统一控件高度（32px）和样式
- HTTP 服务卡片紧凑化布局

**CLI 客户端**:
- 同步删除 `log_delete_all_files` 方法
- 新增 `log_set_level` 方法

**版本号同步**:
- `ble_srv/idf_component.yml`: 2.0.1 → 2.1.0
- `examples/basic/CMakeLists.txt`: 2.0.1 → 2.1.0
- `tools/client.py` / `tools/client_gui.py`: 2.0.1 → 2.1.0
- 文档版本号同步更新

### v2.0.1 (2026-07-19)

**重大变更 (C 固件)**:
- **文件系统迁移**: 日志系统存储后端从 SPIFFS 切换到 LittleFS（`joltwallet/littlefs` 组件）
  - `ble_srv_log.c`: 替换 `esp_spiffs_*` API 为 `esp_littlefs_*` API
  - `ble_srv_log.h`: `BLE_SRV_LOG_STORAGE_SPIFFS` 枚举重命名为 `BLE_SRV_LOG_STORAGE_LITTLEFS`
  - `Kconfig`: `BLE_SRV_LOG_SPIFFS_PATH` 配置项重命名为 `BLE_SRV_LOG_LITTLEFS_PATH`，默认值 `/spiffs` → `/littlefs`；新增 `BLE_SRV_LOG_LITTLEFS_PARTITION` 配置项，默认分区标签 `littlefs`
  - `CMakeLists.txt`: 依赖 `spiffs` 替换为 `littlefs`
  - `idf_component.yml`: 新增 `joltwallet/littlefs` 依赖
  - LittleFS 支持真实目录层级，挂载时自动创建日志目录（不再依赖 SPIFFS 的扁平文件系统）

**客户端同步 (Python)**:
- `tools/client/models.py`: `STORAGE_TYPE_SPIFFS` 重命名为 `STORAGE_TYPE_LITTLEFS`，显示名从 "SPIFFS" 改为 "LittleFS"

**示例项目**:
- `examples/basic/sdkconfig`: 替换 SPIFFS 配置段为 LittleFS 配置段，路径从 `/spiffs` 改为 `/littlefs`；新增 `BLE_SRV_LOG_LITTLEFS_PARTITION` 配置项
- `examples/basic/partitions.csv`: 分区名从 `spiffs` 改为 `littlefs`（subtype 仍为 `spiffs`，joltwallet/littlefs 要求）
- `examples/basic/CMakeLists.txt`: 项目版本号 1.3.1 → 2.0.1

**文档更新**:
- `docs/BLE_SRV_MODULE.md`: 更新日志系统配置章节，新增 LittleFS 分区要求说明
- `docs/PYTHON_CLI.md` / `docs/PYTHON_GUI.md`: 版本号同步更新
- `README.md`: 版本号同步更新，新增 v2.0.1 变更记录

> ⚠️ **不兼容升级说明**: 由于 Kconfig 配置项名变更（`BLE_SRV_LOG_SPIFFS_PATH` → `BLE_SRV_LOG_LITTLEFS_PATH`）和枚举名变更，从 v1.3.1 升级到 v2.0.1 时需要：
> 1. 删除 `sdkconfig` 后执行 `idf.py reconfigure` 重新生成配置
> 2. 检查 `sdkconfig.defaults` 中是否引用了旧配置项名
> 3. 擦除 Flash 中的 spiffs 分区（`idf.py erase-flash`）后重新烧录，避免文件系统格式不兼容

### v1.3.1 (2026-07-15)

**新增功能 (C 固件)**:
- **应用层认证**: 新增 `0xFFE8` 认证特征，BLE 连接后客户端需写入密码认证，认证失败设备主动断开连接
- **设备日志推送**: 新增 `0xFFE9` LOG 特征（NOTIFY），OTA 升级过程中的详细日志实时推送到客户端
- **自定义命令**: 新增 `0xFFEA` 自定义命令特征（WRITE+NOTIFY），支持第三方应用注册回调处理自定义协议
- `ble_srv_gatt.c`: 新增 `ble_srv_gatt_set_custom_cmd_callback()` 和 `ble_srv_gatt_custom_cmd_notify()` API

**客户端增强 (Python)**:
- **GUI 自定义命令 Tab**: 新增通讯日志显示区、命令输入框、ASCII/HEX 格式切换、定时循环发送（支持 10ms-60s 间隔）
- **PIN 码输入**: GUI 左侧面板新增密码输入框，CLI 新增 `--pin` 参数
- **设备日志订阅**: GUI 自动订阅设备日志通知并显示在 BLE 传输日志面板
- **连接安全**: 连接成功后自动发送 PIN 认证作为第一个 GATT 命令

**示例项目**:
- `examples/basic`: 更新 sdkconfig，新增认证配置 `CONFIG_BLE_SRV_AUTH_ENABLED` 和 `CONFIG_BLE_SRV_AUTH_PIN`

### v1.2.1 (2025-07-13)

**固件修复 (C)**:
- `ble_srv_core.c`: 添加 `state_lock` 互斥锁保护 `s_conn_handle` / `s_advertising` 并发访问，修复 GAP 事件与广播状态竞争条件
- `ble_srv_core.c`: `ble_hs_util_ensure_addr` 失败时返回错误并记录日志，替换 `assert` 防止设备崩溃
- `ble_srv_led.c`: 所有信号量获取超时从 `portMAX_DELAY` 改为 `5000ms`，防止死锁

**配置修复**:
- `sdkconfig.defaults`: 修正配置项名 `CONFIG_BLE_SRV_NAME_PREFIX` -> `CONFIG_BLE_SRV_ADV_NAME_PREFIX`

**GUI 客户端修复 (Python)**:
- `ble_core.py`: 扫描方式改为 `BleakScanner` 回调方式，修复 macOS 下 RSSI 始终为 -100dBm 的问题
- `ble_core.py`: 提取 `OTA_ERROR_NAMES` 常量，消除重复定义

## License

Apache License 2.0
