"""
BLE服务UUID和命令常量定义
"""

# Device Manager 服务UUID
BLE_DM_SVC_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
BLE_DM_CMD_CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
BLE_DM_INFO_CHAR_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb"
BLE_DM_MEMORY_CHAR_UUID = "0000ffe3-0000-1000-8000-00805f9b34fb"
BLE_DM_CPU_CHAR_UUID = "0000ffe4-0000-1000-8000-00805f9b34fb"
BLE_DM_FLASH_CHAR_UUID = "0000ffe5-0000-1000-8000-00805f9b34fb"
BLE_DM_PARTITION_CHAR_UUID = "0000ffe7-0000-1000-8000-00805f9b34fb"
BLE_DM_RESTART_CHAR_UUID = "0000ffe6-0000-1000-8000-00805f9b34fb"
BLE_DM_AUTH_CHAR_UUID = "0000ffe8-0000-1000-8000-00805f9b34fb"
BLE_DM_LOG_CHAR_UUID = "0000ffe9-0000-1000-8000-00805f9b34fb"
BLE_DM_CUSTOM_CMD_CHAR_UUID = "0000ffea-0000-1000-8000-00805f9b34fb"
BLE_DM_LOG_HTTP_CTRL_CHAR_UUID = "0000ffee-0000-1000-8000-00805f9b34fb"
BLE_DM_LOG_STORAGE_CHAR_UUID = "0000ffef-0000-1000-8000-00805f9b34fb"

# OTA 服务UUID
BLE_OTA_SVC_UUID = "0000ffd0-0000-1000-8000-00805f9b34fb"
BLE_OTA_BT_CMD_CHAR_UUID = "0000ffd1-0000-1000-8000-00805f9b34fb"
BLE_OTA_BT_FW_DATA_CHAR_UUID = "0000ffd2-0000-1000-8000-00805f9b34fb"
BLE_OTA_STATUS_CHAR_UUID = "0000ffd3-0000-1000-8000-00805f9b34fb"
BLE_OTA_URL_CHAR_UUID = "0000ffd4-0000-1000-8000-00805f9b34fb"

# WiFi 服务UUID
BLE_WIFI_SVC_UUID = "0000ffc0-0000-1000-8000-00805f9b34fb"
BLE_WIFI_CONFIG_CHAR_UUID = "0000ffc1-0000-1000-8000-00805f9b34fb"
BLE_WIFI_STATUS_CHAR_UUID = "0000ffc2-0000-1000-8000-00805f9b34fb"
BLE_WIFI_CTRL_CHAR_UUID = "0000ffc3-0000-1000-8000-00805f9b34fb"

# LED 服务UUID
BLE_LED_SVC_UUID = "0000ffb0-0000-1000-8000-00805f9b34fb"
BLE_LED_CTRL_CHAR_UUID = "0000ffb1-0000-1000-8000-00805f9b34fb"
BLE_LED_COLOR_CHAR_UUID = "0000ffb2-0000-1000-8000-00805f9b34fb"
BLE_LED_EFFECT_CHAR_UUID = "0000ffb3-0000-1000-8000-00805f9b34fb"

# Device Manager 命令
BLE_DM_CMD_GET_INFO = 0x01
BLE_DM_CMD_GET_MEMORY = 0x02
BLE_DM_CMD_GET_CPU = 0x03
BLE_DM_CMD_GET_FLASH = 0x04
BLE_DM_CMD_RESTART = 0x05

# OTA 蓝牙命令
BLE_OTA_BT_CMD_START = 0x01
BLE_OTA_BT_CMD_ABORT = 0x02
BLE_OTA_BT_CMD_VERIFY = 0x03
BLE_OTA_BT_CMD_APPLY = 0x04

# OTA URL 命令
BLE_OTA_URL_CMD_START_URL = 0x01
BLE_OTA_URL_CMD_START_DEFAULT = 0x02
BLE_OTA_URL_CMD_ABORT = 0x03

# WiFi 控制命令
BLE_WIFI_CTRL_FORGET = 0x01
BLE_WIFI_CTRL_NTP_SYNC = 0x02

# LED 控制命令
BLE_LED_CTRL_OFF = 0x00
BLE_LED_CTRL_ON = 0x01

# 日志HTTP控制命令
BLE_LOG_HTTP_CMD_STOP = 0x00
BLE_LOG_HTTP_CMD_START = 0x01
BLE_LOG_HTTP_CMD_STATUS = 0x02
BLE_LOG_HTTP_CMD_WRITE_LOG = 0x03
BLE_LOG_HTTP_CMD_FORMAT_LITTLEFS = 0x05
BLE_LOG_HTTP_CMD_SET_LEVEL = 0x06

# LED 特效
BLE_LED_EFFECT_NONE = 0x00
BLE_LED_EFFECT_BREATH = 0x01
BLE_LED_EFFECT_BLINK = 0x02
BLE_LED_EFFECT_RAINBOW = 0x03
BLE_LED_EFFECT_STROBE = 0x04

ESP_APP_DESC_MAGIC = b'\x32\x54\xCD\xAB'

def parse_esp_fw_version(fw_data: bytes) -> tuple:
    """
    从ESP32固件镜像中解析版本号
    返回: (version_uint32, version_str)，如果解析失败返回默认值(0x00010000, "v1.0.0")
    version_uint32编码: (major << 16) | (minor << 8) | patch
    """
    default_ver = 0x00010000
    default_str = "v1.0.0"

    try:
        search_end = min(len(fw_data), 0x2000)
        magic_pos = fw_data.find(ESP_APP_DESC_MAGIC, 0, search_end)
        if magic_pos < 0:
            return default_ver, default_str

        ver_offset = magic_pos + 4 + 4 + 8
        if ver_offset + 32 > len(fw_data):
            return default_ver, default_str

        ver_bytes = fw_data[ver_offset:ver_offset + 32]
        null_pos = ver_bytes.find(b'\x00')
        if null_pos >= 0:
            ver_bytes = ver_bytes[:null_pos]
        ver_str = ver_bytes.decode('ascii', errors='ignore').strip()
        if not ver_str:
            return default_ver, default_str

        clean = ver_str.lstrip('vV ')
        parts = clean.split('.')
        major = min(255, int(parts[0]) if len(parts) > 0 and parts[0].isdigit() else 1)
        minor = min(255, int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0)
        patch = min(255, int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 0)

        ver_uint = (major << 16) | (minor << 8) | patch
        ver_out = f"v{major}.{minor}.{patch}"
        return ver_uint, ver_out
    except Exception:
        return default_ver, default_str