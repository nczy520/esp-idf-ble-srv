# ble_srv 模组使用说明 v2.1.1

## 目录

- [简介](#简介)
- [功能特性](#功能特性)
- [支持芯片](#支持芯片)
- [依赖组件](#依赖组件)
- [快速开始](#快速开始)
- [项目配置](#项目配置)
- [menuconfig 配置选项](#menuconfig-配置选项)
- [API 参考](#api-参考)
- [GATT 服务与UUID](#gatt-服务与uuid)
- [OTA 固件升级](#ota-固件升级)
- [WiFi 配网](#wifi-配网)
- [WS2812 LED 控制](#ws2812-led-控制)
- [设备信息](#设备信息)
- [温度传感器](#温度传感器)
- [初始化与反初始化顺序](#初始化与反初始化顺序)
- [注意事项](#注意事项)
- [常见问题](#常见问题)
- [故障排查](#故障排查)

---

## 简介

`ble_srv` 是基于 ESP-IDF NimBLE 协议栈的 BLE（蓝牙低功耗）服务组件，为 ESP32 系列芯片提供设备管理、OTA 固件升级、WiFi 配网、WS2812 LED 控制等功能。组件采用模块化设计，各功能可通过 menuconfig 独立开关。

**版本**: 2.1.1
**协议栈**: NimBLE（ESP-IDF 内置）
**兼容**: ESP-IDF v5.x / v6.x（推荐 v6.0+）

---

## 功能特性

### 核心功能
- ✅ NimBLE 主机+外设模式，GATT 服务自动注册
- ✅ BLE 广播（名称前缀 + MAC 后3字节）
- ✅ 连接状态管理
- ✅ 设备远程重启

### 设备信息
- ✅ 芯片型号、MAC 地址、固件版本
- ✅ CPU 频率、核心数、芯片 revision
- ✅ 运行时间（Uptime）、上次重启原因
- ✅ 堆内存/内部内存/PSRAM 使用统计
- ✅ CPU 使用率、任务数、IDF 版本
- ✅ Flash 大小/空闲/速度、分区列表
- ✅ 当前运行分区名称

### OTA 固件升级
- ✅ 蓝牙 OTA（BT OTA）：12包滑动窗口 ACK 协议
- ✅ URL OTA（HTTP/HTTPS 下载）
- ✅ CRC32 完整性校验
- ✅ 固件版本检查（防降级/重复刷入）
- ✅ 进度通知
- ✅ OTA 详细日志实时推送（通过 0xFFE9 NOTIFY）
- ✅ 断连自动重置状态
- ✅ 中止命令支持
- ✅ APPLY_OK 后 3 秒自动重启
- ✅ 终态自动重置（支持多次 OTA）

### 应用层认证
- ✅ BLE 连接后密码写入认证（0xFFE8）
- ✅ 认证失败主动断开连接
- ✅ 密码通过 menuconfig 配置
- ✅ 每次连接都需重新认证

### 自定义命令
- ✅ 0xFFEA 自定义命令 GATT 特征（WRITE + NOTIFY）
- ✅ 第三方应用可注册回调处理自定义协议
- ✅ 支持通过 NOTIFY 返回响应数据
- ✅ 方便功能扩展而不修改核心代码

### WiFi 配网
- ✅ BLE 写入 SSID/密码
- ✅ NVS 持久化存储凭据
- ✅ 自动重连
- ✅ 连接状态查询
- ✅ 凭据删除
- ✅ NTP 时间同步（多服务器、时区配置）

### LED 控制
- ✅ WS2812 RGB LED 控制（RMT 外设驱动）
- ✅ RGB 颜色设置（0-255 每通道）
- ✅ 非阻塞特效：呼吸灯/闪烁/彩虹/频闪
- ✅ 特效速度可调
- ✅ 开/关控制

### 温度传感器
- ✅ 内置温度传感器读取（支持的芯片）
- ✅ 温度范围可配置
- ✅ 温度数据整合到设备信息中

---

## 支持芯片

| 芯片系列 | 蓝牙 | 温度传感器 | 备注 |
|----------|------|------------|------|
| ESP32 | ✅ | ❌ | 无内置温度传感器 |
| ESP32-S2 | ❌ | ✅ | 无蓝牙，不支持本组件 |
| ESP32-S3 | ✅ | ✅ | 推荐 |
| ESP32-C3 | ✅ | ✅ | 推荐 |
| ESP32-C5 | ✅ | ✅ | ESP-IDF v5.1+ |
| ESP32-C6 | ✅ | ✅ | 推荐 |
| ESP32-C61 | ✅ | ✅ | ESP-IDF v5.2+ |
| ESP32-H2 | ✅ | ✅ | 支持 Thread/Zigbee |
| ESP32-P4 | ❌ | ✅ | 无蓝牙，不支持本组件 |

**注意**: 无蓝牙功能的芯片无法使用本组件。

---

## 依赖组件

### ESP-IDF 内置组件（必需）
- `bt` — NimBLE 蓝牙协议栈
- `app_update` — OTA 固件升级
- `nvs_flash` — NVS 非易失性存储
- `spi_flash` — Flash 操作
- `mbedtls` — SSL/TLS（HTTPS OTA）
- `esp_http_client` — HTTP 客户端（URL OTA）
- `esp_https_ota` — HTTPS OTA

### ESP-IDF 内置组件（可选，条件编译）
- `esp_driver_rmt` — RMT 外设驱动（LED 控制）
- `esp_wifi` — WiFi（WiFi 配网）
- `esp_netif` — 网络接口（WiFi）
- `esp_http_server` — HTTP 服务器（WiFi 配网组件依赖）
- `esp_driver_tsens` — 温度传感器驱动

### 外部组件
- `MichMich/esp-idf-wifi-provisioner` — WiFi 配网组件（启用 WiFi 时必需）

---

## 快速开始

### 1. 添加组件

将 `ble_srv` 目录复制到项目的 `components/` 目录下：

```bash
cp -r ble_srv your_project/components/
```

或通过 ESP-IDF 组件管理器添加：

```bash
idf.py add-dependency "ble_srv^2.1.1"
```

### 2. 配置项目

```bash
idf.py menuconfig
```

在 `Component config → BLE Service` 中启用需要的功能。至少需启用：
- `[*] Enable BLE Service`

### 3. 最小代码示例

```c
#include "nvs_flash.h"
#include "ble_srv.h"

void app_main(void)
{
    // 初始化 NVS（必需）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化 WiFi 配网（如启用）
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    ble_srv_wifi_provisioner_init();
#endif

    // 初始化 BLE 服务
    if (!ble_srv_init()) {
        ESP_LOGE("APP", "BLE Service init failed");
        return;
    }

    // 自动连接已保存的 WiFi（如启用）
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    ble_srv_wifi_auto_connect();
#endif

    ESP_LOGI("APP", "BLE Service started");
}
```

### 4. 编译烧录

```bash
idf.py build flash monitor
```

设备启动后将广播 `BLE-SRV-XXXXXX`（XXXXXX 为 MAC 地址后3字节）。

---

## 项目配置

### CMakeLists.txt 配置

确保项目 `CMakeLists.txt` 设置了正确的最小版本和组件依赖：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_project VERSION 1.0.0)
```

**重要**: 项目版本号会作为固件版本号上报。版本号支持最多4段（major.minor.patch.build）。

### sdkconfig.defaults 推荐配置

```
# 启用 NimBLE
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y

# 启用 BLE Service
CONFIG_BLE_SRV_ENABLED=y
CONFIG_BLE_SRV_ADV_NAME_PREFIX="BLE-SRV"

# OTA 配置（根据需要调整）
CONFIG_BLE_SRV_OTA_BT_ENABLED=y
CONFIG_BLE_SRV_OTA_BT_NOTIFY_INTERVAL=10

# 分区表（OTA 需要双分区）
CONFIG_PARTITION_TABLE_TWO_OTA=y
```

### 分区表要求

OTA 功能需要双 OTA 分区表。在 menuconfig 中配置：
```
Partition Table → Partition Table → Factory app, two OTA definitions
```

---

## menuconfig 配置选项

### 基础配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_ENABLED` | bool | y | 启用 BLE Service 组件 |

### 广播配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_ADV_NAME_PREFIX` | string | "BLE-SRV" | BLE 设备名称前缀 |
| `BLE_SRV_ADV_INTERVAL_MIN` | int | 32 | 广播最小间隔（ms） |
| `BLE_SRV_ADV_INTERVAL_MAX` | int | 64 | 广播最大间隔（ms） |

**设备名称格式**: `{前缀}-{MAC后3字节}`，例如 `BLE-SRV-A1B2C3`

### 蓝牙 OTA 配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_OTA_BT_ENABLED` | bool | y | 启用蓝牙 OTA |
| `BLE_SRV_OTA_BT_NOTIFY_INTERVAL` | int | 10 | 进度通知间隔（每写入N包通知一次） |

**通知间隔说明**: 值越小进度条越平滑但 BLE 开销越大；推荐值 5-20。

### URL OTA 配置

URL OTA 依赖 WiFi 模块（`BLE_SRV_WIFI_ENABLED` 必须先启用）。启用后可通过 BLE 写入 URL 或使用默认 URL 从 HTTPS 服务器下载固件。

| 配置项 | 类型 | 默认值 | 依赖 | 说明 |
|--------|------|--------|------|------|
| `BLE_SRV_OTA_URL_ENABLED` | bool | n | `BLE_SRV_WIFI_ENABLED` | 启用 URL OTA |
| `BLE_SRV_OTA_URL_DEFAULT` | string | "" | `BLE_SRV_OTA_URL_ENABLED` | 默认固件 URL（可被运行时 BLE 命令覆盖） |
| `BLE_SRV_OTA_URL_SKIP_CERT_CHECK` | bool | n | `BLE_SRV_OTA_URL_ENABLED` | 跳过 HTTPS 证书 CN/SAN/SNI 验证 |
| `BLE_SRV_OTA_URL_ALLOW_DOWNGRADE` | bool | n | `BLE_SRV_OTA_URL_ENABLED` | 允许降级到旧版本固件 |

**配置示例**:
```
CONFIG_BLE_SRV_WIFI_ENABLED=y
CONFIG_BLE_SRV_OTA_URL_ENABLED=y
CONFIG_BLE_SRV_OTA_URL_DEFAULT="https://example.com/firmware.bin"
CONFIG_BLE_SRV_OTA_URL_SKIP_CERT_CHECK=n
CONFIG_BLE_SRV_OTA_URL_ALLOW_DOWNGRADE=n
```

**安全警告**:
- `BLE_SRV_OTA_URL_SKIP_CERT_CHECK=y` 会禁用所有服务器身份验证（CN/SAN/SNI），仅用于自签名证书测试环境，**生产环境禁止启用**，否则易遭受中间人攻击。
- `BLE_SRV_OTA_URL_ALLOW_DOWNGRADE=y` 允许刷入比当前版本更旧的固件，默认关闭以防止版本回退攻击。仅在需要回滚到旧版本时启用。

**版本检查规则**: 远程版本 > 本地版本 → 下载；远程版本 == 本地版本 → 返回 `BLE_OTA_ERR_VERSION_SAME (0x0C)`；远程版本 < 本地版本 → 返回 `BLE_OTA_ERR_VERSION_DOWNGRADE (0x0B)`（除非启用 `ALLOW_DOWNGRADE`）。

### 认证配置

应用层 PIN 认证（独立于 BLE 安全配对）。启用后客户端必须在 GATT 操作前通过 `0xFFE8` 特征写入正确密码。

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_AUTH_ENABLED` | bool | n | 启用应用层 PIN 认证 |
| `BLE_SRV_AUTH_PIN` | string | "123456" | 默认 PIN 码（1-16 字节，运行时可改） |

**认证流程**:
1. 客户端连接后向 `0xFFE8` 特征写入密码
2. 设备验证密码，失败则发送 `0x00` 通知并断开连接
3. 认证成功后解锁其他 GATT 特征操作

**PIN 码约束**:
- 长度 1-16 字节
- 不局限于数字，可为任意 ASCII 字符
- 修改后立即生效，断电后丢失（除非运行时持久化到 NVS）

> ⚠️ 应用层认证与 BLE 链路层配对（`sm_sc=1`）是两套独立机制。本组件默认启用 LE Secure Connections 配对以兼容 Windows 10/11，应用层 PIN 用于额外控制 GATT 访问权限。

### WiFi 配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_WIFI_ENABLED` | bool | n | 启用 WiFi 配网 |

### NTP 配置（依赖 WiFi）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_NTP_ENABLED` | bool | n | 启用 NTP 时间同步 |
| `BLE_SRV_NTP_TIMEZONE` | string | "CST-8" | 时区字符串（中国标准时间） |
| `BLE_SRV_NTP_SERVER_1` | string | "ntp.aliyun.com" | NTP 服务器1 |
| `BLE_SRV_NTP_SERVER_2` | string | "time1.aliyun.com" | NTP 服务器2 |
| `BLE_SRV_NTP_SERVER_3` | string | "cn.ntp.org.cn" | NTP 服务器3 |
| `BLE_SRV_NTP_SERVER_4` | string | "time.windows.com" | NTP 服务器4 |
| `BLE_SRV_NTP_SERVER_5` | string | "pool.ntp.org" | NTP 服务器5 |

### LED 配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_LED_ENABLED` | bool | n | 启用 WS2812 LED 控制 |
| `BLE_SRV_LED_GPIO` | int | 见说明 | WS2812 数据引脚 |

**默认 GPIO 引脚**:
- ESP32: GPIO 2
- ESP32-S2: GPIO 18
- ESP32-S3: GPIO 21
- ESP32-C3/C6/H2: GPIO 10

### 温度传感器配置

内置温度传感器读取（仅支持 ESP32-S2/S3/C3/C5/C6/C61/H2/P4，ESP32 无内置温度传感器）。温度数据整合到设备信息结构体中。

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `BLE_SRV_TEMP_SENSOR_ENABLED` | bool | y | 启用内置温度传感器 |
| `BLE_SRV_TEMP_SENSOR_RANGE_MIN` | int | -10 | 温度测量下限（°C） |
| `BLE_SRV_TEMP_SENSOR_RANGE_MAX` | int | 80 | 温度测量上限（°C） |

**配置示例**:
```
CONFIG_BLE_SRV_TEMP_SENSOR_ENABLED=y
CONFIG_BLE_SRV_TEMP_SENSOR_RANGE_MIN=-10
CONFIG_BLE_SRV_TEMP_SENSOR_RANGE_MAX=80
```

### 日志系统配置

日志系统支持将运行日志存储到 LittleFS 或 SD 卡，并提供 HTTP 服务器接口用于浏览和下载日志文件。日志队列使用 PSRAM 静态分配，支持 512KB 总容量。

| 配置项 | 类型 | 默认值 | 依赖 | 说明 |
|--------|------|--------|------|------|
| `BLE_SRV_LOG_ENABLED` | bool | n | - | 启用日志系统 |
| `BLE_SRV_LOG_SD_ENABLED` | bool | n | `BLE_SRV_LOG_ENABLED` | 启用 SD 卡存储支持（优先于 LittleFS） |
| `BLE_SRV_LOG_SD_PATH` | string | "/sdcard" | `BLE_SRV_LOG_SD_ENABLED` | SD 卡挂载路径 |
| `BLE_SRV_LOG_LITTLEFS_PATH` | string | "/littlefs" | `BLE_SRV_LOG_ENABLED` | LittleFS 挂载路径 |
| `BLE_SRV_LOG_LITTLEFS_PARTITION` | string | "littlefs" | `BLE_SRV_LOG_ENABLED` | LittleFS 分区标签（需与 partitions.csv 一致） |
| `BLE_SRV_LOG_DIR` | string | "/log" | `BLE_SRV_LOG_ENABLED` | 日志目录名（位于存储路径下） |
| `BLE_SRV_LOG_QUEUE_SIZE` | int | 1024 | `BLE_SRV_LOG_ENABLED` | 日志队列容量（条目数，PSRAM 分配） |
| `BLE_SRV_LOG_LINE_SIZE` | int | 512 | `BLE_SRV_LOG_ENABLED` | 单条日志最大长度（字节） |
| `BLE_SRV_LOG_FLUSH_INTERVAL_MS` | int | 1000 | `BLE_SRV_LOG_ENABLED` | 周期刷盘间隔（ms） |
| `BLE_SRV_LOG_MAX_FILES` | int | 50 | `BLE_SRV_LOG_ENABLED` | 最大日志文件保留数 |
| `BLE_SRV_LOG_MIN_FREE_SPACE` | int | 256 | `BLE_SRV_LOG_ENABLED` | 最小可用空间（KB） |
| `BLE_SRV_LOG_MAX_FILE_SIZE` | int | 512 | `BLE_SRV_LOG_ENABLED` | 单个日志文件最大大小（KB） |
| `BLE_SRV_LOG_HTTP_PORT` | int | 8080 | `BLE_SRV_LOG_ENABLED` | 日志 HTTP 服务器端口 |

**配置示例**:
```
# 基础日志系统（使用 LittleFS）
CONFIG_BLE_SRV_LOG_ENABLED=y
CONFIG_BLE_SRV_LOG_LITTLEFS_PATH="/littlefs"
CONFIG_BLE_SRV_LOG_DIR="/log"
CONFIG_BLE_SRV_LOG_QUEUE_SIZE=1024
CONFIG_BLE_SRV_LOG_LINE_SIZE=512
CONFIG_BLE_SRV_LOG_FLUSH_INTERVAL_MS=1000
CONFIG_BLE_SRV_LOG_MAX_FILES=50
CONFIG_BLE_SRV_LOG_MAX_FILE_SIZE=512
CONFIG_BLE_SRV_LOG_HTTP_PORT=8080

# 启用 SD 卡支持（需在 LittleFS 之外额外启用）
CONFIG_BLE_SRV_LOG_SD_ENABLED=y
CONFIG_BLE_SRV_LOG_SD_PATH="/sdcard"
CONFIG_BLE_SRV_LOG_MIN_FREE_SPACE=256
```

**内存使用说明**:
- 日志队列总内存 = `BLE_SRV_LOG_QUEUE_SIZE` × `BLE_SRV_LOG_LINE_SIZE`，默认 1024 × 512 = **512KB**
- 队列缓冲区使用 `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 从 PSRAM 分配，避免占用内部 RAM
- 队列使用 `xQueueCreateStatic` 静态创建，控制结构体与缓冲区均位于 PSRAM
- deinit 时先 `vQueueDelete` 销毁队列，再 `heap_caps_free` 释放 PSRAM 缓冲区

**存储选择优先级**: SD 卡（启用时）> LittleFS。SD 卡挂载失败时自动回退到 LittleFS。

**LittleFS 分区要求**:
- 分区表需包含一个 `data, spiffs` 类型的分区（LittleFS 复用 spiffs 子类型，这是 joltwallet/littlefs 组件的标准要求）
- 分区名建议为 `littlefs`，并确保 `BLE_SRV_LOG_LITTLEFS_PARTITION` 配置与分区名一致
- LittleFS 支持真实目录层级，挂载时会自动创建日志目录

**HTTP 服务器**: 启用后可通过浏览器访问 `http://<设备IP>:8080/` 浏览和下载日志文件。需要启用 `CONFIG_HTTPD_URI_MATCH_WILDCARD=y` 以支持通配符路由。

**依赖组件**: 启用日志系统时，`ble_srv/CMakeLists.txt` 中已声明以下组件依赖（无需用户手动添加）：
- `vfs` — 虚拟文件系统
- `fatfs` — FAT 文件系统（SD 卡支持）
- `esp_driver_sdmmc` — SDMMC 驱动（SD 卡支持）
- `esp_http_server` — HTTP 服务器
- `littlefs` — LittleFS 文件系统组件（`joltwallet/littlefs`）

### menuconfig 菜单层级树

通过 `idf.py menuconfig` 进入 `Component config → BLE Service` 即可看到以下菜单结构（仅当对应依赖启用时显示子项）：

```
Component config
└── BLE Service
    [*] Enable BLE Service                                  (BLE_SRV_ENABLED)
    ├── Advertising
    │       (BLE-SRV) Device Name Prefix                   (BLE_SRV_ADV_NAME_PREFIX)
    │       (32) Advertising Interval Min (ms)             (BLE_SRV_ADV_INTERVAL_MIN)
    │       (64) Advertising Interval Max (ms)             (BLE_SRV_ADV_INTERVAL_MAX)
    ├── OTA Bluetooth
    │   [*] Enable Bluetooth OTA                           (BLE_SRV_OTA_BT_ENABLED)
    │       (10) Progress Notify Interval (writes)         (BLE_SRV_OTA_BT_NOTIFY_INTERVAL)
    ├── OTA URL                                            (依赖 BLE_SRV_WIFI_ENABLED)
    │   [ ] Enable URL OTA                                 (BLE_SRV_OTA_URL_ENABLED)
    │       ()  Default Firmware URL                       (BLE_SRV_OTA_URL_DEFAULT)
    │       [ ] Skip Certificate Common Name Check         (BLE_SRV_OTA_URL_SKIP_CERT_CHECK)
    │       [ ] Allow Firmware Downgrade                   (BLE_SRV_OTA_URL_ALLOW_DOWNGRADE)
    ├── WiFi
    │   [ ] Enable WiFi Provisioner                        (BLE_SRV_WIFI_ENABLED)
    ├── NTP                                                (依赖 BLE_SRV_WIFI_ENABLED)
    │   [ ] Enable NTP Time Sync                           (BLE_SRV_NTP_ENABLED)
    │       (CST-8) Timezone                               (BLE_SRV_NTP_TIMEZONE)
    │       (ntp.aliyun.com)     NTP Server 1              (BLE_SRV_NTP_SERVER_1)
    │       (time1.aliyun.com)   NTP Server 2              (BLE_SRV_NTP_SERVER_2)
    │       (cn.ntp.org.cn)      NTP Server 3              (BLE_SRV_NTP_SERVER_3)
    │       (time.windows.com)   NTP Server 4              (BLE_SRV_NTP_SERVER_4)
    │       (pool.ntp.org)       NTP Server 5              (BLE_SRV_NTP_SERVER_5)
    ├── Authentication
    │   [ ] Enable PIN Authentication                      (BLE_SRV_AUTH_ENABLED)
    │       (123456) Default PIN Code                      (BLE_SRV_AUTH_PIN)
    ├── LED
    │   [ ] Enable WS2812 LED Control                      (BLE_SRV_LED_ENABLED)
    │       (21) WS2812 LED GPIO Pin                       (BLE_SRV_LED_GPIO)
    ├── Log System
    │   [ ] Enable Log System                              (BLE_SRV_LOG_ENABLED)
    │       [ ] Enable SD Card Support                     (BLE_SRV_LOG_SD_ENABLED)
    │           (/sdcard) SD Card Mount Path               (BLE_SRV_LOG_SD_PATH)
    │       (/littlefs) LittleFS Mount Path                (BLE_SRV_LOG_LITTLEFS_PATH)
    │       (littlefs)  LittleFS Partition Label            (BLE_SRV_LOG_LITTLEFS_PARTITION)
    │       (/log) Log Directory Name                      (BLE_SRV_LOG_DIR)
    │       (1024) Log Queue Size (entries)                (BLE_SRV_LOG_QUEUE_SIZE)
    │       (512) Maximum Log Line Size (bytes)            (BLE_SRV_LOG_LINE_SIZE)
    │       (1000) Flush Interval (ms)                     (BLE_SRV_LOG_FLUSH_INTERVAL_MS)
    │       (50) Maximum Log Files                         (BLE_SRV_LOG_MAX_FILES)
    │       (256) Minimum Free Space (KB)                  (BLE_SRV_LOG_MIN_FREE_SPACE)
    │       (512) Maximum Log File Size (KB)               (BLE_SRV_LOG_MAX_FILE_SIZE)
    │       (8080) HTTP Server Port                        (BLE_SRV_LOG_HTTP_PORT)
    └── Temperature Sensor
        [*] Enable Temperature Sensor                      (BLE_SRV_TEMP_SENSOR_ENABLED)
            (-10) Temperature Range Min (°C)               (BLE_SRV_TEMP_SENSOR_RANGE_MIN)
            (80)  Temperature Range Max (°C)               (BLE_SRV_TEMP_SENSOR_RANGE_MAX)
```

### 依赖关系图

各功能模块存在以下编译期依赖关系，禁用底层模块时上层模块将自动隐藏：

```
BLE_SRV_ENABLED (顶层开关)
├── Advertising                  (无依赖)
├── OTA Bluetooth                (无依赖)
├── WiFi                         (无依赖)
│   ├── OTA URL                  (依赖 WiFi)
│   └── NTP                      (依赖 WiFi)
├── Authentication               (无依赖)
├── LED                          (无依赖)
├── Log System                   (无依赖)
│   └── Log System SD Card       (依赖 Log System)
└── Temperature Sensor           (无依赖)
```

### 功能与 GATT 服务映射

下表展示 menuconfig 选项启用的功能与对应 GATT 服务的映射关系：

| 功能选项 | GATT 服务 UUID | 服务名称 |
|----------|---------------|----------|
| `BLE_SRV_ENABLED` | 0xFFE0 | Device Service（设备信息、重启、认证、日志、自定义命令） |
| `BLE_SRV_OTA_BT_ENABLED` 或 `BLE_SRV_OTA_URL_ENABLED` | 0xFFD0 | OTA Service（固件升级） |
| `BLE_SRV_WIFI_ENABLED` | 0xFFC0 | WiFi Service（WiFi 配网） |
| `BLE_SRV_LED_ENABLED` | 0xFFB0 | LED Service（LED 控制） |

> 说明：OTA Service 在蓝牙 OTA 或 URL OTA 任一启用时即注册；URL OTA 命令特征 `0xFFD4` 仅在 `BLE_SRV_OTA_URL_ENABLED=y` 时有效。

### 典型配置示例

#### 示例 1：最小化配置（仅 BLE 设备信息 + 蓝牙 OTA）

```
CONFIG_BLE_SRV_ENABLED=y
CONFIG_BLE_SRV_ADV_NAME_PREFIX="BLE-SRV"
CONFIG_BLE_SRV_OTA_BT_ENABLED=y
CONFIG_BLE_SRV_OTA_BT_NOTIFY_INTERVAL=10
CONFIG_BLE_SRV_TEMP_SENSOR_ENABLED=y
```

适用场景：资源受限芯片（如 ESP32-C3 4MB Flash），仅需基础设备信息查询和蓝牙 OTA 升级。

#### 示例 2：完整功能配置（推荐用于 ESP32-S3）

```
# 基础
CONFIG_BLE_SRV_ENABLED=y
CONFIG_BLE_SRV_ADV_NAME_PREFIX="BLE-SRV"
CONFIG_BLE_SRV_ADV_INTERVAL_MIN=32
CONFIG_BLE_SRV_ADV_INTERVAL_MAX=64

# 蓝牙 OTA
CONFIG_BLE_SRV_OTA_BT_ENABLED=y
CONFIG_BLE_SRV_OTA_BT_NOTIFY_INTERVAL=10

# WiFi + URL OTA + NTP
CONFIG_BLE_SRV_WIFI_ENABLED=y
CONFIG_BLE_SRV_OTA_URL_ENABLED=y
CONFIG_BLE_SRV_OTA_URL_DEFAULT=""
CONFIG_BLE_SRV_OTA_URL_SKIP_CERT_CHECK=n
CONFIG_BLE_SRV_OTA_URL_ALLOW_DOWNGRADE=n
CONFIG_BLE_SRV_NTP_ENABLED=y
CONFIG_BLE_SRV_NTP_TIMEZONE="CST-8"

# 应用层认证
CONFIG_BLE_SRV_AUTH_ENABLED=y
CONFIG_BLE_SRV_AUTH_PIN="123456"

# LED 控制
CONFIG_BLE_SRV_LED_ENABLED=y
# CONFIG_BLE_SRV_LED_GPIO 默认按芯片型号选择

# 日志系统（使用 LittleFS）
CONFIG_BLE_SRV_LOG_ENABLED=y
CONFIG_BLE_SRV_LOG_LITTLEFS_PATH="/littlefs"
CONFIG_BLE_SRV_LOG_DIR="/log"
CONFIG_BLE_SRV_LOG_QUEUE_SIZE=1024
CONFIG_BLE_SRV_LOG_LINE_SIZE=512
CONFIG_BLE_SRV_LOG_HTTP_PORT=8080

# 温度传感器
CONFIG_BLE_SRV_TEMP_SENSOR_ENABLED=y
```

配套的系统级配置（必须）：
```
# NimBLE
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_MAX_BONDS=0
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512

# PSRAM（日志队列和 NimBLE 内存池需要）
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y

# 双 OTA 分区表
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# HTTPD 通配符路由（日志 HTTP 服务器需要）
CONFIG_HTTPD_URI_MATCH_WILDCARD=y

# HTTPS 证书包（URL OTA 需要）
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
```

#### 示例 3：带 SD 卡日志的生产环境配置

```
# 日志系统 + SD 卡
CONFIG_BLE_SRV_LOG_ENABLED=y
CONFIG_BLE_SRV_LOG_SD_ENABLED=y
CONFIG_BLE_SRV_LOG_SD_PATH="/sdcard"
CONFIG_BLE_SRV_LOG_LITTLEFS_PATH="/littlefs"
CONFIG_BLE_SRV_LOG_DIR="/log"
CONFIG_BLE_SRV_LOG_QUEUE_SIZE=2048       # 加大队列容量
CONFIG_BLE_SRV_LOG_LINE_SIZE=512
CONFIG_BLE_SRV_LOG_FLUSH_INTERVAL_MS=2000 # 降低刷盘频率延长 SD 卡寿命
CONFIG_BLE_SRV_LOG_MAX_FILES=100
CONFIG_BLE_SRV_LOG_MIN_FREE_SPACE=1024   # 至少保留 1MB 空闲
CONFIG_BLE_SRV_LOG_MAX_FILE_SIZE=1024    # 单文件 1MB
CONFIG_BLE_SRV_LOG_HTTP_PORT=8080
```

### menuconfig 操作提示

- **进入子菜单**: 按 `Enter` 或 `→` 进入高亮项的子菜单
- **切换 bool 选项**: 按 `Y` 启用、`N` 禁用，或在选项上按 `空格` 切换
- **编辑 string/int 选项**: 按 `Enter` 进入编辑模式，输入完成后按 `Enter` 确认
- **搜索配置项**: 按 `/` 输入关键字（如 `BLE_SRV_LOG`）快速定位
- **保存退出**: 按 `Esc` 直到退出，选择 `Yes` 保存到 `sdkconfig`
- **加载默认值**: 删除 `sdkconfig` 后执行 `idf.py reconfigure` 会重新生成（基于 `sdkconfig.defaults`）

> ⚠️ 注意：直接编辑 `sdkconfig` 文件不推荐，应使用 `idf.py menuconfig` 或编辑 `sdkconfig.defaults`。`sdkconfig.defaults` 中的配置项名必须与 Kconfig 中定义完全一致（包括 `_ENABLED` 后缀）。

---

## API 参考

### 核心 API（ble_srv.h）

#### ble_srv_init

```c
bool ble_srv_init(void);
```

初始化 BLE 服务。必须在 NVS 初始化之后调用。

**返回值**: 
- `true` — 初始化成功
- `false` — 初始化失败

**注意**: 初始化顺序为 common → BT → URL/LED/WiFi 等子模块。

#### ble_srv_deinit

```c
void ble_srv_deinit(void);
```

反初始化 BLE 服务，释放所有资源。

#### ble_srv_is_connected

```c
bool ble_srv_is_connected(void);
```

查询当前是否有 BLE 客户端连接。

**返回值**: 
- `true` — 已连接
- `false` — 未连接

#### ble_srv_restart_device

```c
void ble_srv_restart_device(void);
```

通过 BLE 命令触发设备重启。延迟 100ms 后调用 `esp_restart()`。

#### ble_srv_get_conn_handle

```c
uint16_t ble_srv_get_conn_handle(void);
```

获取当前 BLE 连接句柄。未连接时返回 0xFFFF。

**返回值**: NimBLE 连接句柄

---

### 设备信息 API（ble_srv_device.h）

#### ble_srv_get_device_info

```c
bool ble_srv_get_device_info(ble_srv_device_info_t *info);
```

获取设备综合信息。

**参数**:
- `info` — 输出设备信息结构体指针

**ble_srv_device_info_t 结构体**:
| 字段 | 类型 | 说明 |
|------|------|------|
| chip_name | char[32] | 芯片名称（如 "ESP32-S3"） |
| chip_model | char[16] | 芯片型号 |
| flash_size | char[16] | Flash 大小描述 |
| mac_address | char[18] | MAC 地址字符串 |
| version | char[32] | 固件版本字符串 |
| cpu_freq_mhz | uint32_t | CPU 频率（MHz） |
| temperature_celsius | float | 芯片温度（°C） |
| temp_sensor_supported | uint8_t | 温度传感器是否支持（0/1） |
| reset_reason | uint8_t | 上次重启原因（esp_reset_reason_t） |
| uptime_seconds | uint32_t | 运行时间（秒） |
| cpu_cores | uint8_t | CPU 核心数 |

#### ble_srv_get_memory_info

```c
bool ble_srv_get_memory_info(ble_srv_memory_info_t *info);
```

获取内存使用信息。

#### ble_srv_get_cpu_info

```c
bool ble_srv_get_cpu_info(ble_srv_cpu_info_t *info);
```

获取 CPU 详细信息。

**ble_srv_cpu_info_t 结构体**:
| 字段 | 类型 | 说明 |
|------|------|------|
| cpu_freq_mhz | uint32_t | CPU 频率（MHz） |
| uptime_seconds | uint32_t | 运行时间（秒） |
| features | uint32_t | 芯片特性标志位 |
| task_count | uint16_t | 任务数量 |
| cpu_cores | uint8_t | CPU 核心数 |
| cpu_usage | uint8_t | CPU 使用率（0-100） |
| chip_revision | uint8_t | 芯片 revision |
| idf_version | char[24] | ESP-IDF 版本字符串 |

#### ble_srv_get_flash_info

```c
bool ble_srv_get_flash_info(ble_srv_flash_info_t *info);
```

获取 Flash 信息。

**ble_srv_flash_info_t 结构体**:
| 字段 | 类型 | 说明 |
|------|------|------|
| flash_total | uint32_t | Flash 总大小（字节） |
| flash_free | uint32_t | Flash 空闲空间（字节） |
| partition_count | uint8_t | 分区数量 |
| flash_speed_mhz | uint8_t | Flash 速度（MHz） |
| running_partition | char[16] | 当前运行分区名称 |

#### ble_srv_get_partition_info

```c
bool ble_srv_get_partition_info(uint8_t index, ble_srv_partition_info_t *info);
```

获取指定索引的分区信息。

**参数**:
- `index` — 分区索引（0 开始）
- `info` — 输出分区信息结构体指针

---

### WiFi API（ble_srv_wifi.h，需要 CONFIG_BLE_SRV_WIFI_ENABLED）

#### ble_srv_wifi_provisioner_init

```c
bool ble_srv_wifi_provisioner_init(void);
```

初始化 WiFi 配网模块。必须在 `ble_srv_init()` 之前调用。

#### ble_srv_wifi_auto_connect

```c
bool ble_srv_wifi_auto_connect(void);
```

使用 NVS 中保存的凭据自动连接 WiFi。在 `ble_srv_init()` 之后调用。

#### ble_srv_wifi_is_connected

```c
bool ble_srv_wifi_is_connected(void);
```

查询 WiFi 是否已连接。

**返回值**: 
- `true` — 已连接并获取 IP
- `false` — 未连接

#### ble_srv_wifi_connect

```c
bool ble_srv_wifi_connect(const char *ssid, const char *password);
```

连接指定 WiFi。

**参数**:
- `ssid` — WiFi 名称（最大 32 字节）
- `password` — WiFi 密码（最大 64 字节，可为空字符串）

#### ble_srv_wifi_forget

```c
bool ble_srv_wifi_forget(void);
```

清除 NVS 中保存的 WiFi 凭据。

#### ble_srv_wifi_get_status

```c
bool ble_srv_wifi_get_status(ble_wifi_status_t *status);
```

获取 WiFi 连接状态。

**ble_wifi_status_t 结构体**:
| 字段 | 类型 | 说明 |
|------|------|------|
| connected | uint8_t | 是否已连接（0/1） |
| rssi | uint8_t | 信号强度 |
| ip_address | uint32_t | IP 地址（网络字节序） |

---

### 自定义命令 API（ble_srv_gatt.h）

#### ble_srv_gatt_set_custom_cmd_callback

```c
void ble_srv_gatt_set_custom_cmd_callback(ble_srv_custom_cmd_cb_t cb);
```

注册自定义命令处理回调函数。

**参数**:
- `cb` — 回调函数指针，格式为 `int callback(uint16_t conn_handle, const uint8_t *data, uint16_t data_len, uint8_t *resp_buf, uint16_t resp_buf_size, uint16_t *out_resp_len)`

**回调返回值**:
- `0` — 处理成功，如有响应数据则通过 NOTIFY 发送
- 非 `0` — 处理失败，不发送响应

**使用示例**:

```c
static int my_custom_handler(uint16_t conn_handle, const uint8_t *data, uint16_t data_len,
                              uint8_t *resp_buf, uint16_t resp_buf_size, uint16_t *out_resp_len)
{
    // 处理接收到的自定义命令
    if (data_len >= 1 && data[0] == 0x01) {
        // 构造响应
        resp_buf[0] = 0x01;
        resp_buf[1] = 0xAA;
        *out_resp_len = 2;
        return 0;  // 成功，将通过 NOTIFY 发送响应
    }
    return -1;  // 失败
}

// 初始化时注册
ble_srv_gatt_set_custom_cmd_callback(my_custom_handler);
```

#### ble_srv_gatt_custom_cmd_notify

```c
bool ble_srv_gatt_custom_cmd_notify(uint16_t conn_handle, const uint8_t *data, uint16_t data_len);
```

主动发送自定义命令通知数据到客户端。

**参数**:
- `conn_handle` — BLE 连接句柄
- `data` — 要发送的数据指针
- `data_len` — 数据长度

**返回值**:
- `true` — 发送成功
- `false` — 发送失败（未订阅或句柄无效）

### NTP API（ble_srv_wifi.h，需要 CONFIG_BLE_SRV_NTP_ENABLED）

#### ble_srv_ntp_sync

```c
bool ble_srv_ntp_sync(void);
```

触发 NTP 时间同步。需要 WiFi 已连接。

---

### LED API（ble_srv_led.h，需要 CONFIG_BLE_SRV_LED_ENABLED）

#### ble_srv_led_init

```c
bool ble_srv_led_init(void);
```

初始化 LED 模块（RMT 外设、特效任务）。在 `ble_srv_init()` 内部自动调用。

#### ble_srv_led_deinit

```c
void ble_srv_led_deinit(void);
```

反初始化 LED 模块，停止特效任务并释放 RMT 资源。

#### ble_srv_led_set_on

```c
bool ble_srv_led_set_on(bool on);
```

开关 LED。

**参数**:
- `on` — `true` 打开，`false` 关闭

#### ble_srv_led_set_rgb

```c
bool ble_srv_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
```

设置 LED RGB 颜色。

**参数**:
- `red` — 红色分量（0-255）
- `green` — 绿色分量（0-255）
- `blue` — 蓝色分量（0-255）

**注意**: 颜色通道顺序为 RGB，不是 GRB。

#### ble_srv_led_set_effect

```c
bool ble_srv_led_set_effect(ble_led_effect_t effect, uint8_t speed);
```

设置 LED 特效。

**参数**:
- `effect` — 特效类型
- `speed` — 特效速度（1-255，数值越小越快）

**特效类型**:
| 枚举值 | 值 | 说明 |
|--------|-----|------|
| BLE_LED_EFFECT_NONE | 0 | 无特效（纯色） |
| BLE_LED_EFFECT_BREATH | 1 | 呼吸灯 |
| BLE_LED_EFFECT_BLINK | 2 | 闪烁 |
| BLE_LED_EFFECT_RAINBOW | 3 | 彩虹渐变 |
| BLE_LED_EFFECT_STROBE | 4 | 频闪 |

#### ble_srv_led_get_status

```c
bool ble_srv_led_get_status(ble_led_status_t *status);
```

获取 LED 当前状态。

**ble_led_status_t 结构体**:
| 字段 | 类型 | 说明 |
|------|------|------|
| on | uint8_t | 开关状态 |
| effect | uint8_t | 当前特效 |
| speed | uint8_t | 特效速度 |
| red | uint8_t | 红色分量 |
| green | uint8_t | 绿色分量 |
| blue | uint8_t | 蓝色分量 |

---

## GATT 服务与UUID

### Device Service（设备服务）

| 项目 | UUID | 属性 | 说明 |
|------|------|------|------|
| Service | 0xFFE0 | - | 设备信息服务 |
| Info Characteristic | 0xFFE2 | Read | 设备综合信息（123字节） |
| Memory Characteristic | 0xFFE3 | Read | 内存信息（44字节） |
| CPU Characteristic | 0xFFE4 | Read | CPU信息（41字节） |
| Flash Characteristic | 0xFFE5 | Read | Flash信息（26字节） |
| Partition Characteristic | 0xFFE7 | Write/Read | 分区索引写入/分区信息读取 |
| Restart Characteristic | 0xFFE6 | Write/Notify | 写入 0x01 触发重启 |
| Auth Characteristic | 0xFFE8 | Read/Write/Notify | 应用层认证（密码写入） |
| Log Characteristic | 0xFFE9 | Notify | 设备日志推送（OTA日志等） |
| Custom Cmd Characteristic | 0xFFEA | Write/Notify | 自定义命令（第三方扩展） |

### OTA Service（OTA服务）

| 项目 | UUID | 属性 | 说明 |
|------|------|------|------|
| Service | 0xFFD0 | - | OTA升级服务 |
| BT Command Char | 0xFFD1 | Write | 蓝牙OTA命令 |
| BT Data Char | 0xFFD2 | Write Without Response | 固件数据写入 |
| Status Char | 0xFFD3 | Notify | OTA状态通知 |
| URL Char | 0xFFD4 | Write | URL OTA命令 |

### WiFi Service（WiFi服务，条件编译）

| 项目 | UUID | 属性 | 说明 |
|------|------|------|------|
| Service | 0xFFC0 | - | WiFi配网服务 |
| Config Characteristic | 0xFFC1 | Write | SSID+Password写入 |
| Status Characteristic | 0xFFC2 | Read | WiFi状态读取 |
| Control Characteristic | 0xFFC3 | Write | 控制命令（忘记/NTP同步） |

### LED Service（LED服务，条件编译）

| 项目 | UUID | 属性 | 说明 |
|------|------|------|------|
| Service | 0xFFB0 | - | LED控制服务 |
| Control Char | 0xFFB1 | Write/Read | 开关控制/状态读取 |
| Color Char | 0xFFB2 | Write | RGB颜色写入 |
| Effect Char | 0xFFB3 | Write | 特效+速度写入 |

---

## OTA 固件升级

### 状态机转换

```
IDLE → CHECKING → CHECK_OK → RECEIVING → VERIFYING → VERIFY_OK → APPLYING → APPLY_OK（重启）
  ↓         ↓          ↓           ↓           ↓            ↓           ↓
ERROR    CHECK_FAIL   ERROR      ERROR     VERIFY_FAIL    APPLY_FAIL   APPLY_FAIL
                      ↓                       ↓            ↓
                  (终态)                   (终态)       (终态)
                      ↓                       ↓            ↓
                  300ms后                  300ms后      300ms后
                  自动重置                  自动重置     自动重置
```

### 状态说明

| 状态 | 值 | 说明 | 是否终态 |
|------|-----|------|----------|
| IDLE | 0x00 | 空闲，就绪 | 否 |
| CHECKING | 0x01 | 检查固件版本和大小 | 否 |
| CHECK_OK | 0x02 | 检查通过，可以开始 | 否 |
| CHECK_FAIL | 0x03 | 版本相同/更旧，无需更新 | 是 |
| RECEIVING | 0x04 | 接收固件数据中 | 否 |
| VERIFYING | 0x05 | 校验中（CRC32） | 否 |
| VERIFY_OK | 0x06 | 校验通过 | 否 |
| VERIFY_FAIL | 0x07 | 校验失败 | 是 |
| APPLYING | 0x08 | 应用固件（切换分区） | 否 |
| APPLY_OK | 0x09 | 应用成功，3秒后重启 | 是（特殊） |
| APPLY_FAIL | 0x0A | 应用失败 | 是 |
| ABORTING | 0x0B | 正在中止 | 否 |
| ABORTED | 0x0C | 已中止 | 是 |
| ERROR | 0x0D | 错误 | 是 |

### 错误码说明

| 错误码 | 值 | 说明 |
|--------|-----|------|
| NONE | 0x00 | 无错误 |
| INVALID_CMD | 0x01 | 无效命令 |
| INVALID_SIZE | 0x02 | 固件大小无效 |
| FLASH_WRITE | 0x03 | Flash写入失败 |
| NO_PARTITION | 0x04 | 无可用OTA分区 |
| VERIFY_FAILED | 0x05 | 固件校验失败 |
| INTERNAL | 0x06 | 设备内部错误 |
| BUSY | 0x07 | 设备忙（OTA进行中） |
| NO_NETWORK | 0x08 | 网络未连接（URL OTA） |
| ABORTED | 0x09 | 用户中止 |
| DISCONNECTED | 0x0A | 连接断开 |
| VERSION_DOWNGRADE | 0x0B | 远程固件版本更旧（拒绝降级） |
| VERSION_SAME | 0x0C | 固件版本相同（无需更新） |
| CRC_MISMATCH | 0x0D | CRC32校验失败 |

### 蓝牙 OTA 协议

蓝牙 OTA 采用基于累积 ACK 的滑动窗口协议：

1. **窗口大小**: 12包（`BLE_OTA_BT_WINDOW_SIZE`）
2. **数据包格式**: `[4字节offset][数据chunk]`
3. **传输流程**:
   - 客户端发送 START 命令（包含大小、CRC、块大小、版本）
   - 设备返回 CHECK_OK 后进入 RECEIVING 状态
   - 客户端连续发送 12 包数据
   - 设备通过 NOTIFY 发送累积 ACK（包含已写入字节数）
   - 客户端收到 ACK 后发送下一窗口数据
   - ACK 超时（2秒）自动重传当前窗口
   - 数据发送完毕后发送 VERIFY 命令
   - 设备进行 CRC32 校验
   - 校验通过后发送 APPLY 命令
   - 设备切换分区并返回 APPLY_OK
   - 3秒后自动重启

### URL OTA 流程

1. 设备需先连接 WiFi
2. 客户端写入 URL（或使用默认 URL）
3. 设备检查网络连接状态
4. 从 URL 下载固件头部，解析版本号
5. 版本比较（远程版本必须更新）
6. 下载完整固件
7. CRC32 校验
8. 应用固件
9. 重启设备

---

## WiFi 配网

### 配网流程

1. 客户端连接 BLE 设备
2. 向 Config 特征值写入 SSID + Password（98字节固定格式）
3. 设备保存凭据到 NVS 并尝试连接
4. 客户端可通过 Status 特征值查询连接状态
5. 连接成功后自动保存，重启自动连接

### WiFi 配置数据格式

`ble_wifi_config_t` 结构体（98字节）:
```
Offset 0-32:   SSID（33字节，空终止）
Offset 33-97:  Password（65字节，空终止）
```

### WiFi 状态数据格式

`ble_wifi_status_t` 结构体（6字节）:
```
Offset 0:      connected（1字节，0/1）
Offset 1:      RSSI（1字节）
Offset 2-5:    IP地址（4字节，网络字节序）
```

### 控制命令

写入 Ctrl 特征值（1字节）:
- `0x01` FORGET — 清除保存的 WiFi 凭据
- `0x02` NTP_SYNC — 触发 NTP 时间同步

---

## WS2812 LED 控制

### 硬件连接

- 数据线连接到配置的 GPIO（默认见 menuconfig 章节）
- LED 灯珠为 WS2812/WS2812B/SK6812 等兼容型号
- 需要外部 5V 供电（根据 LED 数量选择合适电源）
- 数据线建议串联 300-500Ω 电阻

### 控制协议

**颜色设置**: 写入 Color 特征值（3字节）
```
Offset 0: Red（0-255）
Offset 1: Green（0-255）
Offset 2: Blue（0-255）
```

**特效设置**: 写入 Effect 特征值（2字节）
```
Offset 0: effect（0-4）
Offset 1: speed（1-255）
```

**开关控制**: 写入 Ctrl 特征值（1字节）
```
0x00: OFF
0x01: ON
```

**状态读取**: 读取 Ctrl 特征值（6字节）
```
Offset 0: on/off
Offset 1: effect
Offset 2: speed
Offset 3: red
Offset 4: green
Offset 5: blue
```

### 注意事项

- 颜色通道顺序为 **RGB**（固件默认），不是 GRB
- 特效任务在独立 FreeRTOS 任务中运行，非阻塞
- 设置特效后颜色由特效控制，设置 `effect=none` 恢复纯色
- deinit 时先设置 `s_initialized=false` 等待特效任务退出，再释放 RMT 资源

---

## 设备信息

### 设备信息数据格式（123字节）

| 偏移 | 长度 | 类型 | 字段 | 说明 |
|------|------|------|------|------|
| 0 | 32 | char[] | chip_name | 芯片名称 |
| 32 | 16 | char[] | chip_model | 芯片型号 |
| 48 | 16 | char[] | flash_size | Flash大小描述 |
| 64 | 18 | char[] | mac_address | MAC地址 |
| 82 | 32 | char[] | version | 固件版本 |
| 114 | 4 | uint32 | cpu_freq_mhz | CPU频率MHz |
| 118 | 4 | float | temperature | 温度°C |
| 122 | 1 | uint8 | temp_supported | 温度传感器支持 |
| 123 | - | - | - | **（注：实际偏移需以代码为准）** |

### 版本号格式

固件版本从应用描述符（esp_app_desc_t）解析，支持最多4段：
- `major.minor.patch.build`（如 `1.2.1`、`1.2.1.123`）
- 每段使用 `uint16_t` 存储，防止溢出

---

## 温度传感器

- 支持的芯片：ESP32-S2/S3/C3/C5/C6/C61/H2/P4
- ESP32 无内置温度传感器
- 温度值范围由 menuconfig 配置（默认 -10°C ~ 80°C）
- 温度值整合到设备信息结构体中，无需独立读取
- 不支持时 `temp_sensor_supported` 字段为 0，温度值为无效值

---

## 初始化与反初始化顺序

### 正确初始化顺序

```
1. nvs_flash_init()                          // NVS 初始化（必需）
2. ble_srv_wifi_provisioner_init()           // WiFi 配网初始化（如启用，必须在 BLE init 前）
3. ble_srv_init()                            // BLE 服务初始化（内部初始化各子模块）
   ├─ 创建 state_lock（保护 s_conn_handle / s_advertising）
   ├─ ble_srv_common_init()                 // 公共层（创建互斥锁）
   ├─ ble_srv_bt_init()                     // NimBLE 初始化（GAP/GATT）
   ├─ ble_srv_ota_common_init()             // OTA 公共层
   ├─ ble_srv_ota_bt_init()                 // 蓝牙 OTA
   ├─ ble_srv_ota_url_init()                // URL OTA（如启用）
   ├─ ble_srv_led_init()                    // LED（如启用）
   └─ ble_srv_device_init()                 // 设备信息/温度传感器
4. ble_srv_wifi_auto_connect()               // WiFi 自动连接（如启用）
```

### 反初始化顺序

```
1. ble_srv_ota_url_deinit()                 // URL OTA 先停止任务
2. ble_srv_led_deinit()                     // LED 停止特效任务
3. ble_srv_wifi_provisioner_deinit()        // WiFi 配网
4. ble_srv_deinit()                         // BLE 服务反初始化
   ├─ 销毁 state_lock
   └─ ...
```

### 锁顺序约定

为避免死锁，必须遵守以下锁获取顺序：
1. `state_lock`（互斥锁，保护 `s_conn_handle` / `s_advertising`）
2. `bt_lock`（递归互斥锁，保护 BLE 连接状态）
3. `ota_lock`（保护 OTA 会话状态）

**重要**:
- GAP 事件回调中访问 `s_conn_handle` / `s_advertising` 前必须获取 `state_lock`
- 调用外部 common 函数（如 `ble_srv_ota_finish`）前需先释放 `bt_lock`，外部函数自行获取 `ota_lock`

---

## 注意事项

### 系统要求

1. **必须**先初始化 NVS 再调用 `ble_srv_init()`
2. 分区表必须使用双 OTA 分区方案才能支持 OTA
3. URL OTA 依赖 WiFi 功能，必须先启用 `BLE_SRV_WIFI_ENABLED`
4. ESP-IDF 版本推荐 v6.0+，v5.x 也可使用但部分新芯片可能不支持

### OTA 重要提示

5. **每次 OTA 成功后建议重启设备**（APPLY_OK 状态已内置 3 秒自动重启）
6. **OTA 失败需要重启设备后再次尝试**，不要在错误状态下连续重试
7. 终态（CHECK_FAIL/VERIFY_FAIL/APPLY_FAIL/ABORTED/ERROR）会在 300ms 后自动重置为 IDLE，支持多次 OTA
8. OTA 会话通过 `ble_srv_ota_begin(mode)` 获取所有权，确保 BT/URL OTA 互斥
9. OTA 状态机转换路径固定：IDLE→CHECKING→CHECK_OK→RECEIVING→VERIFYING→VERIFY_OK→APPLYING→APPLY_OK
10. 蓝牙 OTA 必须计算并比对 CRC32 校验值
11. URL OTA 必须先检查 WiFi 连接状态，未连接时中止并返回错误
12. URL OTA 的 deinit 需先 abort、通知任务、设任务句柄为 NULL，等待至少 1000ms
13. 网络读取循环需添加超时保护，连续 50 次返回 0 字节（约 1 秒）视为 stalled
14. OTA 状态通知使用 `s_total_received` 和 `s_fw_bytes_written` 的较大值计算进度

### BLE 连接注意

15. 共享变量（`s_conn_handle`, `s_advertising`）使用 `volatile` 声明，**并必须通过 `state_lock` 互斥锁保护**（GAP 事件回调和 start/stop advertising 中）
16. `ble_hs_util_ensure_addr` 失败时返回错误并记录日志，**禁止**使用 `assert` 导致设备崩溃
17. Windows BLE 客户端连接必须使用 `use_cached=False`
18. 设备断开连接时必须唤醒所有等待 ACK 的协程
19. 所有静态变量的访问必须通过互斥锁保护

### 内存与资源

19. `heap_caps_malloc` 分配的内存必须使用 `heap_caps_free` 释放
20. 版本检查缓冲区必须使用 `heap_caps_malloc(MALLOC_CAP_INTERNAL)` 分配在内部 RAM
21. NVS 操作失败时应返回错误，**禁止**调用 `ESP_ERROR_CHECK` 导致设备重启
22. 初始化函数返回值必须检查，失败时回滚已初始化资源
23. 任务句柄变量若为 `volatile`，需使用非 volatile 临时变量接收 `xTaskCreate` 输出后再赋值

### LED 注意

25. LED 驱动 deinit 时必须先设置 `s_initialized=false`，等待 effect task 退出后再释放 RMT 资源
26. 颜色通道顺序为 **RGB**，禁止使用 GRB
27. WS2812 GPIO 配置需根据芯片型号选择，参考 menuconfig 默认值
28. LED 模块所有信号量获取必须使用超时（`LED_LOCK_TIMEOUT_MS = 5000ms`），**禁止**使用 `portMAX_DELAY` 防止死锁

### 代码规范

27. 所有 C 模块必须添加 `s_initialized` 标志，防止重复初始化或未初始化 deinit
28. 重启/等待延迟等魔数必须替换为具名宏常量（如 `BLE_OTA_RESET_DELAY_MS 300`、`BLE_OTA_RESTART_DELAY_MS 3000`）
29. Kconfig 变量必须使用 `_ENABLED` 后缀（如 `BLE_SRV_OTA_URL_ENABLED`）

### WiFi 配置

30. WiFi 配置 GATT 写入时必须确保 ssid/password 字段以 `'\0'` 终止，防止越界读取

---

## 常见问题

### Q: 编译错误 "undefined reference to `ble_srv_init'"？

**A**: 
1. 确认 `ble_srv` 组件在项目的 `components/` 目录下
2. 确认已运行 `idf.py reconfigure` 重新检测组件
3. 检查 `CMakeLists.txt` 中是否正确设置了 `EXTRA_COMPONENT_DIRS`

### Q: 广播名称不是 "BLE-SRV-XXXXXX"？

**A**:
1. 检查 menuconfig 中 `Component config → BLE Service → Device Name Prefix` 设置
2. **注意**: `sdkconfig.defaults` 中配置项名必须为 `CONFIG_BLE_SRV_ADV_NAME_PREFIX`（不是 `CONFIG_BLE_SRV_NAME_PREFIX`）
3. 如果之前做过 OTA，设备可能从旧的 OTA 分区启动。需执行 `idf.py erase-flash` 擦除后重新烧录

### Q: OTA 提示 "No OTA partition"？

**A**: 分区表未使用双 OTA 方案，在 menuconfig 中设置：
```
Partition Table → Partition Table → Factory app, two OTA definitions
```

### Q: URL OTA 提示 "No network"？

**A**: URL OTA 需要设备先连接 WiFi：
1. 确认已启用 `BLE_SRV_WIFI_ENABLED`
2. 先通过 BLE 发送 WiFi 配置连接 WiFi
3. 等待 WiFi 获取 IP 地址后再发送 URL OTA 命令

### Q: LED 颜色红绿相反？

**A**: 本组件使用 **RGB** 顺序：
- 红色 = FF0000
- 绿色 = 00FF00
- 蓝色 = 0000FF
如果你的灯带是 GRB 顺序，需要在硬件层面调整或修改固件中的颜色顺序。

### Q: 启用 LED 后编译报错找不到 RMT 相关头文件？

**A**: 
- ESP-IDF v5.x: RMT 驱动在 `esp_driver_rmt` 组件中，CMakeLists.txt 已声明依赖
- ESP-IDF v4.x: 不支持，必须升级到 v5.x 或 v6.x

### Q: 温度传感器读取值异常？

**A**: 
- ESP32 没有内置温度传感器，此字段无效
- 检查芯片是否支持温度传感器（参考支持芯片列表）
- 温度范围配置需在传感器工作范围内

### Q: NTP 同步不生效？

**A**: 
1. 确认 WiFi 已连接并可访问互联网
2. 确认已启用 `BLE_SRV_NTP_ENABLED`
3. 检查 NTP 服务器地址是否正确（默认使用国内阿里云 NTP）
4. 时区设置需符合 POSIX 时区格式（如 "CST-8" 为中国标准时间）

### Q: 多次调用 ble_srv_init() 导致崩溃？

**A**: 
1. 组件有 `s_initialized` 标志防止重复初始化
2. 但建议在应用层确保只调用一次 `ble_srv_init()`
3. 需要重新初始化时先调用 `ble_srv_deinit()` 再 init

### Q: BLE 连接后很快断开？

**A**: 
1. 检查连接间隔参数（广播间隔不影响连接）
2. 确保客户端有足够的连接超时设置
3. 检查是否有看门狗复位（查看复位原因）
4. OTA 过程中长时间阻塞 GATT 回调可能导致连接超时

---

## 故障排查

### 日志标签

组件使用以下日志标签，可通过 `esp_log_level_set()` 调整日志级别：

| 标签 | 模块 |
|------|------|
| `BLE_SRV` | 核心初始化 |
| `BLE_GAP` | GAP 广播/连接事件 |
| `BLE_GATT` | GATT 服务/读写事件 |
| `OTA` | OTA 公共层 |
| `OTA_BT` | 蓝牙 OTA 驱动 |
| `OTA_URL` | URL OTA 驱动 |
| `WIFI` | WiFi 配网 |
| `NTP` | NTP 时间同步 |
| `LED` | WS2812 LED 驱动 |
| `DEVICE` | 设备信息采集 |
| `TEMP_SENSOR` | 温度传感器 |
| `AUTH` | 应用层认证 |
| `CUSTOM_CMD` | 自定义命令 |

### 启用调试日志

在 `app_main()` 开头添加：

```c
esp_log_level_set("BLE_SRV", ESP_LOG_DEBUG);
esp_log_level_set("OTA_BT", ESP_LOG_DEBUG);
esp_log_level_set("OTA", ESP_LOG_DEBUG);
```

### 常见崩溃原因

1. **NVS 未初始化** — 必须先调用 `nvs_flash_init()`
2. **双 OTA 分区未配置** — 启用 OTA 必须使用双分区表
3. **GPIO 冲突** — LED GPIO 与其他外设冲突
4. **栈溢出** — OTA 任务栈大小不足（已配置为 8KB）
5. **锁顺序错误** — 必须按 bt_lock → ota_lock 顺序获取
6. **在 GATT 回调中执行阻塞操作** — GATT 回调运行在 NimBLE 任务中，禁止长时间阻塞

### OTA 失败排查步骤

1. 检查设备端日志中的 OTA 状态转换
2. 确认固件大小与 OTA 分区大小匹配
3. 检查固件 CRC32 是否正确（客户端会自动计算）
4. 蓝牙 OTA 时检查 MTU 协商是否正常
5. URL OTA 时检查 WiFi 信号强度和网络连通性
6. 版本检查失败时确认新固件版本号确实更新了
7. 失败后重启设备再尝试

### 内存不足排查

1. 启用内存信息读取查看堆内存使用情况
2. OTA 缓冲区使用内部 RAM，确保有足够的连续内部堆
3. PSRAM 可用于非 DMA 缓冲区，但 OTA 写入需使用内部 RAM
