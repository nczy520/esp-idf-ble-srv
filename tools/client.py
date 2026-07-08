import asyncio
import struct
import zlib
import sys
import os
import time
import argparse
from datetime import timedelta

try:
    from bleak import BleakClient, BleakScanner, BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    sys.exit(1)

BLE_DM_SVC_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
BLE_DM_CMD_CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
BLE_DM_INFO_CHAR_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb"
BLE_DM_MEMORY_CHAR_UUID = "0000ffe3-0000-1000-8000-00805f9b34fb"
BLE_DM_CPU_CHAR_UUID = "0000ffe4-0000-1000-8000-00805f9b34fb"
BLE_DM_FLASH_CHAR_UUID = "0000ffe5-0000-1000-8000-00805f9b34fb"
BLE_DM_PARTITION_CHAR_UUID = "0000ffe7-0000-1000-8000-00805f9b34fb"
BLE_DM_RESTART_CHAR_UUID = "0000ffe6-0000-1000-8000-00805f9b34fb"

BLE_OTA_SVC_UUID = "0000ffd0-0000-1000-8000-00805f9b34fb"
BLE_OTA_CMD_CHAR_UUID = "0000ffd1-0000-1000-8000-00805f9b34fb"
BLE_OTA_FW_DATA_CHAR_UUID = "0000ffd2-0000-1000-8000-00805f9b34fb"
BLE_OTA_STATUS_CHAR_UUID = "0000ffd3-0000-1000-8000-00805f9b34fb"

BLE_WIFI_SVC_UUID = "0000ffc0-0000-1000-8000-00805f9b34fb"
BLE_WIFI_CONFIG_CHAR_UUID = "0000ffc1-0000-1000-8000-00805f9b34fb"
BLE_WIFI_STATUS_CHAR_UUID = "0000ffc2-0000-1000-8000-00805f9b34fb"
BLE_WIFI_CTRL_CHAR_UUID = "0000ffc3-0000-1000-8000-00805f9b34fb"

BLE_LED_SVC_UUID = "0000ffb0-0000-1000-8000-00805f9b34fb"
BLE_LED_CTRL_CHAR_UUID = "0000ffb1-0000-1000-8000-00805f9b34fb"
BLE_LED_COLOR_CHAR_UUID = "0000ffb2-0000-1000-8000-00805f9b34fb"
BLE_LED_EFFECT_CHAR_UUID = "0000ffb3-0000-1000-8000-00805f9b34fb"

BLE_DM_CMD_GET_INFO = 0x01
BLE_DM_CMD_GET_MEMORY = 0x02
BLE_DM_CMD_GET_CPU = 0x03
BLE_DM_CMD_GET_FLASH = 0x04
BLE_DM_CMD_RESTART = 0x05

BLE_OTA_CMD_START = 0x01
BLE_OTA_CMD_ABORT = 0x02
BLE_OTA_CMD_VERIFY = 0x03
BLE_OTA_CMD_APPLY = 0x04

BLE_WIFI_CTRL_FORGET = 0x01
BLE_WIFI_CTRL_NTP_SYNC = 0x02

BLE_LED_CTRL_OFF = 0x00
BLE_LED_CTRL_ON = 0x01

BLE_LED_EFFECT_NONE = 0x00
BLE_LED_EFFECT_BREATH = 0x01
BLE_LED_EFFECT_BLINK = 0x02
BLE_LED_EFFECT_RAINBOW = 0x03
BLE_LED_EFFECT_STROBE = 0x04

class OTAState:
    IDLE = 0x00
    RECEIVING = 0x01
    VERIFYING = 0x02
    VERIFY_OK = 0x03
    VERIFY_FAIL = 0x04
    APPLYING = 0x05
    APPLY_OK = 0x06
    APPLY_FAIL = 0x07
    ERROR = 0x08

class OTAError:
    NONE = 0x00
    INVALID_CMD = 0x01
    INVALID_SIZE = 0x02
    FLASH_WRITE = 0x03
    NO_PARTITION = 0x04
    VERIFY_FAILED = 0x05
    INTERNAL = 0x06
    BUSY = 0x07

class DeviceInfo:
    def __init__(self, data):
        self.chip_name = struct.unpack('<32s', data[0:32])[0].decode('utf-8').strip('\x00')
        self.chip_model = struct.unpack('<16s', data[32:48])[0].decode('utf-8').strip('\x00')
        self.flash_size = struct.unpack('<16s', data[48:64])[0].decode('utf-8').strip('\x00')
        self.mac_address = struct.unpack('<18s', data[64:82])[0].decode('utf-8').strip('\x00')
        self.version = struct.unpack('<32s', data[82:114])[0].decode('utf-8').strip('\x00')
        self.cpu_freq_mhz = struct.unpack('<I', data[114:118])[0]

    def __str__(self):
        return f"芯片名称: {self.chip_name}\n芯片型号: {self.chip_model}\nFlash大小: {self.flash_size}\nMAC地址: {self.mac_address}\n版本: {self.version}\nCPU频率: {self.cpu_freq_mhz}MHz"

class MemoryInfo:
    def __init__(self, data):
        self.heap_total = struct.unpack('<I', data[0:4])[0]
        self.heap_free = struct.unpack('<I', data[4:8])[0]
        self.heap_min_free = struct.unpack('<I', data[8:12])[0]
        self.stack_high_watermark = struct.unpack('<I', data[12:16])[0]

    def __str__(self):
        return f"堆内存总量: {self.heap_total / 1024:.1f} KB\n堆内存可用: {self.heap_free / 1024:.1f} KB\n堆内存最小可用: {self.heap_min_free / 1024:.1f} KB\n栈高水位: {self.stack_high_watermark} bytes"

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
        return f"标签: {self.label}\n地址: 0x{self.address:08X}\n大小: {self.size / 1024:.1f} KB\n类型: {self.get_type_name()}\n子类型: {self.get_subtype_name()}"

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
            OTAState.RECEIVING: "接收中",
            OTAState.VERIFYING: "校验中",
            OTAState.VERIFY_OK: "校验成功",
            OTAState.VERIFY_FAIL: "校验失败",
            OTAState.APPLYING: "应用中",
            OTAState.APPLY_OK: "应用成功",
            OTAState.APPLY_FAIL: "应用失败",
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
            OTAError.BUSY: "设备忙"
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

class BLEDeviceManagerClient:
    def __init__(self, device_name=None, address=None):
        self.device_name = device_name
        self.address = address
        self.client = None
        self.ota_status = None
        self.ota_progress_callback = None

    async def scan(self, timeout=5):
        print(f"扫描 BLE 设备...")
        devices = await BleakScanner.discover(timeout=timeout)
        esp32_devices = []
        for d in devices:
            if d.name and (self.device_name is None or d.name.startswith(self.device_name)):
                esp32_devices.append(d)
                print(f"  发现: {d.name} ({d.address})")
        if not esp32_devices:
            print("未发现 ESP32 BLE 设备")
            return None
        if len(esp32_devices) == 1:
            return esp32_devices[0]
        print(f"\n发现 {len(esp32_devices)} 个设备，自动选择第一个:")
        for i, d in enumerate(esp32_devices):
            print(f"  {i+1}. {d.name} ({d.address})")
        return esp32_devices[0]

    async def connect(self, device=None):
        if device is None:
            device = await self.scan()
            if device is None:
                return False

        self.address = device.address
        print(f"\n连接设备: {device.name} ({device.address})")
        try:
            self.client = BleakClient(device, timeout=15)
            await self.client.connect()
            print("连接成功")
            return True
        except BleakError as e:
            print(f"连接失败: {e}")
            return False

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("已断开连接")

    async def read_device_info(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_DM_INFO_CHAR_UUID)
            return DeviceInfo(data)
        except BleakError as e:
            print(f"读取设备信息失败: {e}")
            return None

    async def read_memory_info(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_DM_MEMORY_CHAR_UUID)
            return MemoryInfo(data)
        except BleakError as e:
            print(f"读取内存信息失败: {e}")
            return None

    async def read_cpu_info(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_DM_CPU_CHAR_UUID)
            return CPUInfo(data)
        except BleakError as e:
            print(f"读取CPU信息失败: {e}")
            return None

    async def read_flash_info(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_DM_FLASH_CHAR_UUID)
            return FlashInfo(data)
        except BleakError as e:
            print(f"读取Flash信息失败: {e}")
            return None

    async def read_partition_info(self, index=0):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            await self.client.write_gatt_char(BLE_DM_PARTITION_CHAR_UUID, bytes([index]))
            await asyncio.sleep(0.1)
            data = await self.client.read_gatt_char(BLE_DM_PARTITION_CHAR_UUID)
            return PartitionInfo(data)
        except BleakError as e:
            print(f"读取分区信息失败: {e}")
            return None

    async def read_all_partitions(self):
        flash_info = await self.read_flash_info()
        if flash_info is None:
            return []
        partitions = []
        for i in range(flash_info.partition_count):
            part = await self.read_partition_info(i)
            if part:
                partitions.append(part)
        return partitions

    async def restart_device(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_DM_RESTART_CHAR_UUID, bytes([BLE_DM_CMD_RESTART]))
            print("重启命令已发送，设备即将重启")
            return True
        except BleakError as e:
            print(f"发送重启命令失败: {e}")
            return False

    def _ota_status_handler(self, sender, data):
        try:
            self.ota_status = OTAStatus(data)
        except Exception as e:
            print(f"解析OTA状态失败: {e}")

    def _print_progress(self, sent_bytes, fw_size, start_time, device_status=None):
        pct = min(100, int(sent_bytes * 100 / fw_size))
        bar_len = 30
        filled = int(bar_len * pct / 100)
        bar = '=' * filled + '>' + ' ' * (bar_len - filled - 1)

        sent_str = f"{sent_bytes / 1024:.1f}KB" if sent_bytes >= 1024 else f"{sent_bytes}B"
        total_str = f"{fw_size / 1024:.1f}KB" if fw_size >= 1024 else f"{fw_size}B"

        elapsed = time.time() - start_time
        if elapsed > 0 and sent_bytes > 0:
            speed = sent_bytes / elapsed
            speed_str = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            remain = fw_size - sent_bytes
            eta = remain / speed if speed > 0 else 0
            eta_str = f"{int(eta)}s" if eta < 60 else f"{int(eta / 60)}m{int(eta % 60)}s"
        else:
            speed_str = "0B/s"
            eta_str = "--"

        if device_status and 0 < device_status.bytes_written <= fw_size:
            dev_pct = device_status.progress
            dev_str = f"{device_status.bytes_written / 1024:.1f}KB"
            line = f"\r[{bar}] {pct}% {speed_str} ETA:{eta_str} | 发送:{sent_str}/{total_str} 写入:{dev_str}({dev_pct}%)"
        else:
            line = f"\r[{bar}] {pct}% {speed_str} ETA:{eta_str} | 发送:{sent_str}/{total_str}"

        print(line.ljust(90), end='', flush=True)

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)

            cmd_data = struct.pack('<BIIHHI', BLE_OTA_CMD_START, fw_size, fw_crc, chunk_size, 0, fw_version)
            await self.client.write_gatt_char(BLE_OTA_CMD_CHAR_UUID, cmd_data)

            await asyncio.sleep(0.5)
            if self.ota_status and self.ota_status.state != OTAState.RECEIVING:
                print(f"OTA启动失败: {self.ota_status}")
                return False
            print("OTA会话已启动")
            return True
        except BleakError as e:
            print(f"OTA启动失败: {e}")
            return False

    async def ota_send_fw_data(self, data):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_FW_DATA_CHAR_UUID, data)
            return True
        except BleakError as e:
            print(f"发送固件数据失败: {e}")
            return False

    async def ota_verify(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_CMD_CHAR_UUID, bytes([BLE_OTA_CMD_VERIFY]))

            for _ in range(30):
                await asyncio.sleep(0.5)
                if self.ota_status:
                    if self.ota_status.state == OTAState.VERIFY_OK:
                        print("\nOTA校验成功")
                        return True
                    elif self.ota_status.state in [OTAState.VERIFY_FAIL, OTAState.ERROR]:
                        print(f"\nOTA校验失败: {self.ota_status}")
                        return False
            print("\nOTA校验超时")
            return False
        except BleakError as e:
            print(f"\nOTA校验失败: {e}")
            return False

    async def ota_apply(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_CMD_CHAR_UUID, bytes([BLE_OTA_CMD_APPLY]))
            print("OTA应用命令已发送，设备即将重启")
            return True
        except BleakError as e:
            err_msg = str(e).lower()
            if "disconnect" in err_msg or "disconnected" in err_msg:
                print("OTA应用成功，设备已断开连接并重启")
                return True
            print(f"OTA应用失败: {e}")
            return False

    async def ota_abort(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_CMD_CHAR_UUID, bytes([BLE_OTA_CMD_ABORT]))
            print("OTA中止命令已发送")
            return True
        except BleakError as e:
            print(f"OTA中止失败: {e}")
            return False

    async def wifi_connect(self, ssid, password=""):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            ssid_bytes = ssid.encode('utf-8').ljust(33, b'\x00')[:33]
            pass_bytes = password.encode('utf-8').ljust(65, b'\x00')[:65]
            config_data = ssid_bytes + pass_bytes
            await self.client.write_gatt_char(BLE_WIFI_CONFIG_CHAR_UUID, config_data)
            print(f"WiFi配置已发送: SSID={ssid}")

            await asyncio.sleep(3)

            status = await self.wifi_status()
            if status:
                print(f"WiFi状态: {status}")
            return True
        except BleakError as e:
            print(f"WiFi配置失败: {e}")
            return False

    async def wifi_status(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_WIFI_STATUS_CHAR_UUID)
            return WiFiStatus(data)
        except BleakError as e:
            print(f"读取WiFi状态失败: {e}")
            return None

    async def wifi_forget(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_FORGET]))
            print("WiFi忘记命令已发送，设备将断开WiFi并进入配网模式")
            return True
        except BleakError as e:
            print(f"WiFi忘记命令失败: {e}")
            return False

    async def wifi_ntp_sync(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]))
            print("NTP同步命令已发送")
            return True
        except BleakError as e:
            print(f"NTP同步命令失败: {e}")
            return False

    async def led_on(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]))
            print("LED已开启")
            return True
        except BleakError as e:
            print(f"LED开启失败: {e}")
            return False

    async def led_off(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]))
            print("LED已关闭")
            return True
        except BleakError as e:
            print(f"LED关闭失败: {e}")
            return False

    async def led_set_color(self, color_str):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            color_str = color_str.lstrip('#')
            if len(color_str) != 6:
                print("颜色格式错误，请使用AABBCC格式（如FF0000=红色）")
                return False
            red = int(color_str[0:2], 16)
            green = int(color_str[2:4], 16)
            blue = int(color_str[4:6], 16)
            await self.client.write_gatt_char(BLE_LED_COLOR_CHAR_UUID, bytes([red, green, blue]))
            print(f"LED颜色已设置: #{color_str.upper()} (R={red} G={green} B={blue})")
            return True
        except BleakError as e:
            print(f"LED颜色设置失败: {e}")
            return False

    async def led_status(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            on_data = await self.client.read_gatt_char(BLE_LED_CTRL_CHAR_UUID)
            color_data = await self.client.read_gatt_char(BLE_LED_COLOR_CHAR_UUID)
            effect_data = await self.client.read_gatt_char(BLE_LED_EFFECT_CHAR_UUID)
            is_on = struct.unpack('<B', on_data[0:1])[0]
            red = color_data[0]
            green = color_data[1]
            blue = color_data[2]
            effect = effect_data[0]
            speed = effect_data[1]
            state = "开启" if is_on else "关闭"
            effect_names = {0: "无", 1: "呼吸灯", 2: "闪烁", 3: "彩虹", 4: "频闪"}
            effect_str = effect_names.get(effect, f"未知({effect})")
            print(f"LED状态: {state}, 颜色: #{red:02X}{green:02X}{blue:02X}, 特效: {effect_str}, 速度: {speed}")
            return {"on": bool(is_on), "red": red, "green": green, "blue": blue,
                    "effect": effect, "speed": speed}
        except BleakError as e:
            print(f"读取LED状态失败: {e}")
            return None

    async def led_set_effect(self, effect, speed=50):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            effect_names = {0: "无", 1: "呼吸灯", 2: "闪烁", 3: "彩虹", 4: "频闪"}
            effect_name = effect_names.get(effect, f"未知({effect})")
            await self.client.write_gatt_char(BLE_LED_EFFECT_CHAR_UUID,
                                               bytes([effect, speed]))
            print(f"LED特效已设置: {effect_name}, 速度: {speed}")
            return True
        except BleakError as e:
            print(f"LED特效设置失败: {e}")
            return False

    async def ota_update(self, fw_path, chunk_size=244):
        if not os.path.exists(fw_path):
            print(f"固件文件不存在: {fw_path}")
            return False

        print(f"\n准备升级固件: {fw_path}")
        with open(fw_path, 'rb') as f:
            fw_data = f.read()

        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version = 0x01000000

        print(f"固件大小: {fw_size} bytes")
        print(f"固件CRC: 0x{fw_crc:08X}")
        print(f"固件版本: v1.0.0")

        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            return False

        try:
            total_packages = (fw_size + chunk_size - 1) // chunk_size
            sent_bytes = 0
            last_print = 0
            start_time = time.time()

            print()
            for i in range(0, fw_size, chunk_size):
                chunk = fw_data[i:i+chunk_size]
                await self.ota_send_fw_data(chunk)
                sent_bytes += len(chunk)

                if sent_bytes - last_print >= chunk_size * 2 or i + len(chunk) >= fw_size:
                    self._print_progress(sent_bytes, fw_size, start_time, self.ota_status)
                    last_print = sent_bytes

            self._print_progress(sent_bytes, fw_size, start_time, self.ota_status)
            print()

            if not await self.ota_verify():
                await self.ota_abort()
                return False

            await self.ota_apply()
            return True

        except Exception as e:
            print(f"\nOTA升级失败: {e}")
            await self.ota_abort()
            return False

async def main():
    parser = argparse.ArgumentParser(description='ESP32 BLE Device Manager Client')
    parser.add_argument('-d', '--device', help='设备名称或地址')
    parser.add_argument('-c', '--command', required=True,
                        choices=['info', 'memory', 'cpu', 'flash', 'partition', 'restart', 'ota', 'wifi-status', 'wifi-connect', 'wifi-forget', 'ntp-sync', 'led-on', 'led-off', 'led-color', 'led-status', 'led-effect'],
                        help='执行的命令')
    parser.add_argument('-f', '--firmware', help='OTA固件文件路径')
    parser.add_argument('--ssid', help='WiFi SSID')
    parser.add_argument('--password', help='WiFi密码')
    parser.add_argument('--color', help='LED颜色（AABBCC格式，如FF0000=红色）')
    parser.add_argument('--effect', choices=['none', 'breath', 'blink', 'rainbow', 'strobe'],
                        help='LED特效类型')
    parser.add_argument('--speed', type=int, default=50, help='LED特效速度（1-255，默认50）')

    args = parser.parse_args()

    client = BLEDeviceManagerClient(device_name=args.device)
    connected = False

    try:
        connected = await client.connect()
        if not connected:
            return

        if args.command == 'info':
            info = await client.read_device_info()
            if info:
                print("\n设备信息:")
                print(info)

        elif args.command == 'memory':
            info = await client.read_memory_info()
            if info:
                print("\n内存信息:")
                print(info)

        elif args.command == 'cpu':
            info = await client.read_cpu_info()
            if info:
                print("\nCPU信息:")
                print(info)

        elif args.command == 'flash':
            info = await client.read_flash_info()
            if info:
                print("\nFlash信息:")
                print(info)

        elif args.command == 'partition':
            partitions = await client.read_all_partitions()
            if partitions:
                print(f"\n分区列表 ({len(partitions)} 个):")
                for i, part in enumerate(partitions):
                    print(f"\n--- 分区 {i} ---")
                    print(part)

        elif args.command == 'restart':
            await client.restart_device()

        elif args.command == 'ota':
            if not args.firmware:
                print("请指定固件文件路径: -f <firmware.bin>")
                return
            await client.ota_update(args.firmware)

        elif args.command == 'wifi-status':
            status = await client.wifi_status()
            if status:
                print("\nWiFi状态:")
                print(status)

        elif args.command == 'wifi-connect':
            if not args.ssid:
                print("请指定WiFi SSID: --ssid <SSID>")
                return
            await client.wifi_connect(args.ssid, args.password or "")

        elif args.command == 'wifi-forget':
            await client.wifi_forget()

        elif args.command == 'ntp-sync':
            await client.wifi_ntp_sync()

        elif args.command == 'led-on':
            await client.led_on()

        elif args.command == 'led-off':
            await client.led_off()

        elif args.command == 'led-color':
            if not args.color:
                print("请指定LED颜色: --color AABBCC（如FF0000=红色）")
                return
            await client.led_set_color(args.color)

        elif args.command == 'led-status':
            await client.led_status()

        elif args.command == 'led-effect':
            if not args.effect:
                print("请指定LED特效: --effect <类型>")
                print("可用特效: none(无), breath(呼吸灯), blink(闪烁), rainbow(彩虹), strobe(频闪)")
                return
            effect_map = {'none': 0, 'breath': 1, 'blink': 2, 'rainbow': 3, 'strobe': 4}
            effect_id = effect_map[args.effect]
            await client.led_set_effect(effect_id, args.speed)

    except KeyboardInterrupt:
        print("\n\n用户中止操作")
        if connected and args.command == 'ota':
            print("正在中止OTA...")
            await client.ota_abort()

    finally:
        try:
            if connected:
                await client.disconnect()
        except Exception:
            pass

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n程序已中止")
