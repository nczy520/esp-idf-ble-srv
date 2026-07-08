# ESP-IDF BLE Service

基于 NimBLE 的 ESP32 BLE 服务组件，提供设备管理、OTA 固件升级、WiFi 配网、WS2812 LED 控制等功能。

**版本**: 1.0.3 | **协议栈**: NimBLE | **兼容**: ESP-IDF v5.x / v6.x

## 功能特性

- **设备信息查询** — 芯片型号、Flash 大小、MAC 地址、固件版本、CPU 频率
- **内存/CPU/Flash/分区监控** — 堆内存使用、运行时间、分区列表
- **OTA 固件升级** — 后台写入、CRC 校验、断连自动重置、进度通知
- **WiFi 配网** — BLE 写入 SSID/密码，NVS 持久化，凭据删除，NTP 同步
- **WS2812 LED 控制** — RGB 颜色设置、呼吸灯/闪烁/彩虹/频闪特效（非阻塞切换）
- **设备重启** — BLE 远程重启

## 支持芯片

ESP32 / ESP32-S2 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-H2

## 快速开始

### 1. 添加组件

将 `ble_srv` 目录复制到你的项目 `components/` 下，或通过 ESP-IDF 组件管理器引入：

```bash
idf.py add-dependency "ble_srv^1.0.3"
```

### 2. 配置 menuconfig

```bash
idf.py menuconfig
```

```
Component config → BLE Service:
  [*] Enable BLE Service
      Name prefix (BLE-SRV)          # BLE 广播名前缀
  [*] Enable WiFi Provisioner        # WiFi 配网（可选）
  [*] Enable NTP Time Sync           # NTP 同步（可选）
  [*] Enable WS2812 LED Control      # LED 控制（可选）
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
| Device Service | 0xFFE0 | 设备信息、重启 | 默认启用 |
| OTA Service | 0xFFD0 | 固件升级 | 默认启用 |
| WiFi Service | 0xFFC0 | WiFi 配网 | CONFIG_BLE_SRV_WIFI_PROVISIONER |
| LED Service | 0xFFB0 | LED 控制 | CONFIG_BLE_SRV_LED |

完整 BLE 命令参考见 [docs/BLE_COMMANDS.md](docs/BLE_COMMANDS.md)。

## Python 客户端

项目附带 BLE 客户端工具 `tools/client.py`，依赖 `bleak` 库：

```bash
pip install bleak

# 查看设备信息
python tools/client.py -d BLE-SRV -c info

# OTA 升级
python tools/client.py -d BLE-SRV -c ota -f firmware.bin

# WiFi 配网
python tools/client.py -d BLE-SRV -c wifi-connect --ssid MyWiFi --password 12345678

# LED 控制
python tools/client.py -d BLE-SRV -c led-on
python tools/client.py -d BLE-SRV -c led-color --color FF0000
python tools/client.py -d BLE-SRV -c led-effect --effect breath --speed 80

# 查看所有命令
python tools/client.py -d BLE-SRV -c help
```

## 文件结构

```
ble_srv/
  include/
    ble_srv.h              # 主聚合头文件
    ble_srv_gatt.h         # GATT 服务与 val_handle
    ble_srv_ota.h          # OTA 模块
    ble_srv_wifi.h         # WiFi 模块
    ble_srv_led.h          # LED 模块
    ble_srv_device.h       # 设备信息模块
  src/
    ble_srv_core.c         # NimBLE 初始化、GAP、广播
    ble_srv_gatt.c         # GATT 服务定义与 access callback
    ble_srv_ota.c          # OTA 固件升级（后台写入 + 校验）
    ble_srv_device.c       # 设备信息读取
    ble_srv_wifi.c         # WiFi 配网
    ble_srv_led.c          # WS2812 LED 驱动（RMT + 非阻塞特效）
    ble_srv_ntp.c          # NTP 时间同步
  CMakeLists.txt
  Kconfig
  idf_component.yml
docs/
  BLE_COMMANDS.md          # BLE 操作命令详细参考
tools/
  client.py                # Python BLE 客户端
examples/
  basic/                   # 最小示例项目
```

## 模块说明

| 模块 | 文件 | 职责 |
|------|------|------|
| Core | ble_srv_core.c | NimBLE 初始化、GAP 事件处理、广播管理 |
| GATT | ble_srv_gatt.c | GATT 服务表定义、READ/WRITE access callback |
| OTA | ble_srv_ota.c | 固件接收（StreamBuffer）、后台写入任务、校验、应用 |
| Device | ble_srv_device.c | 芯片/内存/CPU/Flash/分区信息采集 |
| WiFi | ble_srv_wifi.c | WiFi 凭据管理（NVS）、连接/断开/状态查询 |
| LED | ble_srv_led.c | WS2812 RMT 驱动、非阻塞特效任务（restart 标志切换） |
| NTP | ble_srv_ntp.c | SNTP 初始化、多服务器配置、时区设置 |

## 依赖

- ESP-IDF v5.x / v6.x
- NimBLE（ESP-IDF 内置）
- WiFi Provisioner 组件（可选，启用 WiFi 配网时需要）

## License

Apache License 2.0
