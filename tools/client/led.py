"""
LED控制功能模块
包含LED开关、颜色设置和特效控制
"""

import asyncio

try:
    from bleak import BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from .constants import (
    BLE_LED_CTRL_CHAR_UUID,
    BLE_LED_COLOR_CHAR_UUID,
    BLE_LED_EFFECT_CHAR_UUID,
    BLE_LED_CTRL_ON,
    BLE_LED_CTRL_OFF
)


class LEDMixin:
    """LED功能混合类，需与BLEDeviceManagerClient一起使用"""

    async def led_on(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]))
            print("LED已打开")
            return True
        except BleakError as e:
            print(f"打开LED失败: {e}")
            return False

    async def led_off(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]))
            print("LED已关闭")
            return False
        except BleakError as e:
            print(f"关闭LED失败: {e}")
            return False

    async def led_set_color(self, color_hex):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            color_hex = color_hex.strip('#').strip()
            if len(color_hex) != 6:
                print("颜色格式错误，请使用AABBCC格式（如FF0000=红色）")
                return False
            r = int(color_hex[0:2], 16)
            g = int(color_hex[2:4], 16)
            b = int(color_hex[4:6], 16)
            await self.client.write_gatt_char(BLE_LED_COLOR_CHAR_UUID, bytes([r, g, b]))
            print(f"LED颜色已设置: #{color_hex.upper()}")
            return True
        except BleakError as e:
            print(f"设置LED颜色失败: {e}")
            return False
        except ValueError:
            print("颜色格式错误，请使用AABBCC格式（如FF0000=红色）")
            return False

    async def led_status(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_LED_CTRL_CHAR_UUID)
            if data:
                on = data[0] == 1
                print(f"LED状态: {'开' if on else '关'}")
                return on
            return None
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