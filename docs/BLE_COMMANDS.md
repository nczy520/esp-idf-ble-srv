# BLE Service 操作命令参考

> 版本: 1.1.0 | 协议栈: NimBLE | 兼容: ESP-IDF v5.x / v6.x

## 概述

BLE Service 组件通过蓝牙低功耗（BLE）GATT 服务提供 ESP32 设备的远程控制与监控能力。设备广播名称格式为 `前缀-XXXXXX`（MAC 后三字节），默认前缀 `BLE-SRV`。

---

## GATT 服务总览

| 服务 | UUID | 功能 | 条件编译 |
|------|------|------|----------|
| Device Service | 0xFFE0 | 设备信息查询、重启 | 默认启用 |
| OTA Service | 0xFFD0 | 固件空中升级 | 默认启用 |
| WiFi Service | 0xFFC0 | WiFi 配网与控制 | CONFIG_BLE_SRV_WIFI_PROVISIONER |
| LED Service | 0xFFB0 | WS2812 LED 控制与特效 | CONFIG_BLE_SRV_LED |

---

## 1. Device Service (0xFFE0)

设备信息查询和重启控制。

| 特征 | UUID | 属性 | 数据长度 | 说明 |
|------|------|------|----------|------|
| Command | 0xFFE1 | Write | 1 byte | 命令通道 |
| Device Info | 0xFFE2 | Read | 118 bytes | 设备完整信息 |
| Memory Info | 0xFFE3 | Read | 16 bytes | 堆内存状态 |
| CPU Info | 0xFFE4 | Read | 12 bytes | CPU 频率与运行时间 |
| Flash Info | 0xFFE5 | Read | 9 bytes | Flash 总量与分区数 |
| Partition Info | 0xFFE7 | Read/Write | 24 bytes | 分区详情（写索引选择） |
| Restart | 0xFFE6 | Write | 1 byte | 触发重启 |

### 1.1 读取设备信息 (0xFFE2)

返回 `ble_srv_device_info_t`（packed, 118 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 示例 |
|------|------|------|------|------|
| 0 | 32 | chip_name | char[] | "ESP32-S3" |
| 32 | 16 | chip_model | char[] | "ESP32-S3" |
| 48 | 16 | flash_size | char[] | "16MB" |
| 64 | 18 | mac_address | char[] | "AA:BB:CC:DD:EE:FF" |
| 82 | 32 | version | char[] | "1.1.0" |
| 114 | 4 | cpu_freq_mhz | uint32 | 240 |

**客户端命令**: `python client.py -d BLE-SRV -c info`

### 1.2 读取内存信息 (0xFFE3)

返回 `ble_srv_memory_info_t`（packed, 16 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | heap_total | uint32 | 堆总大小 (bytes) |
| 4 | 4 | heap_free | uint32 | 当前空闲堆 |
| 8 | 4 | heap_min_free | uint32 | 历史最小空闲堆 |
| 12 | 4 | stack_high_watermark | uint32 | 任务栈高水位 (bytes) |

**客户端命令**: `python client.py -d BLE-SRV -c memory`

### 1.3 读取 CPU 信息 (0xFFE4)

返回 `ble_srv_cpu_info_t`（packed, 12 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | cpu_freq_mhz | uint32 | CPU 频率 (MHz) |
| 4 | 4 | cpu_usage | uint32 | CPU 使用率（保留，暂为0） |
| 8 | 4 | uptime_seconds | uint32 | 运行时间（秒） |

**客户端命令**: `python client.py -d BLE-SRV -c cpu`

### 1.4 读取 Flash 信息 (0xFFE5)

返回 `ble_srv_flash_info_t`（packed, 9 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | flash_total | uint32 | Flash 总大小 (bytes) |
| 4 | 4 | flash_free | uint32 | Flash 空闲（保留，暂为0） |
| 8 | 1 | partition_count | uint8 | 分区数量 |

**客户端命令**: `python client.py -d BLE-SRV -c flash`

### 1.5 读取分区信息 (0xFFE7)

**Write**: 写入 1 byte 分区索引号（从 0 开始）
**Read**: 返回 `ble_srv_partition_info_t`（packed, 24 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 16 | label | char[] | 分区标签 |
| 16 | 4 | address | uint32 | 起始地址 |
| 20 | 4 | size | uint32 | 分区大小 (bytes) |
| 24 | 1 | type | uint8 | 分区类型 |
| 25 | 1 | subtype | uint8 | 分区子类型 |

**客户端命令**: `python client.py -d BLE-SRV -c partition`

### 1.6 重启设备 (0xFFE6)

写入任意 1 byte 数据触发设备重启。

**客户端命令**: `python client.py -d BLE-SRV -c restart`

---

## 2. OTA Service (0xFFD0)

固件空中升级服务，支持后台写入、CRC 校验、断连自动重置。

| 特征 | UUID | 属性 | 说明 |
|------|------|------|------|
| OTA Command | 0xFFD1 | Write | OTA 命令通道 |
| FW Data | 0xFFD2 | Write | 固件数据流 |
| OTA Status | 0xFFD3 | Read + Notify | OTA 状态通知 |

### 2.1 OTA 命令 (0xFFD1)

| 命令 | 值 | 数据格式 | 说明 |
|------|-----|----------|------|
| START | 0x01 | 1 byte cmd + 16 bytes start_req | 开始 OTA |
| ABORT | 0x02 | 1 byte | 中止当前 OTA |
| VERIFY | 0x03 | 1 byte | 校验已写入的固件 |
| APPLY | 0x04 | 1 byte | 应用新固件并重启 |

**OTA START 请求结构** (`ble_ota_start_req_t`, packed, 16 bytes)：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | fw_size | uint32 | 固件总大小 (bytes) |
| 4 | 4 | fw_crc | uint32 | 固件 CRC32 |
| 8 | 2 | chunk_size | uint16 | 数据分片大小 |
| 10 | 2 | reserved | uint16 | 保留 |
| 12 | 4 | fw_version | uint32 | 固件版本号 |

### 2.2 OTA 状态 (0xFFD3)

`ble_ota_status_t`（packed, 11 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 1 | state | uint8 | 状态码 |
| 1 | 1 | error_code | uint8 | 错误码 |
| 2 | 4 | fw_size | uint32 | 固件总大小 |
| 6 | 4 | bytes_written | uint32 | 已写入大小 |
| 10 | 1 | progress | uint8 | 进度百分比 (0-100) |

**OTA 状态码**：

| 状态 | 值 | 说明 |
|------|-----|------|
| IDLE | 0x00 | 空闲 |
| RECEIVING | 0x01 | 接收中 |
| VERIFYING | 0x02 | 校验中 |
| VERIFY_OK | 0x03 | 校验成功 |
| VERIFY_FAIL | 0x04 | 校验失败 |
| APPLYING | 0x05 | 应用中 |
| APPLY_OK | 0x06 | 应用成功 |
| APPLY_FAIL | 0x07 | 应用失败 |
| ERROR | 0x08 | 错误 |

**客户端命令**: `python client.py -d BLE-SRV -c ota -f firmware.bin`

### 2.3 OTA 流程

```
1. Write CMD(0x01) + start_req   →  设备开始接收
2. Write FW Data (0xFFD2)        →  循环写入固件分片
3. Write CMD(0x03)               →  校验固件
4. Read OTA Status               →  确认校验成功
5. Write CMD(0x04)               →  应用固件，设备重启
```

---

## 3. WiFi Service (0xFFC0)

WiFi 配网与控制服务。需要 `CONFIG_BLE_SRV_WIFI_PROVISIONER=y`。

| 特征 | UUID | 属性 | 说明 |
|------|------|------|------|
| WiFi Config | 0xFFC1 | Write | 写入 SSID 和密码 |
| WiFi Status | 0xFFC2 | Read + Notify | 读取连接状态 |
| WiFi Control | 0xFFC3 | Write | WiFi 控制命令 |

### 3.1 设置 WiFi 凭据 (0xFFC1)

写入 `ble_wifi_config_t`（packed, 98 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 33 | ssid | char[33] | WiFi SSID (\0 结尾) |
| 33 | 65 | password | char[65] | WiFi 密码 (\0 结尾) |

设备收到后自动保存凭据到 NVS 并连接。

**客户端命令**: `python client.py -d BLE-SRV -c wifi-connect --ssid MyWiFi --password 12345678`

### 3.2 读取 WiFi 状态 (0xFFC2)

`ble_wifi_status_t`（packed, 6 bytes）：

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 1 | connected | uint8 | 连接状态 (0/1) |
| 1 | 1 | rssi | uint8 | 信号强度绝对值 (dBm) |
| 2 | 4 | ip_address | uint32 | IP 地址（网络字节序） |

**客户端命令**: `python client.py -d BLE-SRV -c wifi-status`

### 3.3 WiFi 控制命令 (0xFFC3)

| 命令 | 值 | 说明 |
|------|-----|------|
| FORGET | 0x01 | 删除 WiFi 凭据，断开连接 |
| NTP_SYNC | 0x02 | 触发 NTP 时间同步（需 CONFIG_BLE_SRV_NTP_SYNC） |

**客户端命令**:
```bash
python client.py -d BLE-SRV -c wifi-forget
python client.py -d BLE-SRV -c ntp-sync
```

---

## 4. LED Service (0xFFB0)

WS2812 RGB LED 控制与特效服务。需要 `CONFIG_BLE_SRV_LED=y`。

**GPIO 自动选择**：

| 芯片 | GPIO |
|------|------|
| ESP32-S3 | 21 |
| ESP32-C3/C6/H2 | 10 |
| ESP32 | 2 |
| ESP32-S2 | 18 |

| 特征 | UUID | 属性 | 说明 |
|------|------|------|------|
| LED Control | 0xFFB1 | Read + Write | 开关控制 |
| LED Color | 0xFFB2 | Read + Write | RGB 颜色 |
| LED Effect | 0xFFB3 | Read + Write | 特效设置 |

### 4.1 LED 开关 (0xFFB1)

**Write**: 1 byte 控制值

| 值 | 说明 |
|-----|------|
| 0x00 | 关闭 LED |
| 0x01 | 开启 LED |

**Read**: 1 byte 当前开关状态

**客户端命令**:
```bash
python client.py -d BLE-SRV -c led-on
python client.py -d BLE-SRV -c led-off
```

### 4.2 LED 颜色 (0xFFB2)

**Write**: 3 bytes RGB

| 偏移 | 长度 | 字段 | 类型 | 范围 |
|------|------|------|------|------|
| 0 | 1 | red | uint8 | 0x00-0xFF |
| 1 | 1 | green | uint8 | 0x00-0xFF |
| 2 | 1 | blue | uint8 | 0x00-0xFF |

**Read**: 3 bytes 当前 RGB

**客户端命令**: `python client.py -d BLE-SRV -c led-color --color FF0000`

颜色示例：`FF0000`=红 `00FF00`=绿 `0000FF`=蓝 `FFFFFF`=白 `FFAA00`=橙

### 4.3 LED 特效 (0xFFB3)

**Write**: 2 bytes 特效配置

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 1 | effect | uint8 | 特效类型 |
| 1 | 1 | speed | uint8 | 特效速度 (1-255) |

**Read**: 2 bytes 当前特效配置

**特效类型**：

| 特效 | 值 | 说明 |
|------|-----|------|
| NONE | 0x00 | 无特效，常亮显示当前颜色 |
| BREATH | 0x01 | 呼吸灯 — 渐亮渐暗循环 |
| BLINK | 0x02 | 闪烁 — 亮灭交替 |
| RAINBOW | 0x03 | 彩虹 — 自动循环色相 |
| STROBE | 0x04 | 频闪 — 快闪后暂停 |

> 特效切换为非阻塞式设计，从 BLE 写入后立即生效，无需等待旧特效结束。

**客户端命令**:
```bash
python client.py -d BLE-SRV -c led-effect --effect breath --speed 80
python client.py -d BLE-SRV -c led-effect --effect blink --speed 100
python client.py -d BLE-SRV -c led-effect --effect rainbow --speed 60
python client.py -d BLE-SRV -c led-effect --effect strobe --speed 50
python client.py -d BLE-SRV -c led-effect --effect none
```

### 4.4 LED 状态查询

**客户端命令**: `python client.py -d BLE-SRV -c led-status`

返回：LED 开关状态、当前颜色、当前特效和速度。

---

## 5. menuconfig 配置项

```
Component config → BLE Service:
  [*] Enable BLE Service
      Name prefix (BLE-SRV)          # BLE 广播名前缀
  [*] Enable WiFi Provisioner        # WiFi 配网
  [*] Enable NTP Time Sync           # NTP 时间同步
  [*] Enable WS2812 LED Control      # LED 控制

  NTP Configuration →
      Timezone (CST-8)
      NTP Server 1 (ntp.aliyun.com)
      NTP Server 2 (time1.aliyun.com)
      NTP Server 3 (cn.ntp.org.cn)
      NTP Server 4 (time.windows.com)
      NTP Server 5 (pool.ntp.org)
```

---

## 6. 文件结构

```
ble_srv/
  include/
    ble_srv.h              # 主聚合头文件
    ble_srv_gatt.h         # GATT 服务定义与 val_handle
    ble_srv_ota.h          # OTA 模块定义
    ble_srv_wifi.h         # WiFi 配网模块定义
    ble_srv_led.h          # LED 控制模块定义
    ble_srv_device.h       # 设备信息模块定义
  src/
    ble_srv_core.c         # NimBLE 初始化、GAP、广播
    ble_srv_gatt.c         # GATT 服务定义与 access callback
    ble_srv_ota.c          # OTA 固件升级（后台写入 + 校验）
    ble_srv_device.c       # 设备信息读取（芯片/内存/CPU/Flash/分区）
    ble_srv_wifi.c         # WiFi 配网（凭据管理/NVS/状态查询）
    ble_srv_led.c          # WS2812 LED 驱动（RMT + 非阻塞特效）
    ble_srv_ntp.c          # NTP 时间同步
  CMakeLists.txt
  Kconfig
  idf_component.yml
docs/
  BLE_COMMANDS.md          # 本文档
tools/
  client.py                # Python BLE 客户端
examples/
  basic/                   # 最小示例项目
```
