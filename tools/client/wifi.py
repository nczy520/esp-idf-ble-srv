"""
WiFi功能模块
包含WiFi连接管理和状态查询

WiFi配置数据格式（与固件端ble_wifi_config_t对应，packed）:
  ssid[33]     - SSID字符串，固定33字节，不足补零
  password[65] - 密码字符串，固定65字节，不足补零
  总计: 98字节
"""

import asyncio
import struct

try:
    from bleak import BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from .constants import (
    BLE_WIFI_STATUS_CHAR_UUID,
    BLE_WIFI_CONFIG_CHAR_UUID,
    BLE_WIFI_CTRL_CHAR_UUID,
    BLE_WIFI_CTRL_FORGET,
    BLE_WIFI_CTRL_NTP_SYNC
)
from .models import WiFiStatus

WIFI_SSID_LEN = 33
WIFI_PASS_LEN = 65
WIFI_CONFIG_SIZE = WIFI_SSID_LEN + WIFI_PASS_LEN  # 98


class WiFiMixin:
    """WiFi功能混合类，需与BLEDeviceManagerClient一起使用"""

    async def wifi_connect(self, ssid, password):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            ssid_bytes = ssid.encode('utf-8')[:WIFI_SSID_LEN - 1]
            pass_bytes = password.encode('utf-8')[:WIFI_PASS_LEN - 1]
            ssid_padded = ssid_bytes.ljust(WIFI_SSID_LEN, b'\x00')
            pass_padded = pass_bytes.ljust(WIFI_PASS_LEN, b'\x00')
            cmd_data = ssid_padded + pass_padded
            await self.client.write_gatt_char(BLE_WIFI_CONFIG_CHAR_UUID, cmd_data)
            print(f"WiFi连接命令已发送 (SSID: {ssid})")
            return True
        except BleakError as e:
            print(f"发送WiFi连接命令失败: {e}")
            return False

    async def wifi_disconnect(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            ssid_empty = b'\x00' * WIFI_SSID_LEN
            pass_empty = b'\x00' * WIFI_PASS_LEN
            await self.client.write_gatt_char(BLE_WIFI_CONFIG_CHAR_UUID, ssid_empty + pass_empty)
            print("WiFi断开命令已发送")
            return True
        except BleakError as e:
            print(f"发送WiFi断开命令失败: {e}")
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
            print("WiFi配置已清除")
            return True
        except BleakError as e:
            print(f"清除WiFi配置失败: {e}")
            return False

    async def wifi_ntp_sync(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]))
            print("NTP时间同步命令已发送")
            return True
        except BleakError as e:
            print(f"NTP同步失败: {e}")
            return False