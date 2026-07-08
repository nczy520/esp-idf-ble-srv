"""
BLE设备管理器完整客户端
组合所有功能模块：设备信息、OTA、WiFi、LED
"""

from .client import BLEDeviceManagerClient
from .ota import OTAMixin
from .wifi import WiFiMixin
from .led import LEDMixin
from .models import (
    DeviceInfo, MemoryInfo, CPUInfo, FlashInfo, PartitionInfo, OTAStatus, WiFiStatus,
    OTAState, OTAError
)


class BLEDeviceClient(BLEDeviceManagerClient, OTAMixin, WiFiMixin, LEDMixin):
    """
    完整的BLE设备客户端，集成所有功能
    """

    def __init__(self, device_name=None, address=None):
        super().__init__(device_name=device_name, address=address)


__all__ = [
    'BLEDeviceClient',
    'BLEDeviceManagerClient',
    'OTAMixin',
    'WiFiMixin',
    'LEDMixin',
    'DeviceInfo',
    'MemoryInfo',
    'CPUInfo',
    'FlashInfo',
    'PartitionInfo',
    'OTAStatus',
    'WiFiStatus',
    'OTAState',
    'OTAError',
]