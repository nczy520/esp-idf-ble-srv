"""
BLE设备管理器核心客户端
整合所有功能：设备信息、OTA（蓝牙/URL）、WiFi、LED、温度传感器
"""

import asyncio
import struct
import zlib
import os
import time
from typing import Optional, List, Tuple, Any

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
    BLE_DM_AUTH_CHAR_UUID,
    BLE_DM_LOG_CHAR_UUID,
    BLE_DM_CMD_RESTART,
    BLE_LED_CTRL_CHAR_UUID,
    BLE_LED_COLOR_CHAR_UUID,
    BLE_LED_EFFECT_CHAR_UUID,
    BLE_LED_CTRL_ON,
    BLE_LED_CTRL_OFF,
    BLE_WIFI_STATUS_CHAR_UUID,
    BLE_WIFI_CONFIG_CHAR_UUID,
    BLE_WIFI_CTRL_CHAR_UUID,
    BLE_WIFI_CTRL_FORGET,
    BLE_WIFI_CTRL_NTP_SYNC,
    BLE_OTA_BT_CMD_CHAR_UUID,
    BLE_OTA_BT_FW_DATA_CHAR_UUID,
    BLE_OTA_STATUS_CHAR_UUID,
    BLE_OTA_URL_CHAR_UUID,
    BLE_OTA_BT_CMD_START,
    BLE_OTA_BT_CMD_VERIFY,
    BLE_OTA_BT_CMD_APPLY,
    BLE_OTA_BT_CMD_ABORT,
    BLE_OTA_URL_CMD_START_URL,
    BLE_OTA_URL_CMD_START_DEFAULT,
    BLE_OTA_URL_CMD_ABORT,
    parse_esp_fw_version,
)
from .models import (
    DeviceInfo, MemoryInfo, CPUInfo, FlashInfo, PartitionInfo,
    OTAStatus, OTAState, OTAError, WiFiStatus,
)

ERROR_NAMES = {
    OTAError.NONE: "无错误",
    OTAError.INVALID_CMD: "无效命令",
    OTAError.INVALID_SIZE: "固件大小无效",
    OTAError.FLASH_WRITE: "Flash写入失败",
    OTAError.NO_PARTITION: "无可用OTA分区",
    OTAError.VERIFY_FAILED: "固件校验失败",
    OTAError.INTERNAL: "设备内部错误",
    OTAError.BUSY: "设备忙",
    OTAError.NO_NETWORK: "网络未连接",
    OTAError.ABORTED: "用户中止",
    OTAError.DISCONNECTED: "连接断开",
    OTAError.VERSION_DOWNGRADE: "远程固件版本更旧",
    OTAError.VERSION_SAME: "固件版本相同",
    OTAError.CRC_MISMATCH: "固件CRC校验失败",
}

STATE_NAMES = {
    OTAState.IDLE: "空闲",
    OTAState.CHECKING: "检查固件",
    OTAState.CHECK_OK: "检查通过",
    OTAState.CHECK_FAIL: "无需更新",
    OTAState.RECEIVING: "接收固件中",
    OTAState.VERIFYING: "校验中",
    OTAState.VERIFY_OK: "校验通过",
    OTAState.VERIFY_FAIL: "校验失败",
    OTAState.APPLYING: "应用中",
    OTAState.APPLY_OK: "应用成功",
    OTAState.APPLY_FAIL: "应用失败",
    OTAState.ABORTING: "正在中止",
    OTAState.ABORTED: "已中止",
    OTAState.ERROR: "错误",
}


class BLEDeviceClient:
    def __init__(self, device_name=None, address=None, pin=None):
        self.device_name = device_name
        self.address = address
        self.pin = pin
        self.client = None
        self.is_connected = False
        self.ota_status = None
        self._ota_active = False
        self._ota_ack_event = None
        self._ota_ack_bytes = 0
        self._ota_notify_started = False
        self._last_ota_state = None
        self._connect_gen = 0
        self._disconnect_event = None
        self._log_notify_started = False

    def _on_log_notify(self, sender, data):
        try:
            msg = bytes(data).decode('utf-8', errors='replace')
        except Exception:
            msg = repr(data)
        if msg:
            print(f"  [设备日志] {msg}".ljust(120), flush=True)

    def _ota_status_handler(self, sender, data):
        try:
            status = OTAStatus(data)
            self.ota_status = status
            state_changed = self._last_ota_state != status.state
            self._last_ota_state = status.state

            if state_changed:
                name = STATE_NAMES.get(status.state, f"状态{status.state}")
                if status.state in (OTAState.ERROR, OTAState.VERIFY_FAIL, OTAState.APPLY_FAIL, OTAState.ABORTED):
                    err_name = ERROR_NAMES.get(status.error_code, f"错误码{status.error_code}")
                    print(f"\r  [OTA] {name} ({err_name})" + " " * 40)
                elif status.state not in (OTAState.RECEIVING,):
                    print(f"\r  [OTA] {name}" + " " * 40)

            is_terminal = status.state in (
                OTAState.ABORTED, OTAState.ERROR, OTAState.VERIFY_FAIL,
                OTAState.APPLY_FAIL, OTAState.CHECK_FAIL
            )

            if self._ota_ack_event and status.state == OTAState.RECEIVING:
                self._ota_ack_bytes = status.bytes_written
                self._ota_ack_event.set()

            if is_terminal or status.state == OTAState.APPLY_OK:
                self._ota_active = False
                if self._ota_ack_event:
                    self._ota_ack_event.set()

        except Exception:
            pass

    @staticmethod
    def _print_progress(written, total, start_time, label="进度"):
        pct = min(100, int(written * 100 / total)) if total > 0 else 0
        bar_len = 30
        filled = int(bar_len * pct / 100)
        if pct >= 100:
            bar = '=' * bar_len
        else:
            bar = '=' * filled + '>' + ' ' * (bar_len - filled - 1)
        w_str = f"{written/1024:.1f}KB" if written >= 1024 else f"{written}B"
        t_str = f"{total/1024:.1f}KB" if total >= 1024 else f"{total}B"
        elapsed = time.time() - start_time
        if elapsed > 0 and written > 0:
            speed = written / elapsed
            s_str = f"{speed/1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            remain = total - written
            eta = remain / speed if speed > 0 else 0
            if eta >= 60:
                eta_str = f"{int(eta//60)}m{int(eta%60)}s"
            elif eta >= 1:
                eta_str = f"{int(eta)}s"
            else:
                eta_str = "--"
        else:
            s_str = "0B/s"
            eta_str = "--"
        line = f"\r  [{bar}] {pct:3d}% {s_str:>9s} ETA:{eta_str:>5s} | {label}: {w_str}/{t_str}"
        print(line.ljust(100), end='', flush=True)

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
            rssi = getattr(d, 'rssi', None)
            rssi_str = f"  RSSI: {rssi} dBm" if rssi is not None else ""
            print(f"  {i+1}. {d.name} ({d.address}){rssi_str}")
        if not select:
            if len(matched) == 1:
                print(f"\n自动选择: {matched[0].name} ({matched[0].address})")
                return matched[0]
            return matched
        if len(matched) == 1:
            print(f"\n自动选择: {matched[0].name} ({matched[0].address})")
            return matched[0]
        while True:
            try:
                choice = input(f"\n请选择设备 [1-{len(matched)}] (q 退出): ").strip()
                if choice.lower() == 'q':
                    return None
                idx = int(choice) - 1
                if 0 <= idx < len(matched):
                    return matched[idx]
                print(f"无效选择，请输入 1-{len(matched)}")
            except ValueError:
                print("无效输入，请输入数字或 q")
            except (EOFError, KeyboardInterrupt):
                print()
                return None

    async def connect(self, device=None):
        if device is None:
            device = await self.scan(select=True)
            if device is None:
                return False
        if self.client:
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None
            await asyncio.sleep(0.3)
        self._connect_gen += 1
        gen = self._connect_gen
        self._disconnect_event = asyncio.Event()
        self.address = device.address
        self.device_name = device.name
        print(f"\n连接设备: {device.name} ({device.address})")
        try:
            def on_disconnect(c, g=gen):
                if g != self._connect_gen:
                    return
                self._on_disconnect(c)
            self.client = BleakClient(device, timeout=15, use_cached=False, disconnected_callback=on_disconnect)
            await self.client.connect()
            self.is_connected = True
            mtu = self.client.mtu_size if hasattr(self.client, 'mtu_size') else 247
            print(f"连接成功 (MTU={mtu})")
            if self.pin:
                if not await self.authenticate(self.pin):
                    print("PIN认证失败，断开连接")
                    await self.disconnect()
                    return False
            await self._subscribe_log_notify()
            return True
        except BleakError as e:
            print(f"连接失败: {e}")
            self.is_connected = False
            if self._connect_gen == gen:
                self.client = None
            return False

    async def authenticate(self, pin):
        """通过写入PIN码进行GATT层认证"""
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            pin_bytes = pin.encode('utf-8')[:16]
            await self.client.write_gatt_char(BLE_DM_AUTH_CHAR_UUID, pin_bytes, response=True)
            data = await self.client.read_gatt_char(BLE_DM_AUTH_CHAR_UUID)
            if data and data[0] == 1:
                print("PIN认证成功")
                return True
            else:
                print("PIN认证失败: 设备返回未认证状态")
                return False
        except Exception as e:
            print(f"PIN认证失败: {e}")
            return False

    def _on_disconnect(self, client):
        self.is_connected = False
        self._ota_notify_started = False
        self._log_notify_started = False
        self._ota_active = False
        if self._ota_ack_event:
            self._ota_ack_event.set()
        if self._disconnect_event:
            self._disconnect_event.set()
        print("\n设备已断开连接")

    async def _subscribe_log_notify(self):
        if not self.client or not self.client.is_connected:
            return False
        if self._log_notify_started:
            return True
        try:
            await self.client.start_notify(BLE_DM_LOG_CHAR_UUID, self._on_log_notify)
            self._log_notify_started = True
            print("设备日志订阅已开启")
            return True
        except Exception as e:
            print(f"订阅设备日志失败: {e}")
            return False

    async def _unsubscribe_log_notify(self):
        if self._log_notify_started and self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(BLE_DM_LOG_CHAR_UUID)
            except Exception:
                pass
        self._log_notify_started = False

    async def disconnect(self):
        await self._unsubscribe_log_notify()
        if self._ota_notify_started and self.client:
            try:
                await self.client.stop_notify(BLE_OTA_STATUS_CHAR_UUID)
            except Exception:
                pass
        self._ota_notify_started = False
        self._ota_active = False
        if self._ota_ack_event:
            self._ota_ack_event.set()
        old_client = self.client
        disc_event = self._disconnect_event
        if old_client:
            self.client = None
            self._connect_gen += 1
            try:
                await old_client.disconnect()
            except Exception:
                pass
            if disc_event and self.is_connected:
                try:
                    await asyncio.wait_for(disc_event.wait(), timeout=2.0)
                except Exception:
                    pass
            try:
                await asyncio.sleep(0.3)
            except Exception:
                pass
        self.is_connected = False
        self.address = None
        self.device_name = None
        print("已断开连接")

    async def _read_gatt(self, uuid, name="特征值"):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return None
        try:
            return await self.client.read_gatt_char(uuid)
        except BleakError as e:
            print(f"读取{name}失败: {e}")
            return None

    async def _write_gatt(self, uuid, data, response=True, name="特征值"):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(uuid, data, response=response)
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if not response and any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset"]):
                return True
            print(f"写入{name}失败: {e}")
            return False

    async def read_device_info(self):
        data = await self._read_gatt(BLE_DM_INFO_CHAR_UUID, "设备信息")
        return DeviceInfo(data) if data else None

    async def read_memory_info(self):
        data = await self._read_gatt(BLE_DM_MEMORY_CHAR_UUID, "内存信息")
        return MemoryInfo(data) if data else None

    async def read_cpu_info(self):
        data = await self._read_gatt(BLE_DM_CPU_CHAR_UUID, "CPU信息")
        return CPUInfo(data) if data else None

    async def read_flash_info(self):
        data = await self._read_gatt(BLE_DM_FLASH_CHAR_UUID, "Flash信息")
        return FlashInfo(data) if data else None

    async def read_partition_info(self, index=0):
        if not await self._write_gatt(BLE_DM_PARTITION_CHAR_UUID, bytes([index]), name="分区索引"):
            return None
        await asyncio.sleep(0.1)
        data = await self._read_gatt(BLE_DM_PARTITION_CHAR_UUID, "分区信息")
        return PartitionInfo(data) if data else None

    async def read_all_partitions(self):
        flash_info = await self.read_flash_info()
        if not flash_info:
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
            await self.client.write_gatt_char(BLE_DM_RESTART_CHAR_UUID, bytes([BLE_DM_CMD_RESTART]), response=False)
            print("重启命令已发送，设备即将重启")
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset"]):
                print("重启命令已发送（设备已断开）")
                return True
            print(f"发送重启命令失败: {e}")
            return False

    async def read_temperature(self):
        info = await self.read_device_info()
        if not info:
            return None
        if not info.temp_sensor_supported:
            return -999.0
        return info.temperature_celsius

    async def led_on(self):
        if await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]), name="LED"):
            print("LED已打开")
            return True
        return False

    async def led_off(self):
        if await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]), name="LED"):
            print("LED已关闭")
            return True
        return False

    async def led_set_color(self, color_hex):
        color_hex = color_hex.strip('#').strip()
        if len(color_hex) != 6:
            print("颜色格式错误，请使用AABBCC格式（如FF0000=红色）")
            return False
        try:
            r = int(color_hex[0:2], 16)
            g = int(color_hex[2:4], 16)
            b = int(color_hex[4:6], 16)
        except ValueError:
            print("颜色格式错误，请使用AABBCC格式（如FF0000=红色）")
            return False
        if await self._write_gatt(BLE_LED_COLOR_CHAR_UUID, bytes([r, g, b]), name="LED颜色"):
            print(f"LED颜色已设置: #{color_hex.upper()}")
            return True
        return False

    async def led_status(self):
        data = await self._read_gatt(BLE_LED_CTRL_CHAR_UUID, "LED状态")
        if data:
            on = data[0] == 1
            print(f"LED状态: {'开' if on else '关'}")
            return on
        return None

    async def led_set_effect(self, effect, speed=50):
        effect_names = {0: "无", 1: "呼吸灯", 2: "闪烁", 3: "彩虹", 4: "频闪"}
        if await self._write_gatt(BLE_LED_EFFECT_CHAR_UUID, bytes([effect, speed]), name="LED特效"):
            name = effect_names.get(effect, f"未知({effect})")
            print(f"LED特效已设置: {name}, 速度: {speed}")
            return True
        return False

    async def wifi_connect(self, ssid, password):
        ssid_bytes = ssid.encode('utf-8')[:32]
        pass_bytes = password.encode('utf-8')[:64]
        cmd = bytes([len(ssid_bytes)]) + ssid_bytes + bytes([len(pass_bytes)]) + pass_bytes
        if await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, cmd, name="WiFi配置"):
            print(f"WiFi连接命令已发送 (SSID: {ssid})")
            return True
        return False

    async def wifi_disconnect(self):
        cmd = b'\x00\x00'
        if await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, cmd, name="WiFi断开"):
            print("WiFi断开命令已发送")
            return True
        return False

    async def wifi_status(self):
        data = await self._read_gatt(BLE_WIFI_STATUS_CHAR_UUID, "WiFi状态")
        return WiFiStatus(data) if data else None

    async def wifi_forget(self):
        if await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_FORGET]), name="WiFi清除"):
            print("WiFi配置已清除")
            return True
        return False

    async def wifi_ntp_sync(self):
        if await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]), name="NTP同步"):
            print("NTP时间同步命令已发送")
            return True
        return False

    async def _start_ota_notify(self):
        if self._ota_notify_started:
            return True
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
            self._ota_notify_started = True
            return True
        except Exception as e:
            print(f"启动OTA通知失败: {e}")
            return False

    async def _stop_ota_notify(self):
        if self._ota_notify_started and self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(BLE_OTA_STATUS_CHAR_UUID)
            except Exception:
                pass
        self._ota_notify_started = False

    def _get_ota_error_msg(self):
        if not self.ota_status:
            return "未知错误"
        return ERROR_NAMES.get(self.ota_status.error_code, f"错误码{self.ota_status.error_code}")

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        if not await self._start_ota_notify():
            return False
        self._ota_ack_event = asyncio.Event()
        self._ota_ack_bytes = 0
        cmd = struct.pack('<BIIHHI', BLE_OTA_BT_CMD_START, fw_size, fw_crc, chunk_size, 0, fw_version)
        print(f"  OTA启动: size={fw_size}, crc=0x{fw_crc:08X}, chunk={chunk_size}")
        if not await self._write_gatt(BLE_OTA_BT_CMD_CHAR_UUID, cmd, name="OTA启动"):
            return False
        await asyncio.sleep(0.5)
        if self.ota_status and self.ota_status.state == OTAState.RECEIVING:
            print("  OTA会话已启动")
            return True
        if self.ota_status:
            print(f"  OTA启动失败: {STATE_NAMES.get(self.ota_status.state)} ({self._get_ota_error_msg()})")
        else:
            print("  OTA启动失败: 无响应")
        return False

    async def ota_send_fw_data(self, data, max_retries=3):
        for attempt in range(max_retries):
            try:
                await self.client.write_gatt_char(BLE_OTA_BT_FW_DATA_CHAR_UUID, data, response=False)
                return True
            except BleakError:
                if attempt < max_retries - 1:
                    await asyncio.sleep(0.05 * (attempt + 1))
        return False

    async def ota_verify(self):
        print("\n  固件发送完成，开始校验...")
        if not await self._write_gatt(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_VERIFY]), name="OTA校验"):
            return False
        for _ in range(30):
            await asyncio.sleep(0.5)
            if self.ota_status:
                if self.ota_status.state == OTAState.VERIFY_OK:
                    print("  OTA校验成功")
                    return True
                if self.ota_status.state in (OTAState.VERIFY_FAIL, OTAState.ERROR, OTAState.ABORTED):
                    print(f"  OTA校验失败: {self._get_ota_error_msg()}")
                    return False
        print("  OTA校验超时")
        return False

    async def ota_apply(self):
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_APPLY]), response=False)
            print("  OTA应用命令已发送，设备即将重启")
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "cancel", "abort"]):
                print("  OTA应用成功，设备正在重启")
                return True
            print(f"  OTA应用失败: {e}")
            return False

    async def ota_abort(self):
        if not self.client or not self.client.is_connected:
            return True
        try:
            try:
                await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_ABORT]), response=False)
            except Exception:
                pass
            try:
                await self.client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, bytes([BLE_OTA_URL_CMD_ABORT]), response=False)
            except Exception:
                pass
            print("  OTA中止命令已发送")
        except Exception:
            pass
        return True

    async def ota_update(self, fw_path):
        if not os.path.exists(fw_path):
            print(f"固件文件不存在: {fw_path}")
            return False
        with open(fw_path, 'rb') as f:
            fw_data = f.read()
        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version, fw_ver_str = parse_esp_fw_version(fw_data)
        mtu = self.client.mtu_size if self.client and hasattr(self.client, 'mtu_size') else 247
        attr_overhead = 3
        offset_header = 4
        chunk_size = max(20, mtu - attr_overhead - offset_header)
        window_packets = 12
        window_bytes = window_packets * chunk_size
        packet_interval = 0.003
        print(f"\n准备蓝牙OTA升级: {os.path.basename(fw_path)}")
        print(f"  固件大小: {fw_size} bytes ({fw_size/1024:.1f}KB)")
        print(f"  固件版本: {fw_ver_str}")
        print(f"  固件CRC: 0x{fw_crc:08X}")
        print(f"  MTU: {mtu}, 块大小: {chunk_size}, 窗口: {window_packets}包")

        self._ota_active = True
        self._last_ota_state = None
        self.ota_status = None

        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            self._ota_active = False
            await self._stop_ota_notify()
            return False

        start_time = time.time()
        last_pct = -1
        acked_offset = 0
        sent_offset = 0
        total_retries = 0
        max_retries = 50
        ack_timeout = 2.0

        try:
            while acked_offset < fw_size:
                if not self._ota_active:
                    if self.ota_status and self.ota_status.state in (
                        OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL,
                        OTAState.APPLY_FAIL, OTAState.CHECK_FAIL
                    ):
                        print(f"\n  OTA失败: {self._get_ota_error_msg()}")
                    else:
                        print("\n  OTA已中止")
                    return False

                if not self.client or not self.client.is_connected:
                    print("\n  连接已断开")
                    return False

                if self.ota_status and self.ota_status.state in (
                        OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL,
                        OTAState.APPLY_FAIL, OTAState.CHECK_FAIL):
                    print(f"\n  OTA失败: {self._get_ota_error_msg()}")
                    return False

                window_end = min(acked_offset + window_bytes, fw_size)
                while sent_offset < window_end:
                    chunk = fw_data[sent_offset:sent_offset + chunk_size]
                    pkt = struct.pack('<I', sent_offset) + chunk
                    ok = await self.ota_send_fw_data(pkt)
                    if not ok:
                        break
                    sent_offset += len(chunk)
                    await asyncio.sleep(packet_interval)

                if sent_offset >= fw_size and self._ota_ack_bytes >= fw_size:
                    break

                if sent_offset >= fw_size or sent_offset >= acked_offset + window_bytes:
                    self._ota_ack_event = asyncio.Event()
                    try:
                        await asyncio.wait_for(self._ota_ack_event.wait(), timeout=ack_timeout)
                    except asyncio.TimeoutError:
                        total_retries += 1
                        if total_retries > max_retries:
                            print(f"\n  等待ACK超时次数过多({max_retries}次)，传输失败")
                            await self.ota_abort()
                            return False
                        if total_retries <= 3 or total_retries % 10 == 0:
                            print(f"\n  等待ACK超时，从offset={acked_offset}重传 (retry={total_retries})")
                        sent_offset = acked_offset
                        await asyncio.sleep(0.05)
                        continue

                    dev_received = self._ota_ack_bytes
                    if dev_received > acked_offset:
                        acked_offset = dev_received
                        sent_offset = max(sent_offset, acked_offset)
                        total_retries = 0
                    elif dev_received < acked_offset:
                        acked_offset = dev_received
                        sent_offset = dev_received
                        total_retries += 1
                    else:
                        sent_offset = acked_offset
                        total_retries += 1
                        if total_retries > max_retries:
                            print(f"\n  重传次数过多({max_retries}次)，传输失败")
                            await self.ota_abort()
                            return False

                pct = int(acked_offset * 100 / fw_size)
                if pct != last_pct:
                    self._print_progress(acked_offset, fw_size, start_time, "写入")
                    last_pct = pct

            print()
            print("  固件数据发送完毕，等待设备确认全部数据...")
            wait_ok = False
            for _ in range(100):
                await asyncio.sleep(0.05)
                if not self.client or not self.client.is_connected or not self._ota_active:
                    break
                if self.ota_status and self.ota_status.bytes_written >= fw_size:
                    wait_ok = True
                    break
                if self.ota_status and self.ota_status.state in (
                        OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL,
                        OTAState.APPLY_FAIL, OTAState.CHECK_FAIL):
                    break
            if not wait_ok and self.ota_status:
                print(f"  等待接收确认超时，设备已接收 {self.ota_status.bytes_written}/{fw_size} bytes")
            else:
                print("  设备已确认接收全部数据")
            await asyncio.sleep(0.2)

            if not await self.ota_verify():
                await self.ota_abort()
                return False
            await self.ota_apply()
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "abort"]):
                ota_ok = (self.ota_status and self.ota_status.state
                          in (OTAState.APPLY_OK, OTAState.APPLYING, OTAState.VERIFY_OK))
                if ota_ok:
                    print("\n  设备已断开（OTA可能已完成，设备正在重启）")
                    return True
                print("\n  连接已断开，OTA失败")
                return False
            print(f"\n  OTA传输失败: {e}")
            await self.ota_abort()
            return False
        except Exception as e:
            print(f"\n  OTA异常: {e}")
            await self.ota_abort()
            return False
        finally:
            self._ota_active = False
            self._ota_ack_event = None
            await self._stop_ota_notify()

    async def ota_url(self, url=None):
        if not await self._start_ota_notify():
            return False
        self._ota_active = True
        self._last_ota_state = None
        self.ota_status = None

        try:
            if url:
                url_bytes = url.encode('utf-8')
                if len(url_bytes) > 256:
                    print("URL过长（最大256字节）")
                    return False
                cmd = bytes([BLE_OTA_URL_CMD_START_URL]) + url_bytes
                desc = url
            else:
                cmd = bytes([BLE_OTA_URL_CMD_START_DEFAULT])
                desc = "默认URL"

            if not await self._write_gatt(BLE_OTA_URL_CHAR_UUID, cmd, response=False, name="URL OTA"):
                return False
            print(f"  URL OTA已触发: {desc}")

            start_time = time.time()
            last_pct = -1

            for _ in range(600):
                await asyncio.sleep(0.5)
                if not self.ota_status:
                    continue
                st = self.ota_status.state
                if st == OTAState.CHECKING:
                    print("\r  版本检查中..." + " " * 30, end='', flush=True)
                elif st == OTAState.CHECK_OK:
                    print("\r  发现新版本，开始下载" + " " * 20, flush=True)
                elif st == OTAState.CHECK_FAIL:
                    print("\r  固件已是最新版本，无需更新" + " " * 20, flush=True)
                    return True
                elif st == OTAState.RECEIVING:
                    if self.ota_status.fw_size > 0:
                        pct = self.ota_status.progress
                        if pct != last_pct:
                            self._print_progress(self.ota_status.bytes_written, self.ota_status.fw_size, start_time, "下载")
                            last_pct = pct
                elif st == OTAState.VERIFYING:
                    print("\r  校验中" + " " * 50, flush=True)
                elif st == OTAState.VERIFY_OK:
                    print("\r  校验成功" + " " * 50, flush=True)
                elif st == OTAState.VERIFY_FAIL:
                    print(f"\r  校验失败: {self._get_ota_error_msg()}" + " " * 30, flush=True)
                    return False
                elif st == OTAState.APPLYING:
                    print("\r  应用中" + " " * 50, flush=True)
                elif st == OTAState.APPLY_OK:
                    print("\r  OTA应用成功，设备将重启" + " " * 30, flush=True)
                    return True
                elif st == OTAState.APPLY_FAIL:
                    print(f"\r  应用失败: {self._get_ota_error_msg()}" + " " * 30, flush=True)
                    return False
                elif st == OTAState.ERROR:
                    err = self._get_ota_error_msg()
                    print(f"\r  OTA错误: {err}" + " " * 40, flush=True)
                    return False
                elif st == OTAState.ABORTED:
                    print("\r  OTA已中止" + " " * 50, flush=True)
                    return False
            print("\r  URL OTA等待超时" + " " * 40, flush=True)
            return False
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset"]):
                ota_ok = (self.ota_status and self.ota_status.state
                          in (OTAState.APPLY_OK, OTAState.APPLYING, OTAState.VERIFY_OK))
                if ota_ok:
                    print("\n  设备已断开（OTA可能已完成，设备正在重启）")
                    return True
                print("\n  连接已断开，URL OTA失败")
                return False
            print(f"\n  URL OTA失败: {e}")
            return False
        finally:
            self._ota_active = False
            await self._stop_ota_notify()

    async def ota_url_start_url(self, url):
        if not url or not url.strip():
            print("URL不能为空")
            return False
        return await self.ota_url(url)

    async def ota_url_start_default(self):
        return await self.ota_url(None)
