"""
BLE设备数据结构模型
"""

import struct
from datetime import timedelta
from typing import Optional

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
    VERSION_DOWNGRADE = 0x0B
    VERSION_SAME = 0x0C
    CRC_MISMATCH = 0x0D

class DeviceInfo:

    RESET_REASONS = {
        0: "未知",
        1: "上电复位",
        2: "外部复位",
        3: "软件复位",
        4: "异常死机",
        5: "中断看门狗",
        6: "任务看门狗",
        7: "其他看门狗",
        8: "深度睡眠唤醒",
        9: "掉电复位",
        10: "SDIO复位",
    }

    def __init__(self, data: bytes) -> None:
        if len(data) < 129:
            raise ValueError(f"DeviceInfo requires at least 129 bytes, got {len(data)}")
        self.cpu_freq_mhz: int = struct.unpack('<I', data[0:4])[0]
        self.uptime_seconds: int = struct.unpack('<I', data[4:8])[0]
        self.temperature_celsius: float = struct.unpack('<f', data[8:12])[0]
        self.temp_sensor_supported: bool = bool(struct.unpack('<B', data[12:13])[0])
        self.reset_reason: int = struct.unpack('<B', data[13:14])[0]
        self.cpu_cores: int = struct.unpack('<B', data[14:15])[0]
        self.chip_name: str = struct.unpack('<32s', data[15:47])[0].decode('utf-8', errors='replace').strip('\x00')
        self.chip_model: str = struct.unpack('<16s', data[47:63])[0].decode('utf-8', errors='replace').strip('\x00')
        self.flash_size: str = struct.unpack('<16s', data[63:79])[0].decode('utf-8', errors='replace').strip('\x00')
        self.mac_address: str = struct.unpack('<18s', data[79:97])[0].decode('utf-8', errors='replace').strip('\x00')
        self.version: str = struct.unpack('<32s', data[97:129])[0].decode('utf-8', errors='replace').strip('\x00')

    def get_reset_reason_name(self) -> str:
        return self.RESET_REASONS.get(self.reset_reason, f"未知({self.reset_reason})")

    def __str__(self) -> str:
        cpu_info = f"{self.cpu_freq_mhz}MHz"
        if self.cpu_cores > 0:
            cpu_info += f" ({self.cpu_cores}核)"
        result = f"芯片名称: {self.chip_name}\n芯片型号: {self.chip_model}\nFlash大小: {self.flash_size}\nMAC地址: {self.mac_address}\n版本: {self.version}\nCPU频率: {cpu_info}"
        if self.temp_sensor_supported:
            result += f"\n温度: {self.temperature_celsius:.2f}°C"
        else:
            result += "\n温度: 不支持"
        uptime = timedelta(seconds=self.uptime_seconds)
        result += f"\n运行时间: {uptime}\n上次重启原因: {self.get_reset_reason_name()}"
        return result

class MemoryInfo:
    def __init__(self, data: bytes) -> None:
        if len(data) < 40:
            raise ValueError(f"MemoryInfo requires at least 40 bytes, got {len(data)}")
        self.internal_total: int = struct.unpack('<I', data[0:4])[0]
        self.internal_free: int = struct.unpack('<I', data[4:8])[0]
        self.internal_min_free: int = struct.unpack('<I', data[8:12])[0]
        self.internal_largest: int = struct.unpack('<I', data[12:16])[0]
        self.psram_total: int = struct.unpack('<I', data[16:20])[0]
        self.psram_free: int = struct.unpack('<I', data[20:24])[0]
        self.psram_min_free: int = struct.unpack('<I', data[24:28])[0]
        self.psram_largest: int = struct.unpack('<I', data[28:32])[0]
        self.dma_free: int = struct.unpack('<I', data[32:36])[0]
        self.total_free: int = struct.unpack('<I', data[36:40])[0]
        self.task_count: int = struct.unpack('<H', data[40:42])[0] if len(data) >= 42 else 0
        self.stack_hwm: int = struct.unpack('<H', data[42:44])[0] if len(data) >= 44 else 0
        self._legacy = len(data) < 44

    @staticmethod
    def _fmt_kb(val: int) -> str:
        if val >= 1024 * 1024:
            return f"{val / (1024 * 1024):.1f} MB"
        return f"{val / 1024:.1f} KB"

    @staticmethod
    def _pct(part: int, total: int) -> str:
        if total <= 0:
            return "N/A"
        return f"{part * 100 / total:.1f}%"

    def __str__(self) -> str:
        lines = []
        lines.append(f"内部RAM: {self._fmt_kb(self.internal_free)}/{self._fmt_kb(self.internal_total)} "
                      f"({self._pct(self.internal_free, self.internal_total)})  "
                      f"最低:{self._fmt_kb(self.internal_min_free)}  最大块:{self._fmt_kb(self.internal_largest)}")

        if self.psram_total > 0:
            lines.append(f"PSRAM:   {self._fmt_kb(self.psram_free)}/{self._fmt_kb(self.psram_total)} "
                          f"({self._pct(self.psram_free, self.psram_total)})  "
                          f"最低:{self._fmt_kb(self.psram_min_free)}  最大块:{self._fmt_kb(self.psram_largest)}")

        lines.append(f"其他:    DMA:{self._fmt_kb(self.dma_free)}  总可用:{self._fmt_kb(self.total_free)}  "
                      f"任务:{self.task_count}个  BLE栈:{self._fmt_kb(self.stack_hwm)}")
        return "\n".join(lines)

class CPUInfo:

    FEATURE_NAMES = {
        0: "嵌入式Flash",
        1: "2.4GHz WiFi",
        4: "BLE",
        5: "经典蓝牙",
        6: "IEEE 802.15.4",
    }

    def __init__(self, data: bytes) -> None:
        self.cpu_freq_mhz: int = struct.unpack('<I', data[0:4])[0]
        self.uptime_seconds: int = struct.unpack('<I', data[4:8])[0]
        self.features: int = struct.unpack('<I', data[8:12])[0] if len(data) >= 12 else 0
        self.task_count: int = struct.unpack('<H', data[12:14])[0] if len(data) >= 14 else 0
        self.cpu_cores: int = struct.unpack('<B', data[14:15])[0] if len(data) >= 15 else 0
        self.cpu_usage: int = struct.unpack('<B', data[15:16])[0] if len(data) >= 16 else 0
        self.chip_revision: int = struct.unpack('<B', data[16:17])[0] if len(data) >= 17 else 0
        self.idf_version: str = struct.unpack('<24s', data[17:41])[0].decode('utf-8', errors='replace').strip('\x00') if len(data) >= 41 else ""
        self._legacy = len(data) < 41

    def _get_feature_list(self) -> str:
        feats = []
        for bit, name in self.FEATURE_NAMES.items():
            if self.features & (1 << bit):
                feats.append(name)
        return ", ".join(feats) if feats else "无"

    def __str__(self) -> str:
        uptime = timedelta(seconds=self.uptime_seconds)
        if self._legacy:
            return f"CPU频率: {self.cpu_freq_mhz}MHz\nCPU使用率: {self.cpu_usage}%\n运行时间: {uptime}"
        cores_str = f" ({self.cpu_cores}核)" if self.cpu_cores > 0 else ""
        lines = []
        lines.append(f"CPU: {self.cpu_freq_mhz}MHz{cores_str} rev{self.chip_revision}  任务:{self.task_count}个  运行:{uptime}")
        lines.append(f"功能: {self._get_feature_list()}")
        if self.idf_version:
            lines.append(f"ESP-IDF: {self.idf_version}")
        return "\n".join(lines)

class FlashInfo:
    def __init__(self, data: bytes) -> None:
        self.flash_total: int = struct.unpack('<I', data[0:4])[0]
        self.flash_free: int = struct.unpack('<I', data[4:8])[0]
        self.flash_speed_mhz: int = struct.unpack('<B', data[8:9])[0]
        self.partition_count: int = struct.unpack('<B', data[9:10])[0]
        self.running_partition: str = struct.unpack('<16s', data[10:26])[0].decode('utf-8', errors='replace').strip('\x00') if len(data) >= 26 else ""
        self._legacy = len(data) < 26

    @staticmethod
    def _fmt_mb(val: int) -> str:
        return f"{val / (1024 * 1024):.1f} MB"

    @staticmethod
    def _pct(part: int, total: int) -> str:
        if total <= 0:
            return "N/A"
        return f"{part * 100 / total:.1f}%"

    def __str__(self) -> str:
        if self._legacy:
            return f"Flash总量: {self._fmt_mb(self.flash_total)}\nFlash可用: {self._fmt_mb(self.flash_free)}\n分区数量: {self.partition_count}"
        used = self.flash_total - self.flash_free
        speed_str = f"{self.flash_speed_mhz}MHz" if self.flash_speed_mhz > 0 else "未知"
        lines = []
        lines.append(f"Flash: {self._fmt_mb(self.flash_total)}  已用:{self._fmt_mb(used)} ({self._pct(used, self.flash_total)})  空闲:{self._fmt_mb(self.flash_free)}")
        lines.append(f"速度: {speed_str}  分区:{self.partition_count}个  当前分区:{self.running_partition or '未知'}")
        return "\n".join(lines)

class PartitionInfo:
    def __init__(self, data: bytes) -> None:
        if len(data) < 26:
            raise ValueError(f"PartitionInfo requires at least 26 bytes, got {len(data)}")
        self.address: int = struct.unpack('<I', data[0:4])[0]
        self.size: int = struct.unpack('<I', data[4:8])[0]
        self.type: int = struct.unpack('<B', data[8:9])[0]
        self.subtype: int = struct.unpack('<B', data[9:10])[0]
        self.label: str = struct.unpack('<16s', data[10:26])[0].decode('utf-8', errors='replace').strip('\x00')

    def get_type_name(self) -> str:
        types = {0: 'app', 1: 'data'}
        return types.get(self.type, f'unknown({self.type})')

    def get_subtype_name(self) -> str:
        if self.type == 0:
            subtypes = {0: 'factory', 16: 'ota_0', 17: 'ota_1'}
        elif self.type == 1:
            subtypes = {2: 'phy', 4: 'nvs', 14: 'otadata'}
        else:
            subtypes = {}
        return subtypes.get(self.subtype, f'unknown({self.subtype})')

    def __str__(self) -> str:
        return f"{self.label:12} | 地址: 0x{self.address:08X} | 大小: {self.size / 1024:6.1f} KB | 类型: {self.get_type_name():8} | 子类型: {self.get_subtype_name()}"

class OTAStatus:
    def __init__(self, data: bytes) -> None:
        if len(data) < 11:
            raise ValueError(f"OTAStatus requires at least 11 bytes, got {len(data)}")
        self.fw_size: int = struct.unpack('<I', data[0:4])[0]
        self.bytes_written: int = struct.unpack('<I', data[4:8])[0]
        self.state: int = struct.unpack('<B', data[8:9])[0]
        self.error_code: int = struct.unpack('<B', data[9:10])[0]
        self.progress: int = struct.unpack('<B', data[10:11])[0]

    def __str__(self) -> str:
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
            OTAError.DISCONNECTED: "连接断开",
            OTAError.VERSION_DOWNGRADE: "远程版本更旧",
            OTAError.VERSION_SAME: "版本相同",
            OTAError.CRC_MISMATCH: "固件CRC校验失败"
        }
        return f"状态: {state_names.get(self.state, f'未知({self.state})')}\n错误: {error_names.get(self.error_code, f'未知({self.error_code})')}\n固件大小: {self.fw_size} bytes\n已写入: {self.bytes_written} bytes\n进度: {self.progress}%"

class WiFiStatus:
    def __init__(self, data: bytes) -> None:
        if len(data) < 6:
            raise ValueError(f"WiFiStatus requires at least 6 bytes, got {len(data)}")
        self.ip_address: int = struct.unpack('<I', data[0:4])[0]
        self.connected: bool = bool(struct.unpack('<B', data[4:5])[0])
        self.rssi: int = struct.unpack('<B', data[5:6])[0]

    def __str__(self) -> str:
        if self.connected:
            ip = f"{self.ip_address & 0xFF}.{(self.ip_address >> 8) & 0xFF}.{(self.ip_address >> 16) & 0xFF}.{(self.ip_address >> 24) & 0xFF}"
            return f"状态: 已连接\n信号强度: -{self.rssi} dBm\nIP地址: {ip}"
        return f"状态: 未连接"

class TemperatureInfo:
    def __init__(self, data: bytes) -> None:
        if len(data) < 8:
            raise ValueError(f"TemperatureInfo requires at least 8 bytes, got {len(data)}")
        self.temperature_celsius: float = struct.unpack('<h', data[0:2])[0] / 10.0
        self.temperature_min: float = struct.unpack('<h', data[2:4])[0] / 10.0
        self.temperature_max: float = struct.unpack('<h', data[4:6])[0] / 10.0
        self.temperature_samples: int = struct.unpack('<H', data[6:8])[0]

    def __str__(self) -> str:
        return f"当前温度: {self.temperature_celsius:.1f}°C\n最低温度: {self.temperature_min:.1f}°C\n最高温度: {self.temperature_max:.1f}°C\n采样次数: {self.temperature_samples}"

class PowerInfo:
    def __init__(self, data: bytes) -> None:
        if len(data) < 32:
            raise ValueError(f"PowerInfo requires at least 32 bytes, got {len(data)}")
        self.total_power_mw: int = struct.unpack('<I', data[0:4])[0]
        self.cpu_power_mw: int = struct.unpack('<I', data[4:8])[0]
        self.wifi_power_mw: int = struct.unpack('<I', data[8:12])[0]
        self.ble_power_mw: int = struct.unpack('<I', data[12:16])[0]
        self.peripherals_power_mw: int = struct.unpack('<I', data[16:20])[0]
        self.power_min_mw: int = struct.unpack('<I', data[20:24])[0]
        self.power_max_mw: int = struct.unpack('<I', data[24:28])[0]
        self.power_samples: int = struct.unpack('<I', data[28:32])[0]

    def __str__(self) -> str:
        return f"系统总功耗: {self.total_power_mw} mW\nCPU功耗: {self.cpu_power_mw} mW\nWiFi功耗: {self.wifi_power_mw} mW\n蓝牙功耗: {self.ble_power_mw} mW\n外设功耗: {self.peripherals_power_mw} mW\n最低功耗: {self.power_min_mw} mW\n最高功耗: {self.power_max_mw} mW\n采样次数: {self.power_samples}"

class LogStorageInfo:
    STORAGE_TYPE_NONE = 0
    STORAGE_TYPE_LITTLEFS = 1
    STORAGE_TYPE_SD = 2

    STORAGE_TYPE_NAMES = {
        0: "未初始化",
        1: "LittleFS",
        2: "SD卡",
    }

    def __init__(self, data: bytes) -> None:
        if len(data) < 17:
            raise ValueError(f"LogStorageInfo requires at least 17 bytes, got {len(data)}")
        self.total_size: int = struct.unpack('<I', data[0:4])[0]
        self.used_size: int = struct.unpack('<I', data[4:8])[0]
        self.free_size: int = struct.unpack('<I', data[8:12])[0]
        self.file_count: int = struct.unpack('<I', data[12:16])[0]
        self.storage_type: int = struct.unpack('<B', data[16:17])[0]

    @staticmethod
    def _fmt_size(val: int) -> str:
        if val >= 1024 * 1024:
            return f"{val / (1024 * 1024):.2f} MB"
        elif val >= 1024:
            return f"{val / 1024:.2f} KB"
        return f"{val} B"

    @staticmethod
    def _pct(part: int, total: int) -> str:
        if total <= 0:
            return "N/A"
        return f"{part * 100 / total:.1f}%"

    def get_storage_type_name(self) -> str:
        return self.STORAGE_TYPE_NAMES.get(self.storage_type, f"未知({self.storage_type})")

    def __str__(self) -> str:
        lines = []
        lines.append(f"存储类型: {self.get_storage_type_name()}")
        lines.append(f"总大小: {self._fmt_size(self.total_size)}")
        lines.append(f"已用: {self._fmt_size(self.used_size)} ({self._pct(self.used_size, self.total_size)})")
        lines.append(f"剩余: {self._fmt_size(self.free_size)} ({self._pct(self.free_size, self.total_size)})")
        lines.append(f"日志文件数: {self.file_count}")
        return "\n".join(lines)