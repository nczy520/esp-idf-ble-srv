"""
BLE设备管理器核心模块
"""

import asyncio
import struct
import zlib
import os
import time
import queue
import threading
import re
from typing import Optional, List, Dict, Any, Callable, Tuple, Union


def format_device_address(address):
    if not address:
        return ""
    mac_pattern = re.compile(r'^([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$')
    if mac_pattern.match(address):
        return address.replace('-', ':').upper()
    uuid_pattern = re.compile(r'^([0-9A-Fa-f]{8})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{4})-([0-9A-Fa-f]{12})$', re.IGNORECASE)
    match = uuid_pattern.match(address)
    if match:
        parts = match.group(5)
        mac_parts = [parts[i:i+2] for i in range(0, 12, 2)]
        return ':'.join(mac_parts).upper()
    return address.upper()

try:
    from bleak import BleakClient, BleakScanner, BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from client.constants import (
    BLE_DM_INFO_CHAR_UUID,
    BLE_DM_MEMORY_CHAR_UUID,
    BLE_DM_CPU_CHAR_UUID,
    BLE_DM_FLASH_CHAR_UUID,
    BLE_DM_PARTITION_CHAR_UUID,
    BLE_DM_RESTART_CHAR_UUID,
    BLE_DM_AUTH_CHAR_UUID,
    BLE_DM_LOG_CHAR_UUID,
    BLE_DM_CUSTOM_CMD_CHAR_UUID,
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
    BLE_DM_LOG_HTTP_CTRL_CHAR_UUID,
    BLE_DM_LOG_STORAGE_CHAR_UUID,
    BLE_LOG_HTTP_CMD_STOP,
    BLE_LOG_HTTP_CMD_START,
    BLE_LOG_HTTP_CMD_STATUS,
    BLE_LOG_HTTP_CMD_WRITE_LOG,
    BLE_LOG_HTTP_CMD_FORMAT_LITTLEFS,
    BLE_LOG_HTTP_CMD_SET_LEVEL,
    parse_esp_fw_version,
)
from client.models import (
    DeviceInfo,
    MemoryInfo,
    CPUInfo,
    FlashInfo,
    PartitionInfo,
    OTAStatus,
    OTAState,
    OTAError,
    WiFiStatus,
    LogStorageInfo,
)


EFFECT_MAP = {
    "无": 0,
    "呼吸灯": 1,
    "闪烁": 2,
    "彩虹": 3,
    "频闪": 4,
}

OTA_ERROR_NAMES = {
    0: "无错误",
    1: "无效命令",
    2: "固件大小无效",
    3: "Flash写入失败",
    4: "无可用OTA分区",
    5: "固件校验失败",
    6: "设备内部错误",
    7: "设备忙",
    8: "网络未连接",
    9: "用户中止",
    10: "连接断开",
    11: "远程固件版本更旧",
    12: "固件版本相同",
    13: "固件CRC校验失败",
}

_UI_FLUSH_INTERVAL = 0.05


class BleCore:
    """BLE设备管理器核心类"""

    def __init__(self):
        self.ble_client = None
        self.device_address = None
        self.device_name = None
        self.connected_time = None
        self.ota_status = None
        self._is_connected = False
        self._log_callback = None
        self._disconnect_callback = None
        self._restart_notify_received = False
        self._restart_in_progress = False
        self._ota_progress_callback = None
        self._ota_fw_size = 0
        self._ota_start_time = 0
        self._ota_page = None
        self._safe_update_fn = None
        self._ota_ack_event = None
        self._ota_url_status_callback = None
        self._ota_notify_started = False
        self._notify_gen = 0
        self._ota_session_id = 0
        self._ota_active = False
        self._ota_mode = None
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0
        self._ota_ack_bytes = 0
        self._ota_consecutive_timeouts = 0
        self._connect_gen = 0
        self._disconnect_event = None
        self._log_notify_started = False
        self._log_notify_gen = 0

        self._custom_cmd_notify_started = False
        self._custom_cmd_notify_gen = 0
        self._custom_cmd_callback = None

        self._ui_queue = queue.Queue()
        self._ui_flush_lock = threading.Lock()
        self._ui_flush_scheduled = False

    def set_log_callback(self, callback):
        self._log_callback = callback

    def set_disconnect_callback(self, callback):
        self._disconnect_callback = callback

    def set_page(self, page):
        self._ota_page = page

    def set_safe_update(self, fn):
        """注入受全局锁保护的 page.update 包装，供 UI 刷新统一使用，避免与 handler 并发 update 竞争"""
        self._safe_update_fn = fn

    def set_ota_url_status_callback(self, callback):
        self._ota_url_status_callback = callback

    def _reset_ota_state(self):
        ack_event = self._ota_ack_event
        self.ota_status = None
        self._ota_progress_callback = None
        self._ota_fw_size = 0
        self._ota_start_time = 0
        self._ota_ack_event = None
        self._ota_url_status_callback = None
        self._ota_active = False
        self._ota_mode = None
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0
        self._ota_ack_bytes = 0
        self._ota_consecutive_timeouts = 0
        if ack_event:
            ack_event.set()

    async def _stop_ota_notify(self):
        notify_gen = self._notify_gen
        client = self.ble_client
        if self._ota_notify_started and client and self._is_connected:
            try:
                await client.stop_notify(BLE_OTA_STATUS_CHAR_UUID)
            except Exception:
                pass
        if self._notify_gen == notify_gen:
            self._ota_notify_started = False

    async def _start_ota_notify(self):
        if not self._ota_notify_started and self.ble_client and self._is_connected:
            try:
                self._notify_gen += 1
                await self.ble_client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
                self._ota_notify_started = True
            except Exception as e:
                self._log(f"启动OTA通知失败: {e}", "warn")
        return self._ota_notify_started

    def _begin_ota_session(self):
        self._ota_session_id += 1
        self._ota_active = True
        self.ota_status = None
        self._ota_fw_size = 0
        self._ota_start_time = time.time()
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0
        self._ota_ack_bytes = 0
        self._ota_consecutive_timeouts = 0
        return self._ota_session_id

    def _schedule_ui_flush(self):
        if not self._ota_page:
            return
        with self._ui_flush_lock:
            if self._ui_flush_scheduled:
                return
            self._ui_flush_scheduled = True

        def _do_flush():
            try:
                self._flush_ui_updates()
            finally:
                with self._ui_flush_lock:
                    self._ui_flush_scheduled = False
                if not self._ui_queue.empty():
                    self._schedule_ui_flush()

        try:
            self._ota_page.run_thread(_do_flush)
        except Exception:
            with self._ui_flush_lock:
                self._ui_flush_scheduled = False

    def _queue_ui(self, item_type, payload):
        self._ui_queue.put((item_type, payload))
        self._schedule_ui_flush()

    def _flush_ui_updates(self):
        if not self._ota_page:
            return
        logs = []
        progress = None
        disc_cb = None
        url_cb = None

        while True:
            try:
                item_type, payload = self._ui_queue.get_nowait()
            except queue.Empty:
                break
            if item_type == "log":
                logs.append(payload)
            elif item_type == "progress":
                progress = payload
            elif item_type == "disconnect":
                disc_cb = payload
            elif item_type == "url_ota":
                url_cb = payload

        if not logs and progress is None and disc_cb is None and url_cb is None:
            return

        try:
            log_cb = self._log_callback
            for msg, direction in logs:
                try:
                    if log_cb:
                        log_cb(msg, direction)
                except Exception:
                    pass

            if progress is not None:
                cb, written, total, start_time = progress
                try:
                    if cb:
                        cb(written, total, written, start_time)
                except Exception:
                    pass

            if disc_cb is not None:
                try:
                    disc_cb()
                except Exception:
                    pass

            if url_cb is not None:
                cb, state, sw, fs = url_cb
                try:
                    if cb:
                        if state == OTAState.CHECKING:
                            cb("checking", None)
                        elif state == OTAState.CHECK_OK:
                            cb("check_ok", None)
                        elif state == OTAState.CHECK_FAIL:
                            cb("no_update", None)
                        elif state == OTAState.RECEIVING:
                            cb("receiving", (sw, fs))
                        elif state == OTAState.VERIFYING:
                            cb("verifying", None)
                        elif state == OTAState.VERIFY_OK:
                            cb("verify_ok", None)
                        elif state == OTAState.VERIFY_FAIL:
                            cb("error", None)
                        elif state == OTAState.APPLYING:
                            cb("applying", None)
                        elif state == OTAState.APPLY_OK:
                            cb("apply_ok", None)
                        elif state == OTAState.APPLY_FAIL:
                            cb("error", None)
                        elif state == OTAState.ABORTED:
                            cb("aborted", None)
                        elif state == OTAState.ABORTING:
                            pass
                        elif state == OTAState.ERROR:
                            cb("error", None)
                except Exception:
                    pass
        finally:
            try:
                if self._safe_update_fn:
                    self._safe_update_fn()
                else:
                    self._ota_page.update()
            except Exception:
                pass

    def _log(self, msg, direction="info"):
        if not self._log_callback:
            return
        if self._ota_page:
            self._queue_ui("log", (msg, direction))
        else:
            try:
                self._log_callback(msg, direction)
            except Exception as e:
                print(f"[BLE] Log callback error: {e}")

    def _make_disconnect_handler(self, gen):
        def _handler(client):
            if gen != self._connect_gen:
                return
            self._handle_disconnect()
        return _handler

    def _handle_disconnect(self):
        self._is_connected = False
        self.connected_time = None
        self._ota_notify_started = False
        self._notify_gen += 1
        self._log_notify_started = False
        self._log_notify_gen += 1
        self._custom_cmd_notify_started = False
        self._custom_cmd_notify_gen += 1
        self._custom_cmd_callback = None
        self._reset_ota_state()
        if not self._restart_in_progress:
            self._log("设备已断开连接", "warn")
        if self._disconnect_event:
            try:
                self._disconnect_event.set()
            except Exception:
                pass
        if self._disconnect_callback:
            if self._ota_page:
                self._queue_ui("disconnect", self._disconnect_callback)
            else:
                try:
                    self._disconnect_callback()
                except Exception:
                    pass

    def _on_restart_notify(self, sender, data):
        self._log("收到设备重启确认通知", "rx")
        self._restart_notify_received = True

    @property
    def connected(self):
        return self._is_connected and self.ble_client is not None and self.ble_client.is_connected

    def get_connection_duration_str(self):
        if self.connected_time is None:
            return None
        elapsed = time.time() - self.connected_time
        hours = int(elapsed // 3600)
        minutes = int((elapsed % 3600) // 60)
        seconds = int(elapsed % 60)
        if hours > 0:
            return f"{hours:02d}:{minutes:02d}:{seconds:02d}"
        return f"{minutes:02d}:{seconds:02d}"

    async def scan_devices(self, timeout=5, name_filter=None, on_device_found=None):
        devices = []
        seen_addrs = set()
        name_filter_lower = name_filter.lower() if name_filter else None

        def detection_callback(device, advertising_data):
            name = device.name or ""
            if not name:
                return
            if name_filter_lower is not None and not name.lower().startswith(name_filter_lower):
                return
            addr = format_device_address(device.address)
            if addr in seen_addrs:
                return
            seen_addrs.add(addr)
            info = {
                "address": addr,
                "raw_address": device.address,
                "name": name,
                "rssi": advertising_data.rssi if advertising_data.rssi is not None else -100,
            }
            devices.append(info)
            if on_device_found:
                try:
                    on_device_found(info)
                except Exception:
                    pass

        self._log("开始扫描BLE设备...", "info")
        scanner = BleakScanner(detection_callback=detection_callback)
        try:
            await scanner.start()
            await asyncio.sleep(timeout)
            await scanner.stop()
        except Exception as e:
            self._log(f"扫描异常: {e}", "error")

        self._log(f"扫描完成，发现 {len(devices)} 个设备", "info")
        return devices

    async def connect_device(self, device_info, pin=None):
        address = device_info.get("raw_address", device_info.get("address")) if isinstance(device_info, dict) else getattr(device_info, "address", None)
        name = device_info.get("name", "Unknown") if isinstance(device_info, dict) else getattr(device_info, "name", "Unknown")

        if self.ble_client and self._is_connected:
            try:
                await self.ble_client.disconnect()
            except Exception:
                pass
            self.ble_client = None
            self._is_connected = False
            await asyncio.sleep(0.3)

        self._connect_gen += 1
        gen = self._connect_gen
        self._disconnect_event = asyncio.Event()
        self.device_address = address
        self.device_name = name
        self._restart_in_progress = False
        self._log(f"正在连接 {name} [{address}]...", "info")
        try:
            handler = self._make_disconnect_handler(gen)
            self.ble_client = BleakClient(address, disconnected_callback=handler, use_cached=False)
            await self.ble_client.connect()
            self._is_connected = True
            self.connected_time = time.time()
            # 主动请求 MTU=512，与设备 BLE_SRV_PREFERRED_MTU 对齐。
            # 失败/不支持时回退到 bleak 报告的 mtu_size。
            try:
                if hasattr(self.ble_client, "set_mtu"):
                    await self.ble_client.set_mtu(512)
            except Exception as e:
                self._log(f"set_mtu 失败，使用默认 MTU: {e}", "warn")
            mtu = self.ble_client.mtu_size if hasattr(self.ble_client, "mtu_size") else 247
            self._log(f"连接成功: {name} (MTU={mtu})", "success")
            if pin:
                if not await self._authenticate(pin):
                    self._log("PIN认证失败，断开连接", "error")
                    try:
                        await self.ble_client.disconnect()
                    except Exception:
                        pass
                    self._is_connected = False
                    self.ble_client = None
                    return False, "PIN认证失败"
            await self._subscribe_log_notify()
            return True, mtu
        except BleakError as e:
            self._log(f"连接失败: {e}", "error")
            self._is_connected = False
            if self._connect_gen == gen:
                self.ble_client = None
            return False, str(e)
        except Exception as e:
            self._log(f"连接异常: {e}", "error")
            self._is_connected = False
            if self._connect_gen == gen:
                self.ble_client = None
            return False, str(e)

    async def _authenticate(self, pin):
        """通过写入 PIN 码并订阅 auth 特征 notify 等待结果。
        设备在写回调中比较 PIN 后主动 notify 0x01(成功) / 0x00(失败)，
        取代旧版"写后立即读"竞态方案。
        """
        if not self.ble_client or not self.ble_client.is_connected:
            return False
        try:
            self._log("正在PIN认证...", "tx")
            pin_bytes = pin.encode('utf-8')[:16]
            auth_result = asyncio.Event()
            auth_code = {'value': None}

            def on_auth_notify(sender, data):
                if data and len(data) >= 1:
                    auth_code['value'] = data[0]
                    auth_result.set()

            await self.ble_client.start_notify(BLE_DM_AUTH_CHAR_UUID, on_auth_notify)
            try:
                await self.ble_client.write_gatt_char(BLE_DM_AUTH_CHAR_UUID, pin_bytes, response=True)
                try:
                    await asyncio.wait_for(auth_result.wait(), timeout=3.0)
                except asyncio.TimeoutError:
                    self._log("PIN认证超时：未收到设备通知", "error")
                    return False
                if auth_code['value'] == 1:
                    self._log("PIN认证成功", "success")
                    return True
                else:
                    self._log("PIN认证失败: 设备返回 0x00", "error")
                    return False
            finally:
                try:
                    await self.ble_client.stop_notify(BLE_DM_AUTH_CHAR_UUID)
                except Exception:
                    pass
        except Exception as e:
            self._log(f"PIN认证异常: {e}", "error")
            return False

    def _on_log_notify(self, sender, data: bytearray):
        """处理LOG特征值通知"""
        try:
            msg = bytes(data).decode('utf-8', errors='replace')
        except Exception:
            msg = repr(data)
        if not msg:
            return
        level = "info"
        if msg.startswith("[E]"):
            level = "error"
        elif msg.startswith("[W]"):
            level = "warn"
        elif msg.startswith("[D]"):
            level = "debug"
        elif msg.startswith("[I]"):
            level = "info"
        self._log(msg, level)

    async def _unsubscribe_log_notify(self):
        """取消LOG特征值通知订阅"""
        notify_gen = self._log_notify_gen
        client = self.ble_client
        if self._log_notify_started and client and self._is_connected:
            try:
                await client.stop_notify(BLE_DM_LOG_CHAR_UUID)
            except Exception:
                pass
        if self._log_notify_gen == notify_gen:
            self._log_notify_started = False

    async def _subscribe_log_notify(self):
        """订阅LOG特征值通知（接收设备端日志）"""
        if not self.ble_client or not self.ble_client.is_connected:
            return False
        if self._log_notify_started:
            return True
        try:
            self._log_notify_gen += 1
            await self.ble_client.start_notify(BLE_DM_LOG_CHAR_UUID, self._on_log_notify)
            self._log_notify_started = True
            self._log("设备日志订阅已开启", "info")
            return True
        except Exception as e:
            self._log(f"订阅设备日志失败: {e}", "warn")
            return False

    def set_custom_cmd_callback(self, callback):
        """设置自定义命令回调"""
        self._custom_cmd_callback = callback

    def _on_custom_cmd_notify(self, sender, data: bytearray):
        """处理自定义命令特征值通知"""
        try:
            if self._custom_cmd_callback:
                self._custom_cmd_callback(bytes(data))
        except Exception:
            pass

    async def _unsubscribe_custom_cmd_notify(self):
        """取消自定义命令特征值通知订阅"""
        notify_gen = self._custom_cmd_notify_gen
        client = self.ble_client
        if self._custom_cmd_notify_started and client and self._is_connected:
            try:
                await client.stop_notify(BLE_DM_CUSTOM_CMD_CHAR_UUID)
            except Exception:
                pass
        if self._custom_cmd_notify_gen == notify_gen:
            self._custom_cmd_notify_started = False

    async def _subscribe_custom_cmd_notify(self):
        """订阅自定义命令特征值通知（接收设备端自定义响应）"""
        if not self.ble_client or not self.ble_client.is_connected:
            return False
        if self._custom_cmd_notify_started:
            return True
        try:
            self._custom_cmd_notify_gen += 1
            await self.ble_client.start_notify(BLE_DM_CUSTOM_CMD_CHAR_UUID, self._on_custom_cmd_notify)
            self._custom_cmd_notify_started = True
            self._log("自定义命令通知订阅已开启", "info")
            return True
        except Exception as e:
            self._log(f"订阅自定义命令通知失败: {e}", "warn")
            return False

    async def custom_cmd_send(self, data, response=True):
        """发送自定义命令数据
        Args:
            data: bytes类型的数据
            response: 是否需要等待响应（保留接口，目前通过notify返回）
        Returns:
            bool: 是否发送成功
        """
        if not self.connected:
            return False
        if not isinstance(data, (bytes, bytearray)):
            return False
        try:
            if not self._custom_cmd_notify_started:
                await self._subscribe_custom_cmd_notify()
            await self._write_gatt(BLE_DM_CUSTOM_CMD_CHAR_UUID, bytes(data), response=response)
            return True
        except Exception as e:
            self._log(f"自定义命令发送失败: {e}", "error")
            return False

    async def disconnect_device(self):
        await self._stop_ota_notify()
        await self._unsubscribe_log_notify()
        await self._unsubscribe_custom_cmd_notify()
        self._reset_ota_state()
        old_client = self.ble_client
        disc_event = self._disconnect_event
        if old_client:
            self.ble_client = None
            try:
                await old_client.disconnect()
            except Exception:
                pass
            if disc_event and self._is_connected:
                try:
                    await asyncio.wait_for(disc_event.wait(), timeout=2.0)
                except Exception:
                    pass
            self._connect_gen += 1
            self._notify_gen += 1
            try:
                await asyncio.sleep(0.3)
            except Exception:
                pass
        self._is_connected = False
        self.device_address = None
        self.device_name = None
        self.connected_time = None
        self.ble_client = None
        self._log("已断开连接", "info")
        return True

    async def get_rssi(self):
        return None

    async def _read_gatt(self, uuid):
        if not self.connected:
            raise BleakError("未连接")
        self._log(f"读取 GATT [{uuid}]", "tx")
        try:
            data = await self.ble_client.read_gatt_char(uuid)
            self._log(f"读取成功 [{uuid}]: {len(data)} bytes", "rx")
            return data
        except (BleakError, OSError) as e:
            if "not found" in str(e).lower():
                self._log(f"特征不存在 [{uuid}]", "warn")
                return None
            raise

    async def _write_gatt(self, uuid, data, response=True, ignore_disconnect=False):
        if not self.connected:
            raise BleakError("未连接")
        self._log(f"写入 GATT [{uuid}]: {len(data)} bytes", "tx")
        try:
            await self.ble_client.write_gatt_char(uuid, data, response=response)
            self._log(f"写入成功 [{uuid}]", "rx")
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if "not found" in msg:
                self._log(f"特征不存在 [{uuid}]", "warn")
                return False
            if ignore_disconnect and any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "abort"]):
                self._log("写入完成（设备已断开）", "warn")
                self._is_connected = False
                if self._disconnect_callback:
                    if self._ota_page:
                        self._queue_ui("disconnect", self._disconnect_callback)
                    else:
                        try:
                            self._disconnect_callback()
                        except Exception:
                            pass
                return True
            self._log(f"写入失败 [{uuid}]: {e}", "error")
            raise

    async def read_device_info(self):
        data = await self._read_gatt(BLE_DM_INFO_CHAR_UUID)
        return DeviceInfo(data)

    async def read_memory_info(self):
        data = await self._read_gatt(BLE_DM_MEMORY_CHAR_UUID)
        return MemoryInfo(data)

    async def read_cpu_info(self):
        data = await self._read_gatt(BLE_DM_CPU_CHAR_UUID)
        return CPUInfo(data)

    async def read_flash_info(self):
        data = await self._read_gatt(BLE_DM_FLASH_CHAR_UUID)
        return FlashInfo(data)

    async def read_partition_info(self, index=0):
        await self._write_gatt(BLE_DM_PARTITION_CHAR_UUID, bytes([index]))
        await asyncio.sleep(0.1)
        data = await self._read_gatt(BLE_DM_PARTITION_CHAR_UUID)
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
        if not self.connected:
            return False
        self._restart_notify_received = False
        self._restart_in_progress = True
        try:
            self._log("启动重启通知监听", "tx")
            try:
                await self.ble_client.start_notify(BLE_DM_RESTART_CHAR_UUID, self._on_restart_notify)
            except Exception as e:
                self._log(f"启动通知失败: {e}", "warn")
            self._log("发送重启命令", "tx")
            try:
                await self.ble_client.write_gatt_char(BLE_DM_RESTART_CHAR_UUID, bytes([BLE_DM_CMD_RESTART]), response=False)
            except Exception as e:
                self._log(f"发送命令异常: {e}（设备可能已开始重启）", "warn")
            for _ in range(15):
                await asyncio.sleep(0.1)
                if self._restart_notify_received:
                    self._log("设备确认重启成功", "success")
                    break
            else:
                self._log("未收到确认，设备可能已重启", "warn")
            try:
                await self.ble_client.stop_notify(BLE_DM_RESTART_CHAR_UUID)
            except Exception:
                pass
            self._is_connected = False
            if self._disconnect_callback:
                if self._ota_page:
                    self._queue_ui("disconnect", self._disconnect_callback)
                else:
                    try:
                        self._disconnect_callback()
                    except Exception:
                        pass
            self._log("设备重启完成", "success")
            return True
        except asyncio.CancelledError:
            return True
        except Exception as e:
            self._log(f"重启设备异常: {e}", "error")
            return False
        finally:
            self._restart_in_progress = False

    async def led_on(self):
        if not await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON])):
            return False
        self._log("LED 已打开", "success")
        return True

    async def led_off(self):
        if not await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF])):
            return False
        self._log("LED 已关闭", "success")
        return True

    async def led_status(self):
        data = await self._read_gatt(BLE_LED_CTRL_CHAR_UUID)
        return data[0] if data else 0

    async def led_set_color(self, r, g, b):
        if not await self._write_gatt(BLE_LED_COLOR_CHAR_UUID, bytes([r, g, b])):
            return False
        self._log(f"LED 颜色已设置: R={r}, G={g}, B={b}", "success")
        return True

    async def led_set_effect(self, effect, speed=50):
        if isinstance(effect, str):
            effect = EFFECT_MAP.get(effect, 0)
        data = bytes([effect, speed])
        if not await self._write_gatt(BLE_LED_EFFECT_CHAR_UUID, data):
            return False
        self._log(f"LED 特效已设置: effect={effect}, speed={speed}", "success")
        return True

    async def wifi_connect(self, ssid, password):
        ssid_bytes = ssid.encode('utf-8')[:32]
        pass_bytes = password.encode('utf-8')[:64]
        cmd = bytes([len(ssid_bytes)]) + ssid_bytes + bytes([len(pass_bytes)]) + pass_bytes
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, cmd)
        self._log(f"WiFi 连接命令已发送: {ssid}", "success")
        return True

    async def wifi_disconnect(self):
        cmd = b'\x00\x00'
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, cmd)
        self._log("WiFi 断开命令已发送", "success")
        return True

    async def wifi_status(self):
        data = await self._read_gatt(BLE_WIFI_STATUS_CHAR_UUID)
        return WiFiStatus(data) if data else None

    async def wifi_forget(self):
        await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_FORGET]))
        self._log("WiFi 忘记命令已发送", "success")
        return True

    async def wifi_ntp_sync(self):
        await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]))
        self._log("NTP 同步命令已发送", "success")
        return True

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        if not self.connected:
            return False
        try:
            self._begin_ota_session()
            self._ota_mode = "bt"
            self._ota_fw_size = fw_size
            self._ota_ack_event = asyncio.Event()
            self._ota_ack_bytes = 0
            await self._start_ota_notify()
            self._log("OTA通知已启动", "rx")
            cmd = struct.pack('<BIIHHI', BLE_OTA_BT_CMD_START, fw_size, fw_crc, chunk_size, 0, fw_version)
            self._log(f"OTA启动: size={fw_size}, crc={fw_crc:#x}, chunk={chunk_size}", "tx")
            await self.ble_client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, cmd)
            self._log("OTA启动命令已发送", "rx")
            await asyncio.sleep(0.5)
            if self.ota_status and self.ota_status.state != OTAState.RECEIVING:
                return False
            return True
        except BleakError as e:
            self._log(f"OTA启动失败: {e}", "error")
            return False

    async def ota_send_fw_data(self, data, max_retries=3):
        if not self.connected:
            return False
        for attempt in range(max_retries):
            try:
                await self.ble_client.write_gatt_char(BLE_OTA_BT_FW_DATA_CHAR_UUID, data, response=False)
                return True
            except BleakError:
                if attempt < max_retries - 1:
                    await asyncio.sleep(0.05 * (attempt + 1))
                else:
                    return False

    async def ota_verify(self):
        if not self.connected:
            return False
        try:
            self._log("OTA校验", "tx")
            await self.ble_client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_VERIFY]))
            for _ in range(30):
                await asyncio.sleep(0.5)
                if not self.connected:
                    self._log("校验期间连接断开", "warn")
                    return False
                if self.ota_status:
                    if self.ota_status.state == OTAState.VERIFY_OK:
                        self._log("OTA校验成功", "success")
                        return True
                    elif self.ota_status.state in [OTAState.VERIFY_FAIL, OTAState.ERROR, OTAState.ABORTED]:
                        self._log("OTA校验失败或中止", "error")
                        return False
            self._log("OTA校验超时", "error")
            return False
        except BleakError as e:
            self._log(f"OTA校验异常: {e}", "error")
            return False

    async def ota_apply(self):
        if not self.connected:
            return True
        try:
            self._log("OTA应用", "tx")
            await self.ble_client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_APPLY]))
            self._log("OTA应用命令已发送，设备即将重启", "success")
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "not found", "abort", "取消", "cancelled"]):
                self._log("OTA应用完成，设备正在重启", "success")
                return True
            self._log(f"OTA应用失败: {e}", "error")
            return False

    async def ota_abort(self):
        if not self.connected:
            self._reset_ota_state()
            return True
        try:
            try:
                await self.ble_client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_ABORT]))
                self._log("蓝牙OTA中止命令已发送", "rx")
            except Exception as e:
                self._log(f"蓝牙OTA中止: {e}", "warn")
            try:
                await self.ble_client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, bytes([BLE_OTA_URL_CMD_ABORT]))
                self._log("URL OTA中止命令已发送", "rx")
            except Exception as e:
                self._log(f"URL OTA中止: {e}", "warn")
            return True
        except (BleakError, OSError) as e:
            self._log(f"OTA中止: {e}", "warn")
            return True

    async def ota_url_start(self, url=None):
        if not self.connected:
            return False, "未连接"
        try:
            self._begin_ota_session()
            self._ota_mode = "url"
            await self._start_ota_notify()

            if url:
                url_bytes = url.encode('utf-8')
                if len(url_bytes) > 256:
                    return False, "URL过长"
                cmd_data = bytes([BLE_OTA_URL_CMD_START_URL]) + url_bytes
                self._log(f"URL OTA: {url}", "tx")
                await self.ble_client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, cmd_data, response=False)
                self._log("URL OTA命令已发送", "rx")
            else:
                self._log("默认URL OTA", "tx")
                cmd_data = bytes([BLE_OTA_URL_CMD_START_DEFAULT])
                await self.ble_client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, cmd_data, response=False)
                self._log("默认URL OTA命令已发送", "rx")
            return True, "OTA URL已触发"
        except BleakError as e:
            self._log(f"URL OTA失败: {e}", "error")
            self._ota_active = False
            return False, str(e)

    @staticmethod
    def _read_fw_file(fw_path):
        with open(fw_path, 'rb') as f:
            return f.read()

    async def ota_update(self, fw_path, progress_cb=None):
        if not os.path.exists(fw_path):
            return False, "固件文件不存在"
        # 读取整个固件并计算 CRC 是阻塞操作，放到线程池执行，避免卡住事件循环
        loop = asyncio.get_event_loop()
        fw_data = await loop.run_in_executor(None, self._read_fw_file, fw_path)
        fw_size = len(fw_data)
        fw_crc = await loop.run_in_executor(None, lambda: zlib.crc32(fw_data) & 0xFFFFFFFF)
        fw_version, fw_ver_str = parse_esp_fw_version(fw_data)
        mtu = self.ble_client.mtu_size if self.ble_client else 247
        attr_overhead = 3
        offset_header = 4
        chunk_size = max(20, mtu - attr_overhead - offset_header)
        window_packets = 12
        window_bytes = window_packets * chunk_size
        packet_interval = 0.003
        self._log(f"开始OTA: 文件={os.path.basename(fw_path)}, 大小={fw_size}, 版本={fw_ver_str}, MTU={mtu}, 块大小={chunk_size}", "info")
        self._ota_progress_callback = progress_cb
        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            self._reset_ota_state()
            return False, "OTA启动失败"
        try:
            acked_offset = 0
            sent_offset = 0
            total_retries = 0
            max_retries = 50
            ack_timeout = 2.0

            while acked_offset < fw_size:
                if not self._ota_active:
                    self._reset_ota_state()
                    if self.ota_status and self.ota_status.state in (OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL, OTAState.APPLY_FAIL, OTAState.CHECK_FAIL):
                        err_name = OTA_ERROR_NAMES.get(self.ota_status.error_code, f"错误码{self.ota_status.error_code}")
                        return False, f"OTA失败: {err_name}"
                    return False, "OTA已中止"

                if not self.connected:
                    self._reset_ota_state()
                    return False, "连接已断开"

                if self.ota_status and self.ota_status.state in (
                        OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL,
                        OTAState.APPLY_FAIL, OTAState.CHECK_FAIL):
                    self._reset_ota_state()
                    err_name = OTA_ERROR_NAMES.get(self.ota_status.error_code, f"错误码{self.ota_status.error_code}")
                    return False, f"OTA失败: {err_name}"

                window_end = min(acked_offset + window_bytes, fw_size)
                sent_something = False
                while sent_offset < window_end:
                    chunk = fw_data[sent_offset:sent_offset + chunk_size]
                    pkt = struct.pack('<I', sent_offset) + chunk
                    ok = await self.ota_send_fw_data(pkt)
                    if not ok:
                        break
                    sent_offset += len(chunk)
                    sent_something = True
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
                            self._log(f"等待ACK超时次数过多({max_retries}次)，传输失败", "error")
                            await self.ota_abort()
                            self._reset_ota_state()
                            return False, "传输超时，设备无响应"
                        if total_retries <= 3 or total_retries % 10 == 0:
                            self._log(f"等待ACK超时，从offset={acked_offset}重传 (retry={total_retries})", "warn")
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
                            self._log(f"重传次数过多({max_retries}次)，传输失败", "error")
                            await self.ota_abort()
                            self._reset_ota_state()
                            return False, "传输失败"

            self._log("固件数据发送完毕，等待设备确认全部数据...", "info")
            wait_ok = False
            for _ in range(100):
                await asyncio.sleep(0.05)
                if not self.connected or not self._ota_active:
                    break
                if self.ota_status and self.ota_status.bytes_written >= fw_size:
                    wait_ok = True
                    break
                if self.ota_status and self.ota_status.state in (
                        OTAState.ERROR, OTAState.ABORTED, OTAState.VERIFY_FAIL,
                        OTAState.APPLY_FAIL, OTAState.CHECK_FAIL):
                    break
            if not wait_ok and self.ota_status:
                self._log(f"等待接收确认超时，设备已接收 {self.ota_status.bytes_written}/{fw_size} bytes", "warn")
            else:
                self._log("设备已确认接收全部数据", "rx")
            await asyncio.sleep(0.2)
            self._log("开始校验固件...", "info")
            if not await self.ota_verify():
                await self.ota_abort()
                self._reset_ota_state()
                return False, "OTA校验失败"
            await self.ota_apply()
            self._reset_ota_state()
            return True, "OTA升级成功"
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "not found", "abort"]):
                ota_ok = (self.ota_status and self.ota_status.state
                          in (OTAState.APPLY_OK, OTAState.APPLYING, OTAState.VERIFY_OK))
                self._reset_ota_state()
                if ota_ok:
                    return True, "设备已断开（OTA可能已完成，设备正在重启）"
                return False, "连接已断开，OTA失败"
            await self.ota_abort()
            self._reset_ota_state()
            return False, str(e)
        except Exception as e:
            await self.ota_abort()
            self._reset_ota_state()
            return False, str(e)

    def _ota_status_handler(self, sender, data):
        try:
            if not self._ota_active:
                return
            status = OTAStatus(data)
            self.ota_status = status

            is_bt_ota = (self._ota_mode == "bt")
            is_url_ota = (self._ota_mode == "url")

            state_changed = self._last_ota_state != status.state
            self._last_ota_state = status.state

            now = time.time()
            should_log_progress = False

            if state_changed:
                state_names = {
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
                name = state_names.get(status.state, f"状态{status.state}")
                if is_bt_ota:
                    prefix = "BT OTA"
                    self._log(f"{prefix}状态: {name}", "rx" if status.state not in (OTAState.ERROR, OTAState.VERIFY_FAIL, OTAState.APPLY_FAIL, OTAState.ABORTED) else "warn")
                elif is_url_ota:
                    if status.state not in (OTAState.RECEIVING,):
                        prefix = "URL OTA"
                        self._log(f"{prefix}状态: {name}", "rx" if status.state not in (OTAState.ERROR, OTAState.VERIFY_FAIL, OTAState.APPLY_FAIL, OTAState.ABORTED, OTAState.CHECK_FAIL) else "warn")
                should_log_progress = is_bt_ota
            elif is_bt_ota and status.state == OTAState.RECEIVING and status.progress > 0:
                if now - self._last_progress_log_time >= 1.0 or status.progress >= 100:
                    should_log_progress = True

            if should_log_progress and status.progress > 0:
                self._last_progress_log_time = now
                prefix = "BT OTA" if is_bt_ota else "OTA"
                self._log(f"{prefix}进度: {status.progress}% ({status.bytes_written}/{status.fw_size} bytes)", "rx")

            is_terminal = status.state in (
                OTAState.ABORTED, OTAState.ERROR, OTAState.VERIFY_FAIL,
                OTAState.APPLY_FAIL, OTAState.CHECK_FAIL
            )

            if is_bt_ota and self._ota_ack_event and status.state == OTAState.RECEIVING:
                self._ota_ack_bytes = status.bytes_written
                self._ota_ack_event.set()
                self._ota_consecutive_timeouts = 0

            if is_bt_ota and self._ota_progress_callback and self._ota_fw_size > 0:
                if status.bytes_written > 0 or status.state in (OTAState.VERIFY_OK, OTAState.APPLY_OK):
                    written = status.bytes_written if status.bytes_written > 0 else self._ota_fw_size
                    if written != self._last_ota_log_bytes or state_changed:
                        self._last_ota_log_bytes = written
                        if self._ota_page:
                            cb = self._ota_progress_callback
                            self._queue_ui("progress", (cb, written, self._ota_fw_size, self._ota_start_time))

            if is_url_ota and self._ota_url_status_callback:
                url_cb = self._ota_url_status_callback
                self._queue_ui("url_ota", (url_cb, status.state, status.bytes_written, status.fw_size))

            if is_terminal or status.state == OTAState.APPLY_OK:
                self._ota_active = False
                self._ota_mode = None
                if is_bt_ota and self._ota_ack_event:
                    self._ota_ack_event.set()
                self._notify_gen += 1
                self._ota_notify_started = False
        except Exception as e:
            print(f"[BLE] OTA status handler error: {e}")

    async def log_http_start(self):
        """启动日志HTTP服务器"""
        await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, bytes([BLE_LOG_HTTP_CMD_START]))
        self._log("日志HTTP服务器启动命令已发送", "tx")
        return True

    async def log_http_stop(self):
        """停止日志HTTP服务器"""
        await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, bytes([BLE_LOG_HTTP_CMD_STOP]))
        self._log("日志HTTP服务器停止命令已发送", "tx")
        return True

    async def log_http_get_status(self):
        """获取日志HTTP服务器状态，返回 {"running": bool, "url": str}"""
        data = await self._read_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID)
        if not data or len(data) < 1:
            return {"running": False, "url": ""}
        running = data[0] == 1
        url = ""
        if len(data) >= 2 and data[1] > 0:
            url = data[2:2 + data[1]].decode('utf-8', errors='replace')
        self._log(f"HTTP状态: running={running}, url={url}", "rx")
        return {"running": running, "url": url}

    async def write_device_log(self, message: str):
        """向设备端日志系统写入一条日志消息"""
        if not self._is_connected:
            return False
        try:
            msg_bytes = message.encode('utf-8')
            data = bytes([BLE_LOG_HTTP_CMD_WRITE_LOG]) + msg_bytes
            await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, data)
            return True
        except Exception as e:
            self._log(f"写入设备日志失败: {e}", "err")
            return False

    async def read_log_storage_info(self):
        """读取日志存储信息（总大小、已用、剩余、文件数）"""
        data = await self._read_gatt(BLE_DM_LOG_STORAGE_CHAR_UUID)
        if data:
            self._log(f"存储信息原始数据 ({len(data)} bytes): {data.hex()}", "debug")
            return LogStorageInfo(data)
        return None

    async def log_format_littlefs(self):
        """格式化LittleFS分区"""
        await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, bytes([BLE_LOG_HTTP_CMD_FORMAT_LITTLEFS]))
        self._log("已发送格式化LittleFS命令", "tx")
        return True

    async def log_set_level(self, level):
        """设置日志级别 (0=NONE 1=ERROR 2=WARN 3=INFO 4=DEBUG 5=VERBOSE)"""
        await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, bytes([BLE_LOG_HTTP_CMD_SET_LEVEL, level]))
        self._log(f"已发送设置日志级别命令: {level}", "tx")
        return True

    async def log_write_marker(self, msg):
        """写入客户端标记日志"""
        data = bytes([BLE_LOG_HTTP_CMD_WRITE_LOG]) + msg.encode("utf-8")
        await self._write_gatt(BLE_DM_LOG_HTTP_CTRL_CHAR_UUID, data)
        self._log(f"已写入标记日志: {msg}", "tx")
        return True


