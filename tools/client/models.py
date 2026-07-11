"""
BLE设备数据结构模型
"""

import struct
from datetime import timedelta

class OTAState:
    IDLE = 0x00
    CHECKING = 0x01
    CHECK_OK = 0x02
    CHECK_FAIL = 0x03
    RECEIVING = 0x04
    VERIFYING = 0x05
    VERIFY_OK = 0x06
    VERIFY_FAIL = 0x07
    APPLYING = 0x08
    APPLY_OK = 0x09
    APPLY_FAIL = 0x0A
    ABORTING = 0x0B
    ABORTED = 0x0C
    ERROR = 0x0D

class OTAError:
    NONE = 0x00
    INVALID_CMD = 0x01
    INVALID_SIZE = 0x02
    FLASH_WRITE = 0x03
    NO_PARTITION = 0x04
    VERIFY_FAILED = 0x05
    INTERNAL = 0x06
    BUSY = 0x07
    NO_NETWORK = 0x08
    ABORTED = 0x09
    DISCONNECTED = 0x0A

class DeviceInfo:
    def __init__(self, data):
        self.chip_name = struct.unpack('<32s', data[0:32])[0].decode('utf-8').strip('\x00')
        self.chip_model = struct.unpack('<16s', data[32:48])[0].decode('utf-8').strip('\x00')
        self.flash_size = struct.unpack('<16s', data[48:64])[0].decode('utf-8').strip('\x00')
        self.mac_address = struct.unpack('<18s', data[64:82])[0].decode('utf-8').strip('\x00')
        self.version = struct.unpack('<32s', data[82:114])[0].decode('utf-8').strip('\x00')
        self.cpu_freq_mhz = struct.unpack('<I', data[114:118])[0]
        self.temperature_celsius = struct.unpack('<f', data[118:122])[0]
        self.temp_sensor_supported = struct.unpack('<B', data[122:123])[0]

    def __str__(self):
        result = f"芯片名称: {self.chip_name}\n芯片型号: {self.chip_model}\nFlash大小: {self.flash_size}\nMAC地址: {self.mac_address}\n版本: {self.version}\nCPU频率: {self.cpu_freq_mhz}MHz"
        if self.temp_sensor_supported:
            result += f"\n温度: {self.temperature_celsius:.2f}°C"
        else:
            result += "\n温度: 不支持"
        return result

class MemoryInfo:
    def __init__(self, data):
        self.heap_total = struct.unpack('<I', data[0:4])[0]
        self.heap_free = struct.unpack('<I', data[4:8])[0]
        self.heap_min_free = struct.unpack('<I', data[8:12])[0]
        self.stack_high_watermark = struct.unpack('<I', data[12:16])[0]
        if len(data) >= 28:
            self.psram_total = struct.unpack('<I', data[16:20])[0]
            self.psram_free = struct.unpack('<I', data[20:24])[0]
            self.psram_min_free = struct.unpack('<I', data[24:28])[0]
        else:
            self.psram_total = 0
            self.psram_free = 0
            self.psram_min_free = 0

    def __str__(self):
        result = f"堆内存总量: {self.heap_total / 1024:.1f} KB\n堆内存可用: {self.heap_free / 1024:.1f} KB\n堆内存最小可用: {self.heap_min_free / 1024:.1f} KB\n栈高水位: {self.stack_high_watermark} bytes"
        if self.psram_total > 0:
            result += f"\nPSRAM总量: {self.psram_total / 1024:.1f} KB\nPSRAM可用: {self.psram_free / 1024:.1f} KB\nPSRAM最小可用: {self.psram_min_free / 1024:.1f} KB"
        return result

class CPUInfo:
    def __init__(self, data):
        self.cpu_freq_mhz = struct.unpack('<I', data[0:4])[0]
        self.cpu_usage = struct.unpack('<I', data[4:8])[0]
        self.uptime_seconds = struct.unpack('<I', data[8:12])[0]

    def __str__(self):
        uptime = timedelta(seconds=self.uptime_seconds)
        return f"CPU频率: {self.cpu_freq_mhz}MHz\nCPU使用率: {self.cpu_usage}%\n运行时间: {uptime}"

class FlashInfo:
    def __init__(self, data):
        self.flash_total = struct.unpack('<I', data[0:4])[0]
        self.flash_free = struct.unpack('<I', data[4:8])[0]
        self.partition_count = struct.unpack('<B', data[8:9])[0]

    def __str__(self):
        return f"Flash总量: {self.flash_total / (1024 * 1024):.1f} MB\nFlash可用: {self.flash_free / (1024 * 1024):.1f} MB\n分区数量: {self.partition_count}"

class PartitionInfo:
    def __init__(self, data):
        self.label = struct.unpack('<16s', data[0:16])[0].decode('utf-8').strip('\x00')
        self.address = struct.unpack('<I', data[16:20])[0]
        self.size = struct.unpack('<I', data[20:24])[0]
        self.type = struct.unpack('<B', data[24:25])[0]
        self.subtype = struct.unpack('<B', data[25:26])[0]

    def get_type_name(self):
        types = {0: 'app', 1: 'data'}
        return types.get(self.type, f'unknown({self.type})')

    def get_subtype_name(self):
        if self.type == 0:
            subtypes = {0: 'factory', 16: 'ota_0', 17: 'ota_1'}
        elif self.type == 1:
            subtypes = {2: 'phy', 4: 'nvs', 14: 'otadata'}
        else:
            subtypes = {}
        return subtypes.get(self.subtype, f'unknown({self.subtype})')

    def __str__(self):
        return f"{self.label:12} | 地址: 0x{self.address:08X} | 大小: {self.size / 1024:6.1f} KB | 类型: {self.get_type_name():8} | 子类型: {self.get_subtype_name()}"

class OTAStatus:
    def __init__(self, data):
        self.state = struct.unpack('<B', data[0:1])[0]
        self.error_code = struct.unpack('<B', data[1:2])[0]
        self.fw_size = struct.unpack('<I', data[2:6])[0]
        self.bytes_written = struct.unpack('<I', data[6:10])[0]
        self.progress = struct.unpack('<B', data[10:11])[0]

    def __str__(self):
        state_names = {
            OTAState.IDLE: "空闲",
            OTAState.CHECKING: "检查版本中",
            OTAState.CHECK_OK: "版本检查通过",
            OTAState.CHECK_FAIL: "无需更新",
            OTAState.RECEIVING: "接收中",
            OTAState.VERIFYING: "校验中",
            OTAState.VERIFY_OK: "校验成功",
            OTAState.VERIFY_FAIL: "校验失败",
            OTAState.APPLYING: "应用中",
            OTAState.APPLY_OK: "应用成功",
            OTAState.APPLY_FAIL: "应用失败",
            OTAState.ABORTING: "中止中",
            OTAState.ABORTED: "已中止",
            OTAState.ERROR: "错误"
        }
        error_names = {
            OTAError.NONE: "无错误",
            OTAError.INVALID_CMD: "无效命令",
            OTAError.INVALID_SIZE: "无效大小",
            OTAError.FLASH_WRITE: "Flash写入错误",
            OTAError.NO_PARTITION: "无可用分区",
            OTAError.VERIFY_FAILED: "校验失败",
            OTAError.INTERNAL: "内部错误",
            OTAError.BUSY: "设备忙",
            OTAError.NO_NETWORK: "网络未连接",
            OTAError.ABORTED: "用户中止",
            OTAError.DISCONNECTED: "连接断开"
        }
        return f"状态: {state_names.get(self.state, f'未知({self.state})')}\n错误: {error_names.get(self.error_code, f'未知({self.error_code})')}\n固件大小: {self.fw_size} bytes\n已写入: {self.bytes_written} bytes\n进度: {self.progress}%"

class WiFiStatus:
    def __init__(self, data):
        self.connected = struct.unpack('<B', data[0:1])[0]
        self.rssi = struct.unpack('<B', data[1:2])[0]
        self.ip_address = struct.unpack('<I', data[2:6])[0]

    def __str__(self):
        if self.connected:
            ip = f"{self.ip_address & 0xFF}.{(self.ip_address >> 8) & 0xFF}.{(self.ip_address >> 16) & 0xFF}.{(self.ip_address >> 24) & 0xFF}"
            return f"状态: 已连接\n信号强度: -{self.rssi} dBm\nIP地址: {ip}"
        return f"状态: 未连接"

class TemperatureInfo:
    def __init__(self, data):
        self.temperature_celsius = struct.unpack('<h', data[0:2])[0] / 10.0
        self.temperature_min = struct.unpack('<h', data[2:4])[0] / 10.0
        self.temperature_max = struct.unpack('<h', data[4:6])[0] / 10.0
        self.temperature_samples = struct.unpack('<H', data[6:8])[0]

    def __str__(self):
        return f"当前温度: {self.temperature_celsius:.1f}°C\n最低温度: {self.temperature_min:.1f}°C\n最高温度: {self.temperature_max:.1f}°C\n采样次数: {self.temperature_samples}"

class PowerInfo:
    def __init__(self, data):
        self.total_power_mw = struct.unpack('<I', data[0:4])[0]
        self.cpu_power_mw = struct.unpack('<I', data[4:8])[0]
        self.wifi_power_mw = struct.unpack('<I', data[8:12])[0]
        self.ble_power_mw = struct.unpack('<I', data[12:16])[0]
        self.peripherals_power_mw = struct.unpack('<I', data[16:20])[0]
        self.power_min_mw = struct.unpack('<I', data[20:24])[0]
        self.power_max_mw = struct.unpack('<I', data[24:28])[0]
        self.power_samples = struct.unpack('<I', data[28:32])[0]

    def __str__(self):
        return f"系统总功耗: {self.total_power_mw} mW\nCPU功耗: {self.cpu_power_mw} mW\nWiFi功耗: {self.wifi_power_mw} mW\n蓝牙功耗: {self.ble_power_mw} mW\n外设功耗: {self.peripherals_power_mw} mW\n最低功耗: {self.power_min_mw} mW\n最高功耗: {self.power_max_mw} mW\n采样次数: {self.power_samples}"