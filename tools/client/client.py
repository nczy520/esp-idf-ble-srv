"""
BLE设备管理器核心客户端
提供基本的BLE连接、设备信息查询功能
"""

import asyncio
import time

try:
    from bleak import BleakClient, BleakScanner, BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from .constants import (
    BLE_DM_INFO_CHAR_UUID,
    BLE_DM_MEMORY_CHAR_UUID,
    BLE_DM_CPU_CHAR_UUID,
    BLE_DM_FLASH_CHAR_UUID,
    BLE_DM_PARTITION_CHAR_UUID,
    BLE_DM_RESTART_CHAR_UUID,
    BLE_DM_CMD_RESTART
)
from .models import DeviceInfo, MemoryInfo, CPUInfo, FlashInfo, PartitionInfo


class BLEDeviceManagerClient:
    def __init__(self, device_name=None, address=None):
        self.device_name = device_name
        self.address = address
        self.client = None
        self.ota_status = None
        self.ota_progress_callback = None

    async def scan(self, timeout=3, select=False):
        print(f"扫描 BLE 设备（超时 {timeout}s）...")
        devices = await BleakScanner.discover(timeout=timeout)
        matched = []
        for d in devices:
            if d.name and (self.device_name is None or d.name.startswith(self.device_name)):
                matched.append(d)

        if not matched:
            print("未发现匹配的 BLE 设备")
            return None

        print(f"\n发现 {len(matched)} 个匹配设备:")
        for i, d in enumerate(matched):
            rssi_str = ""
            if hasattr(d, 'rssi') and d.rssi is not None:
                rssi_str = f"  RSSI: {d.rssi} dBm"
            print(f"  {i + 1}. {d.name} ({d.address}){rssi_str}")

        if not select:
            if len(matched) == 1:
                print(f"\n仅发现一个设备，自动选择: {matched[0].name} ({matched[0].address})")
                return matched[0]
            return matched

        if len(matched) == 1:
            print(f"\n仅发现一个设备，自动选择: {matched[0].name} ({matched[0].address})")
            return matched[0]

        while True:
            try:
                choice = input(f"\n请选择设备 [1-{len(matched)}] (q 退出): ").strip()
                if choice.lower() == 'q':
                    return None
                idx = int(choice) - 1
                if 0 <= idx < len(matched):
                    return matched[idx]
                print(f"无效选择，请输入 1-{len(matched)} 之间的数字")
            except ValueError:
                print("无效输入，请输入数字或 q 退出")
            except (EOFError, KeyboardInterrupt):
                print()
                return None

    async def connect(self, device=None):
        if device is None:
            device = await self.scan(select=True)
            if device is None:
                return False

        self.address = device.address
        print(f"\n连接设备: {device.name} ({device.address})")
        try:
            self.client = BleakClient(device, timeout=15, use_cached=False)
            await self.client.connect()
            mtu = self.client.mtu_size
            print(f"连接成功 (MTU={mtu})")
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

    def _print_progress(self, written_bytes, fw_size, start_time, label="写入", sent_bytes=None):
        pct = min(100, int(written_bytes * 100 / fw_size))
        bar_len = 30
        filled = int(bar_len * pct / 100)
        bar = '=' * filled + '>' + ' ' * (bar_len - filled - 1) if pct < 100 else '=' * bar_len

        written_str = f"{written_bytes / 1024:.1f}KB" if written_bytes >= 1024 else f"{written_bytes}B"
        total_str = f"{fw_size / 1024:.1f}KB" if fw_size >= 1024 else f"{fw_size}B"

        elapsed = time.time() - start_time
        if elapsed > 0 and written_bytes > 0:
            speed = written_bytes / elapsed
            speed_str = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            remain = fw_size - written_bytes
            eta = remain / speed if speed > 0 else 0
            eta_str = f"{int(eta)}s" if eta < 60 else f"{int(eta / 60)}m{int(eta % 60)}s"
        else:
            speed_str = "0B/s"
            eta_str = "--"

        if sent_bytes is not None:
            sent_pct = min(100, int(sent_bytes * 100 / fw_size))
            line = f"\r[{bar}] {pct}% {speed_str} ETA:{eta_str} | 发送:{sent_pct}% | {label}:{written_str}/{total_str}"
        else:
            line = f"\r[{bar}] {pct}% {speed_str} ETA:{eta_str} | {label}:{written_str}/{total_str}"
        print(line.ljust(100), end='', flush=True)