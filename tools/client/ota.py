"""
OTA（空中下载）功能模块
包含蓝牙OTA和URL OTA功能
"""

import asyncio
import struct
import time
import os
import zlib

try:
    from bleak import BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    import sys
    sys.exit(1)

from .constants import (
    BLE_OTA_STATUS_CHAR_UUID,
    BLE_OTA_BT_CMD_CHAR_UUID,
    BLE_OTA_BT_FW_DATA_CHAR_UUID,
    BLE_OTA_URL_CHAR_UUID,
    BLE_OTA_BT_CMD_START,
    BLE_OTA_BT_CMD_ABORT,
    BLE_OTA_BT_CMD_VERIFY,
    BLE_OTA_BT_CMD_APPLY,
    BLE_OTA_URL_CMD_START_URL,
    BLE_OTA_URL_CMD_START_DEFAULT,
    BLE_OTA_URL_CMD_ABORT
)
from .models import OTAStatus, OTAState, OTAError


class OTAMixin:
    """OTA功能混合类，需与BLEDeviceManagerClient一起使用"""

    def __init__(self):
        self.ota_status = None
        self._ota_ack_event = None

    def _ota_status_handler(self, sender, data):
        try:
            self.ota_status = OTAStatus(data)
            if self._ota_ack_event:
                self._ota_ack_event.set()
        except Exception as e:
            print(f"解析OTA状态失败: {e}")

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)

            cmd_data = struct.pack('<BIIHHI', BLE_OTA_BT_CMD_START, fw_size, fw_crc, chunk_size, 0, fw_version)
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, cmd_data)

            await asyncio.sleep(0.5)
            if self.ota_status and self.ota_status.state != OTAState.RECEIVING:
                print(f"OTA启动失败: {self.ota_status}")
                return False
            print("OTA会话已启动")
            return True
        except BleakError as e:
            print(f"OTA启动失败: {e}")
            return False

    async def ota_send_fw_data(self, data, max_retries=3):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        for attempt in range(max_retries):
            try:
                await self.client.write_gatt_char(BLE_OTA_BT_FW_DATA_CHAR_UUID, data, response=False)
                return True
            except BleakError as e:
                if attempt < max_retries - 1:
                    await asyncio.sleep(0.05 * (attempt + 1))
                else:
                    print(f"发送固件数据失败({max_retries}次重试): {e}")
                    return False

    async def ota_verify(self):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_VERIFY]))

            for _ in range(30):
                await asyncio.sleep(0.5)
                if self.ota_status:
                    if self.ota_status.state == OTAState.VERIFY_OK:
                        print("\nOTA校验成功")
                        return True
                    elif self.ota_status.state in [OTAState.VERIFY_FAIL, OTAState.ERROR]:
                        print(f"\nOTA校验失败: {self.ota_status}")
                        return False
            print("\nOTA校验超时")
            return False
        except BleakError as e:
            print(f"\nOTA校验失败: {e}")
            return False

    async def ota_apply(self):
        if not self.client or not self.client.is_connected:
            print("设备已断开连接（可能已重启完成OTA）")
            return True
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_APPLY]))
            print("OTA应用命令已发送，设备即将重启")
            return True
        except (BleakError, OSError) as e:
            err_msg = str(e).lower()
            if any(kw in err_msg for kw in ["disconnect", "disconnected", "unreachable", "取消", "cancel", "aborted", "reset"]):
                print("OTA应用成功，设备已断开连接并重启")
                return True
            print(f"OTA应用失败: {e}")
            return False

    async def ota_abort(self):
        if not self.client or not self.client.is_connected:
            return True
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_ABORT]))
            print("OTA中止命令已发送")
            return True
        except (BleakError, OSError):
            return True

    async def _ota_url_run(self, cmd_data, url_desc):
        if not self.client or not self.client.is_connected:
            print("未连接设备")
            return False
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
            await self.client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, cmd_data)
            print(f"URL OTA 已触发: {url_desc}")

            start_time = time.time()
            last_progress_pct = -1
            last_state = None

            for _ in range(600):
                await asyncio.sleep(0.5)
                if not self.ota_status:
                    continue

                state = self.ota_status.state

                if state == OTAState.CHECKING:
                    if state != last_state:
                        print("  版本检查中...")
                        last_state = state
                elif state == OTAState.CHECK_OK:
                    if state != last_state:
                        print("  发现新版本，开始下载")
                        last_state = state
                elif state == OTAState.CHECK_FAIL:
                    if state != last_state:
                        print("  固件已是最新版本，无需更新")
                        last_state = state
                    break
                elif state == OTAState.RECEIVING:
                    if self.ota_status.fw_size > 0:
                        pct = self.ota_status.progress
                        if pct != last_progress_pct:
                            self._print_progress(self.ota_status.bytes_written,
                                                 self.ota_status.fw_size,
                                                 start_time, label="下载")
                            last_progress_pct = pct
                elif state != last_state:
                    last_state = state
                    if state == OTAState.VERIFYING:
                        print("  校验中")
                    elif state == OTAState.VERIFY_OK:
                        print("  ✅ 校验成功")
                    elif state == OTAState.VERIFY_FAIL:
                        print("  ❌ 校验失败")
                        break
                    elif state == OTAState.APPLYING:
                        print("  应用中")
                    elif state == OTAState.APPLY_OK:
                        print("  ✅ OTA应用成功，设备将重启")
                        break
                    elif state == OTAState.APPLY_FAIL:
                        print("  ❌ 应用失败")
                        break
                    elif state == OTAState.ERROR:
                        if self.ota_status and self.ota_status.error_code == OTAError.NO_NETWORK:
                            print("  ❌ 设备未连接到互联网，无法进行URL OTA更新")
                        else:
                            print(f"  ❌ OTA过程发生错误: {self.ota_status}")
                        break

            return True
        except (BleakError, OSError) as e:
            err_msg = str(e).lower()
            if any(kw in err_msg for kw in ["disconnect", "disconnected", "unreachable", "取消", "cancel", "aborted", "reset"]):
                print("设备已断开连接（可能正在重启）")
                return True
            print(f"URL OTA 失败: {e}")
            return False

    async def ota_https_start_url(self, url):
        if not url or not url.strip():
            print("URL 不能为空")
            return False
        url_bytes = url.encode('utf-8')
        cmd_data = bytes([BLE_OTA_URL_CMD_START_URL]) + url_bytes
        return await self._ota_url_run(cmd_data, url)

    async def ota_https_start_default(self):
        cmd_data = bytes([BLE_OTA_URL_CMD_START_DEFAULT])
        return await self._ota_url_run(cmd_data, "默认URL")

    async def ota_update(self, fw_path, chunk_size=0):
        if not os.path.exists(fw_path):
            print(f"固件文件不存在: {fw_path}")
            return False

        print(f"\n准备升级固件: {fw_path}")
        with open(fw_path, 'rb') as f:
            fw_data = f.read()

        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version = 0x01000000

        if chunk_size == 0 and self.client and self.client.is_connected:
            mtu = self.client.mtu_size
            chunk_size = mtu - 3
            if chunk_size < 20:
                chunk_size = 244
        elif chunk_size == 0:
            chunk_size = 244

        print(f"固件大小: {fw_size} bytes")
        print(f"固件CRC: 0x{fw_crc:08X}")
        print(f"固件版本: v1.0.0")
        print(f"MTU: {self.client.mtu_size if self.client else 'N/A'}, chunk_size: {chunk_size}")

        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            self._ota_ack_event = None
            return False

        try:
            total_packages = (fw_size + chunk_size - 1) // chunk_size
            start_time = time.time()

            print(f"开始传输，共 {total_packages} 包...")
            offset = 0
            last_progress_pct = -1
            consecutive_failures = 0
            batch_count = 0

            self._ota_ack_event = asyncio.Event()

            while offset < fw_size:
                chunk = fw_data[offset:offset + chunk_size]
                if batch_count == 9:
                    self._ota_ack_event.clear()
                try:
                    success = await self.ota_send_fw_data(chunk)
                    if success:
                        offset += len(chunk)
                        batch_count += 1
                        consecutive_failures = 0
                        if batch_count >= 10:
                            batch_count = 0
                            try:
                                await asyncio.wait_for(self._ota_ack_event.wait(), timeout=5.0)
                            except asyncio.TimeoutError:
                                pass
                    else:
                        consecutive_failures += 1
                        if consecutive_failures >= 10:
                            print("\n连续发送失败，检查连接...")
                            if not self.client or not self.client.is_connected:
                                print("连接已断开，尝试重新连接...")
                                raise BleakError("连接断开")
                            consecutive_failures = 0
                        await asyncio.sleep(0.05)
                except (BleakError, OSError) as e:
                    consecutive_failures += 1
                    if consecutive_failures >= 5:
                        print(f"\n发送失败超过5次: {e}")
                        raise
                    await asyncio.sleep(0.1)

                if self.ota_status and self.ota_status.bytes_written > 0:
                    pct = int(self.ota_status.bytes_written * 100 / fw_size)
                    if pct != last_progress_pct:
                        self._print_progress(self.ota_status.bytes_written, fw_size,
                                             start_time, label="写入")
                        last_progress_pct = pct

            print()
            print(f"数据已发送，开始校验...")

            if not await self.ota_verify():
                await self.ota_abort()
                self._ota_ack_event = None
                return False

            await self.ota_apply()
            self._ota_ack_event = None
            return True

        except (BleakError, OSError) as e:
            err_msg = str(e).lower()
            if any(kw in err_msg for kw in ["disconnect", "disconnected", "unreachable", "取消", "cancel", "aborted", "reset"]):
                print("\n设备已断开连接（OTA可能已完成，设备正在重启）")
                self._ota_ack_event = None
                return True
            print(f"\nOTA升级失败: {e}")
            await self.ota_abort()
            self._ota_ack_event = None
            return False
        except Exception as e:
            print(f"\nOTA升级失败: {e}")
            await self.ota_abort()
            self._ota_ack_event = None
            return False