"""
WiFi功能模块
包含WiFi连接管理和状态查询
"""

import asyncio

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
    BLE_WIFI_CMD_CONNECT,
    BLE_WIFI_CMD_DISCONNECT,
    BLE_WIFI_CTRL_FORGET,
    BLE_WIFI_CTRL_NTP_SYNC
)
from .models import WiFiStatus


class WiFiMixin:
    """WiFi功能混合类，需与BLEDeviceManagerClient一起使用"""

    async def wifi_connect(self, ssid, password):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            ssid_bytes = ssid.encode('utf-8')
            pass_bytes = password.encode('utf-8')
            cmd_data = bytes([BLE_WIFI_CMD_CONNECT, len(ssid_bytes)]) + ssid_bytes + \
                       bytes([len(pass_bytes)]) + pass_bytes
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
            await self.client.write_gatt_char(BLE_WIFI_CONFIG_CHAR_UUID, bytes([BLE_WIFI_CMD_DISCONNECT]))
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