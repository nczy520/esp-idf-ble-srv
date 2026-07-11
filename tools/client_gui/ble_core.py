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
)
from client.models import (
    DeviceInfo,
    MemoryInfo,
    CPUInfo,
    FlashInfo,
    PartitionInfo,
    OTAStatus,
    OTAState,
    WiFiStatus,
)


EFFECT_MAP = {
    "无": 0,
    "呼吸灯": 1,
    "闪烁": 2,
    "彩虹": 3,
    "频闪": 4,
}

_UI_FLUSH_INTERVAL = 0.05


class BleCore:
    """BLE设备管理器核心类"""

    def __init__(self, event_loop=None):
        self.event_loop = event_loop
        self.ble_client = None
        self.device_address = None
        self.device_name = None
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
        self._ota_ack_event = None
        self._ota_url_status_callback = None
        self._ota_notify_started = False
        self._ota_session_id = 0
        self._ota_active = False
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0

        self._ui_queue = queue.Queue()
        self._ui_flush_lock = threading.Lock()
        self._ui_flush_scheduled = False

    def set_log_callback(self, callback):
        self._log_callback = callback

    def set_disconnect_callback(self, callback):
        self._disconnect_callback = callback

    def set_page(self, page):
        self._ota_page = page

    def set_ota_url_status_callback(self, callback):
        self._ota_url_status_callback = callback

    def _reset_ota_state(self):
        self.ota_status = None
        self._ota_progress_callback = None
        self._ota_fw_size = 0
        self._ota_start_time = 0
        self._ota_ack_event = None
        self._ota_url_status_callback = None
        self._ota_active = False
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0

    async def _stop_ota_notify(self):
        if self._ota_notify_started and self.ble_client and self._is_connected:
            try:
                await self.ble_client.stop_notify(BLE_OTA_STATUS_CHAR_UUID)
            except Exception:
                pass
        self._ota_notify_started = False

    async def _start_ota_notify(self):
        if not self._ota_notify_started and self.ble_client and self._is_connected:
            try:
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
        self._ota_ack_event = None
        self._ota_start_time = time.time()
        self._last_ota_state = None
        self._last_ota_log_bytes = -1
        self._last_progress_log_time = 0
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
            except Exception:
                pass

    def _on_disconnect(self, client):
        self._is_connected = False
        self._ota_active = False
        self._ota_notify_started = False
        if not self._restart_in_progress:
            self._log("设备已断开连接", "warn")
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

    async def scan_devices(self, timeout=5, name_filter=None, on_device_found=None):
        devices = []
        name_filter_lower = name_filter.lower() if name_filter else None

        def detection_callback(device, advertising_data):
            name = device.name or ""
            if name and (name_filter_lower is None or name.lower().startswith(name_filter_lower)):
                info = {
                    "address": device.address,
                    "name": name,
                    "rssi": advertising_data.rssi if advertising_data.rssi is not None else -100,
                }
                if info not in devices:
                    devices.append(info)
                    if on_device_found:
                        on_device_found(info)

        self._log("开始扫描BLE设备...", "info")
        async with BleakScanner(detection_callback=detection_callback) as scanner:
            await asyncio.sleep(timeout)
        self._log(f"扫描完成，发现 {len(devices)} 个设备", "info")
        return devices

    async def connect_device(self, device_info):
        address = device_info.get("address") if isinstance(device_info, dict) else device_info.address
        name = device_info.get("name", "Unknown") if isinstance(device_info, dict) else getattr(device_info, "name", "Unknown")
        self.device_address = address
        self.device_name = name
        self._log(f"正在连接 {name} [{address}]...", "info")
        try:
            self.ble_client = BleakClient(address, disconnected_callback=self._on_disconnect, use_cached=False)
            await self.ble_client.connect()
            self._is_connected = True
            mtu = self.ble_client.mtu_size if hasattr(self.ble_client, "mtu_size") else 247
            return True, mtu
        except BleakError as e:
            self._log(f"连接失败: {e}", "error")
            self._is_connected = False
            return False, str(e)

    async def disconnect_device(self):
        await self._stop_ota_notify()
        self._reset_ota_state()
        if self.ble_client:
            try:
                await self.ble_client.disconnect()
            except Exception:
                pass
            self.ble_client = None
        self._is_connected = False
        self.device_address = None
        self.device_name = None
        self._log("已断开连接", "info")
        return True

    async def get_rssi(self):
        return None

    async def _read_gatt(self, uuid):
        if not self.connected:
            raise BleakError("未连接")
        self._log(f"读取 GATT [{uuid}]", "tx")
        data = await self.ble_client.read_gatt_char(uuid)
        self._log(f"读取成功 [{uuid}]: {len(data)} bytes", "rx")
        return data

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
            if ignore_disconnect and any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "not found", "abort"]):
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

    async def read_temperature(self):
        data = await self._read_gatt(BLE_DM_INFO_CHAR_UUID)
        if data and len(data) >= 123:
            temp = struct.unpack('<f', data[118:122])[0]
            supported = struct.unpack('<B', data[122:123])[0]
            if not supported:
                return -999.0
            return temp
        return None

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
        await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]))
        self._log("LED 已打开", "success")
        return True

    async def led_off(self):
        await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]))
        self._log("LED 已关闭", "success")
        return True

    async def led_status(self):
        data = await self._read_gatt(BLE_LED_CTRL_CHAR_UUID)
        return data[0] if data else 0

    async def led_set_color(self, r, g, b):
        await self._write_gatt(BLE_LED_COLOR_CHAR_UUID, bytes([g, r, b]))
        self._log(f"LED 颜色已设置: R={r}, G={g}, B={b}", "success")
        return True

    async def led_set_effect(self, effect, speed=50):
        if isinstance(effect, str):
            effect = EFFECT_MAP.get(effect, 0)
        data = bytes([effect, speed])
        await self._write_gatt(BLE_LED_EFFECT_CHAR_UUID, data)
        self._log(f"LED 特效已设置: effect={effect}, speed={speed}", "success")
        return True

    async def wifi_connect(self, ssid, password):
        ssid_bytes = ssid.encode('utf-8')[:32].ljust(33, b'\x00')
        pass_bytes = password.encode('utf-8')[:64].ljust(65, b'\x00')
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, ssid_bytes + pass_bytes)
        self._log(f"WiFi 连接命令已发送: {ssid}", "success")
        return True

    async def wifi_disconnect(self):
        empty_data = b'\x00' * 98
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, empty_data)
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
            self._ota_fw_size = fw_size
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

    async def ota_update(self, fw_path, progress_cb=None):
        if not os.path.exists(fw_path):
            return False, "固件文件不存在"
        with open(fw_path, 'rb') as f:
            fw_data = f.read()
        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version = 0x01000000
        mtu = self.ble_client.mtu_size if self.ble_client else 247
        chunk_size = max(20, mtu - 3) if mtu - 3 >= 20 else 244
        self._log(f"开始OTA: 文件={os.path.basename(fw_path)}, 大小={fw_size}, MTU={mtu}, 块大小={chunk_size}", "info")
        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            self._reset_ota_state()
            return False, "OTA启动失败"
        self._ota_progress_callback = progress_cb
        self._ota_ack_event = asyncio.Event()
        try:
            offset = 0
            failures = 0
            batch_count = 0
            while offset < fw_size:
                if not self._ota_active:
                    self._reset_ota_state()
                    return False, "OTA已中止"
                chunk = fw_data[offset:offset + chunk_size]
                if batch_count == 9 and self._ota_ack_event:
                    self._ota_ack_event.clear()
                ok = await self.ota_send_fw_data(chunk)
                if ok:
                    offset += len(chunk)
                    batch_count += 1
                    failures = 0
                    if batch_count >= 10:
                        batch_count = 0
                        if self._ota_ack_event:
                            try:
                                await asyncio.wait_for(self._ota_ack_event.wait(), timeout=5.0)
                            except asyncio.TimeoutError:
                                pass
                    else:
                        await asyncio.sleep(0.001)
                else:
                    failures += 1
                    if failures >= 10:
                        if not self.connected:
                            self._reset_ota_state()
                            return False, "连接已断开"
                        failures = 0
                    await asyncio.sleep(0.05)
            self._log(f"固件发送完成，开始校验...", "info")
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
                self._reset_ota_state()
                return True, "设备已断开（OTA可能已完成）"
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
                self._log(f"OTA状态: {name}", "rx" if status.state not in (OTAState.ERROR, OTAState.VERIFY_FAIL, OTAState.APPLY_FAIL, OTAState.ABORTED) else "warn")
                should_log_progress = True
            elif status.state == OTAState.RECEIVING and status.progress > 0:
                if now - self._last_progress_log_time >= 1.0 or status.progress >= 100:
                    should_log_progress = True

            if should_log_progress and status.progress > 0:
                self._last_progress_log_time = now
                self._log(f"OTA进度: {status.progress}% ({status.bytes_written}/{status.fw_size} bytes)", "rx")

            if self._ota_ack_event:
                self._ota_ack_event.set()

            if self._ota_progress_callback and self._ota_fw_size > 0:
                if status.bytes_written > 0 or status.state in (OTAState.VERIFY_OK, OTAState.APPLY_OK):
                    written = status.bytes_written if status.bytes_written > 0 else self._ota_fw_size
                    if written != self._last_ota_log_bytes or state_changed:
                        self._last_ota_log_bytes = written
                        if self._ota_page:
                            cb = self._ota_progress_callback
                            self._queue_ui("progress", (cb, written, self._ota_fw_size, self._ota_start_time))

            if self._ota_url_status_callback:
                url_cb = self._ota_url_status_callback
                self._queue_ui("url_ota", (url_cb, status.state, status.bytes_written, status.fw_size))

            is_terminal = status.state in (
                OTAState.ABORTED, OTAState.ERROR, OTAState.VERIFY_FAIL,
                OTAState.APPLY_FAIL, OTAState.CHECK_FAIL
            )

            if is_terminal or status.state == OTAState.APPLY_OK:
                self._ota_active = False
                if self.event_loop and self.event_loop.is_running():
                    asyncio.run_coroutine_threadsafe(self._stop_ota_notify(), self.event_loop)
                else:
                    self._ota_notify_started = False
        except Exception:
            pass
