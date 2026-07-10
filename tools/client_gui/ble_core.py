"""
BLE设备管理器核心模块
"""

import asyncio
import struct
import zlib
import os
import time

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

    def set_log_callback(self, callback):
        """设置日志回调函数"""
        self._log_callback = callback

    def set_disconnect_callback(self, callback):
        """设置断开连接回调函数"""
        self._disconnect_callback = callback

    def set_page(self, page):
        """设置页面对象（用于UI更新）"""
        self._ota_page = page

    def _log(self, msg, direction="info"):
        """记录日志"""
        if self._log_callback:
            try:
                self._log_callback(msg, direction)
            except Exception:
                pass

    def _on_disconnect(self, client):
        """设备断开连接回调"""
        self._is_connected = False
        if not self._restart_in_progress:
            self._log("设备已断开连接", "warn")
        if self._disconnect_callback:
            try:
                self._disconnect_callback()
            except Exception:
                pass

    def _on_restart_notify(self, sender, data):
        """重启通知回调（在BLE线程中调用）"""
        self._log("收到设备重启确认通知", "rx")
        self._restart_notify_received = True

    @property
    def connected(self):
        """是否已连接"""
        return self._is_connected and self.ble_client is not None and self.ble_client.is_connected

    async def scan_devices(self, timeout=5, name_filter=None, on_device_found=None):
        """扫描BLE设备"""
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
        """连接设备"""
        address = device_info.get("address") if isinstance(device_info, dict) else device_info.address
        name = device_info.get("name", "Unknown") if isinstance(device_info, dict) else getattr(device_info, "name", "Unknown")
        self.device_address = address
        self.device_name = name
        self._log(f"正在连接 {name} [{address}]...", "info")
        try:
            self.ble_client = BleakClient(address, disconnected_callback=self._on_disconnect)
            await self.ble_client.connect()
            self._is_connected = True
            mtu = self.ble_client.mtu_size if hasattr(self.ble_client, "mtu_size") else 247
            return True, mtu
        except BleakError as e:
            self._log(f"连接失败: {e}", "error")
            self._is_connected = False
            return False, str(e)

    async def disconnect_device(self):
        """断开连接"""
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
        """获取蓝牙信号强度（Bleak不支持连接后获取RSSI）"""
        return None

    async def _read_gatt(self, uuid):
        """读取GATT特征值"""
        if not self.connected:
            raise BleakError("未连接")
        self._log(f"读取 GATT [{uuid}]", "tx")
        data = await self.ble_client.read_gatt_char(uuid)
        self._log(f"读取成功 [{uuid}]: {len(data)} bytes", "rx")
        return data

    async def _write_gatt(self, uuid, data, response=True, ignore_disconnect=False):
        """写入GATT特征值"""
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
                self._log(f"写入完成（设备已断开）", "warn")
                self._is_connected = False
                if self._disconnect_callback:
                    try:
                        self._disconnect_callback()
                    except Exception:
                        pass
                return True
            self._log(f"写入失败 [{uuid}]: {e}", "error")
            raise

    async def read_device_info(self):
        """读取设备信息"""
        data = await self._read_gatt(BLE_DM_INFO_CHAR_UUID)
        return DeviceInfo(data)

    async def read_memory_info(self):
        """读取内存信息"""
        data = await self._read_gatt(BLE_DM_MEMORY_CHAR_UUID)
        return MemoryInfo(data)

    async def read_cpu_info(self):
        """读取CPU信息"""
        data = await self._read_gatt(BLE_DM_CPU_CHAR_UUID)
        return CPUInfo(data)

    async def read_flash_info(self):
        """读取Flash信息"""
        data = await self._read_gatt(BLE_DM_FLASH_CHAR_UUID)
        return FlashInfo(data)

    async def read_partition_info(self, index=0):
        """读取分区信息"""
        await self._write_gatt(BLE_DM_PARTITION_CHAR_UUID, bytes([index]))
        await asyncio.sleep(0.1)
        data = await self._read_gatt(BLE_DM_PARTITION_CHAR_UUID)
        return PartitionInfo(data) if data else None

    async def read_all_partitions(self):
        """读取所有分区信息"""
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
        """读取温度"""
        data = await self._read_gatt(BLE_DM_INFO_CHAR_UUID)
        if data and len(data) >= 123:
            temp = struct.unpack('<f', data[118:122])[0]
            supported = struct.unpack('<B', data[122:123])[0]
            if not supported:
                return -999.0
            return temp
        return None

    async def restart_device(self):
        """重启设备"""
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
            # 等待设备确认（轮询方式，最多1.5秒）
            for _ in range(15):
                await asyncio.sleep(0.1)
                if self._restart_notify_received:
                    self._log("设备确认重启成功", "success")
                    break
            else:
                self._log("未收到确认，设备可能已重启", "warn")
            # 停止通知
            try:
                await self.ble_client.stop_notify(BLE_DM_RESTART_CHAR_UUID)
            except Exception:
                pass
            # 标记断开
            self._is_connected = False
            # 触发断开回调更新UI
            if self._disconnect_callback:
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
        """打开LED"""
        await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]))
        self._log("LED 已打开", "success")
        return True

    async def led_off(self):
        """关闭LED"""
        await self._write_gatt(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]))
        self._log("LED 已关闭", "success")
        return True

    async def led_status(self):
        """读取LED状态"""
        data = await self._read_gatt(BLE_LED_CTRL_CHAR_UUID)
        return data[0] if data else 0

    async def led_set_color(self, r, g, b):
        """设置LED颜色（设备端R和G相反）"""
        await self._write_gatt(BLE_LED_COLOR_CHAR_UUID, bytes([g, r, b]))
        self._log(f"LED 颜色已设置: R={r}, G={g}, B={b}", "success")
        return True

    async def led_set_effect(self, effect, speed=50):
        """设置LED特效"""
        if isinstance(effect, str):
            effect = EFFECT_MAP.get(effect, 0)
        data = bytes([effect, speed])
        await self._write_gatt(BLE_LED_EFFECT_CHAR_UUID, data)
        self._log(f"LED 特效已设置: effect={effect}, speed={speed}", "success")
        return True

    async def wifi_connect(self, ssid, password):
        """连接WiFi"""
        ssid_bytes = ssid.encode('utf-8')[:32].ljust(33, b'\x00')
        pass_bytes = password.encode('utf-8')[:64].ljust(65, b'\x00')
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, ssid_bytes + pass_bytes)
        self._log(f"WiFi 连接命令已发送: {ssid}", "success")
        return True

    async def wifi_disconnect(self):
        """断开WiFi"""
        empty_data = b'\x00' * 98
        await self._write_gatt(BLE_WIFI_CONFIG_CHAR_UUID, empty_data)
        self._log("WiFi 断开命令已发送", "success")
        return True

    async def wifi_status(self):
        """读取WiFi状态"""
        data = await self._read_gatt(BLE_WIFI_STATUS_CHAR_UUID)
        return WiFiStatus(data) if data else None

    async def wifi_forget(self):
        """忘记WiFi网络"""
        await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_FORGET]))
        self._log("WiFi 忘记命令已发送", "success")
        return True

    async def wifi_ntp_sync(self):
        """NTP时间同步"""
        await self._write_gatt(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]))
        self._log("NTP 同步命令已发送", "success")
        return True

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        """启动OTA升级"""
        if not self.connected:
            return False
        try:
            self._log("启动OTA通知", "tx")
            await self.ble_client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
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
        """发送固件数据"""
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
        """OTA校验"""
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
                    elif self.ota_status.state in [OTAState.VERIFY_FAIL, OTAState.ERROR]:
                        self._log("OTA校验失败", "error")
                        return False
            self._log("OTA校验超时", "error")
            return False
        except BleakError as e:
            self._log(f"OTA校验异常: {e}", "error")
            return False

    async def ota_apply(self):
        """OTA应用"""
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
        """OTA中止"""
        if not self.connected:
            return True
        try:
            self._log("OTA中止", "tx")
            await self.ble_client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_ABORT]))
            self._log("OTA中止命令已发送", "rx")
            return True
        except (BleakError, OSError) as e:
            self._log(f"OTA中止: {e}", "warn")
            return True

    async def ota_url_start(self, url=None):
        """URL OTA升级"""
        if not self.connected:
            return False, "未连接"
        try:
            if url:
                url_bytes = url.encode('utf-8')
                if len(url_bytes) > 256:
                    return False, "URL过长"
                self._log(f"URL OTA: {url}", "tx")
                await self.ble_client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, url_bytes, response=False)
                self._log("URL已发送", "rx")
            else:
                self._log("默认URL OTA", "tx")
            cmd = bytes([BLE_OTA_URL_CMD_START_URL])
            await self.ble_client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, cmd, response=False)
            self._log("OTA URL启动命令已发送", "rx")
            return True, "OTA URL已触发"
        except BleakError as e:
            self._log(f"URL OTA失败: {e}", "error")
            return False, str(e)

    async def ota_update(self, fw_path, progress_cb=None):
        """OTA升级主流程"""
        if not os.path.exists(fw_path):
            return False, "固件文件不存在"
        with open(fw_path, 'rb') as f:
            fw_data = f.read()
        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version = 0x01000000
        mtu = self.ble_client.mtu_size if self.ble_client else 247
        chunk_size = max(20, mtu - 3) if mtu - 3 >= 20 else 244
        self.ota_status = None
        self._ota_progress_callback = progress_cb
        self._ota_fw_size = fw_size
        self._ota_start_time = time.time()
        self._log(f"开始OTA: 文件={os.path.basename(fw_path)}, 大小={fw_size}, MTU={mtu}, 块大小={chunk_size}", "info")
        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            self._ota_progress_callback = None
            return False, "OTA启动失败"
        try:
            start_time = time.time()
            offset = 0
            sent_bytes = 0
            failures = 0
            max_inflight = 16 * 1024
            while offset < fw_size:
                if self.ota_status and self.ota_status.bytes_written > 0:
                    if sent_bytes - self.ota_status.bytes_written > max_inflight:
                        await asyncio.sleep(0.01)
                        continue
                chunk = fw_data[offset:offset + chunk_size]
                ok = await self.ota_send_fw_data(chunk)
                if ok:
                    sent_bytes += len(chunk)
                    offset += len(chunk)
                    failures = 0
                else:
                    failures += 1
                    if failures >= 10:
                        if not self.connected:
                            return False, "连接已断开"
                        failures = 0
                    await asyncio.sleep(0.05)
            elapsed = time.time() - start_time
            speed = fw_size / elapsed if elapsed > 0 else 1.5
            remain = fw_size - (self.ota_status.bytes_written if self.ota_status else 0)
            expect = remain / speed if speed > 0 else 60
            timeout = max(120, int(expect * 2))
            self._log(f"固件发送完成，等待设备写入... 预计{expect:.1f}秒", "info")
            for i in range(timeout):
                await asyncio.sleep(1)
                if self.ota_status:
                    if self.ota_status.bytes_written >= fw_size:
                        self._log("设备写入完成", "success")
                        break
            else:
                await self.ota_abort()
                self._ota_progress_callback = None
                return False, "设备写入超时"
            if not await self.ota_verify():
                await self.ota_abort()
                self._ota_progress_callback = None
                return False, "OTA校验失败"
            await self.ota_apply()
            self._ota_progress_callback = None
            return True, "OTA升级成功"
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "reset", "not found", "abort"]):
                self._ota_progress_callback = None
                return True, "设备已断开（OTA可能已完成）"
            await self.ota_abort()
            self._ota_progress_callback = None
            return False, str(e)
        except Exception as e:
            await self.ota_abort()
            self._ota_progress_callback = None
            return False, str(e)

    def _ota_status_handler(self, sender, data):
        """OTA状态通知处理"""
        try:
            self.ota_status = OTAStatus(data)
            if self._ota_progress_callback and self.ota_status.bytes_written > 0 and self._ota_fw_size > 0:
                written = self.ota_status.bytes_written
                total = self._ota_fw_size
                start_time = self._ota_start_time
                if self.event_loop and self._ota_page:
                    def update_progress():
                        try:
                            self._ota_progress_callback(written, total, 0, start_time)
                        except Exception:
                            pass
                    self._ota_page.run_thread(update_progress)
        except Exception:
            pass