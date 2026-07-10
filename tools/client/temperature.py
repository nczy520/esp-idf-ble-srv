"""
温度传感器功能模块
温度数据嵌入在设备信息(DeviceInfo)结构体中，通过0xFFE2特征值读取

结构体布局(packed, 123 bytes):
  [0:32]    chip_name
  [32:48]   chip_model
  [48:64]   flash_size
  [64:82]   mac_address
  [82:114]  version
  [114:118] cpu_freq_mhz (uint32)
  [118:122] temperature_celsius (float)
  [122:123] temp_sensor_supported (uint8)
"""

import struct

try:
    from bleak import BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from .constants import BLE_DM_INFO_CHAR_UUID

DEVICE_INFO_LEN = 123
TEMP_OFFSET = 118
TEMP_SUPPORTED_OFFSET = 122


class TemperatureMixin:
    """温度传感器功能混合类，需与BLEDeviceManagerClient一起使用"""

    async def read_temperature(self):
        """读取当前温度（从设备信息结构体中提取）"""
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            data = await self.client.read_gatt_char(BLE_DM_INFO_CHAR_UUID)

            if len(data) < DEVICE_INFO_LEN:
                print(f"设备信息数据长度不足（{len(data)}字节，需{DEVICE_INFO_LEN}字节），请升级固件")
                return None

            temp = struct.unpack('<f', data[TEMP_OFFSET:TEMP_OFFSET + 4])[0]
            supported = struct.unpack('<B', data[TEMP_SUPPORTED_OFFSET:TEMP_SUPPORTED_OFFSET + 1])[0]
            if not supported:
                return -999.0
            return temp
        except BleakError as e:
            print(f"读取温度失败: {e}")
            return None
