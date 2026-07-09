#!/usr/bin/env python3
"""
ESP32 BLE Device Manager - Flet GUI Client
跨平台蓝牙BLE设备管理器图形界面客户端 (macOS / Windows)

依赖: pip install flet bleak
"""

import asyncio
import struct
import time
import os
import sys
import zlib
import threading
from datetime import timedelta

try:
    import flet as ft
except ImportError:
    print("请先安装 flet: pip install flet")
    sys.exit(1)

try:
    from bleak import BleakClient, BleakScanner, BleakError
except ImportError:
    print("请先安装 bleak: pip install bleak")
    sys.exit(1)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ble_client.constants import *
from ble_client.models import (
    DeviceInfo, MemoryInfo, CPUInfo, FlashInfo, PartitionInfo,
    OTAStatus, OTAState, OTAError, WiFiStatus
)

WIFI_SSID_LEN = 33
WIFI_PASS_LEN = 65

EFFECT_MAP = {
    "无": BLE_LED_EFFECT_NONE,
    "呼吸灯": BLE_LED_EFFECT_BREATH,
    "闪烁": BLE_LED_EFFECT_BLINK,
    "彩虹": BLE_LED_EFFECT_RAINBOW,
    "频闪": BLE_LED_EFFECT_STROBE,
}


class BLECore:
    def __init__(self, loop):
        self.loop = loop
        self.client = None
        self.address = None
        self.device_name = None
        self.ota_status = None
        self._connected = False

    @property
    def is_connected(self):
        return self._connected and self.client is not None and self.client.is_connected

    async def scan(self, timeout=5, name_filter=None):
        devices = await BleakScanner.discover(timeout=timeout)
        result = []
        for d in devices:
            if d.name:
                if name_filter is None or d.name.startswith(name_filter):
                    rssi = getattr(d, 'rssi', None)
                    result.append({
                        "name": d.name,
                        "address": str(d.address),
                        "rssi": rssi,
                        "device": d,
                    })
        return result

    async def connect(self, device_info):
        if self.is_connected:
            await self.disconnect()
        self.address = device_info["address"]
        self.device_name = device_info["name"]
        try:
            self.client = BleakClient(device_info["device"], timeout=15)
            await self.client.connect()
            self._connected = True
            return True, self.client.mtu_size
        except BleakError as e:
            self._connected = False
            return False, str(e)

    async def disconnect(self):
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
            except Exception:
                pass
        self._connected = False
        self.client = None

    async def _read(self, uuid):
        if not self.is_connected:
            return None
        try:
            return await self.client.read_gatt_char(uuid)
        except BleakError:
            return None

    async def _write(self, uuid, data, response=True):
        if not self.is_connected:
            return False
        try:
            await self.client.write_gatt_char(uuid, data, response=response)
            return True
        except BleakError:
            return False

    async def read_device_info(self):
        data = await self._read(BLE_DM_INFO_CHAR_UUID)
        return DeviceInfo(data) if data else None

    async def read_memory_info(self):
        data = await self._read(BLE_DM_MEMORY_CHAR_UUID)
        return MemoryInfo(data) if data else None

    async def read_cpu_info(self):
        data = await self._read(BLE_DM_CPU_CHAR_UUID)
        return CPUInfo(data) if data else None

    async def read_flash_info(self):
        data = await self._read(BLE_DM_FLASH_CHAR_UUID)
        return FlashInfo(data) if data else None

    async def read_partition_info(self, index=0):
        if not self.is_connected:
            return None
        ok = await self._write(BLE_DM_PARTITION_CHAR_UUID, bytes([index]))
        if not ok:
            return None
        await asyncio.sleep(0.1)
        data = await self._read(BLE_DM_PARTITION_CHAR_UUID)
        return PartitionInfo(data) if data else None

    async def read_all_partitions(self):
        flash = await self.read_flash_info()
        if not flash:
            return []
        parts = []
        for i in range(flash.partition_count):
            p = await self.read_partition_info(i)
            if p:
                parts.append(p)
        return parts

    async def restart_device(self):
        return await self._write(BLE_DM_RESTART_CHAR_UUID, bytes([BLE_DM_CMD_RESTART]))

    async def read_temperature(self):
        data = await self._read(BLE_DM_INFO_CHAR_UUID)
        if not data or len(data) < 123:
            return None
        temp = struct.unpack('<f', data[118:122])[0]
        supported = struct.unpack('<B', data[122:123])[0]
        if not supported:
            return -999.0
        return temp

    async def led_on(self):
        return await self._write(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_ON]))

    async def led_off(self):
        return await self._write(BLE_LED_CTRL_CHAR_UUID, bytes([BLE_LED_CTRL_OFF]))

    async def led_status(self):
        data = await self._read(BLE_LED_CTRL_CHAR_UUID)
        if data:
            return data[0] == 1
        return None

    async def led_set_color(self, r, g, b):
        return await self._write(BLE_LED_COLOR_CHAR_UUID, bytes([r, g, b]))

    async def led_set_effect(self, effect, speed=50):
        return await self._write(BLE_LED_EFFECT_CHAR_UUID, bytes([effect, speed]))

    async def wifi_connect(self, ssid, password):
        if not self.is_connected:
            return False
        sb = ssid.encode('utf-8')[:WIFI_SSID_LEN - 1].ljust(WIFI_SSID_LEN, b'\x00')
        pb = password.encode('utf-8')[:WIFI_PASS_LEN - 1].ljust(WIFI_PASS_LEN, b'\x00')
        return await self._write(BLE_WIFI_CONFIG_CHAR_UUID, sb + pb)

    async def wifi_status(self):
        data = await self._read(BLE_WIFI_STATUS_CHAR_UUID)
        return WiFiStatus(data) if data else None

    async def wifi_forget(self):
        return await self._write(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_FORGET]))

    async def wifi_ntp_sync(self):
        return await self._write(BLE_WIFI_CTRL_CHAR_UUID, bytes([BLE_WIFI_CTRL_NTP_SYNC]))

    def _ota_status_handler(self, sender, data):
        try:
            self.ota_status = OTAStatus(data)
        except Exception:
            pass

    async def ota_start(self, fw_size, fw_crc, chunk_size, fw_version):
        if not self.is_connected:
            return False
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
            cmd = struct.pack('<BIIHHI', BLE_OTA_BT_CMD_START, fw_size, fw_crc, chunk_size, 0, fw_version)
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, cmd)
            await asyncio.sleep(0.5)
            if self.ota_status and self.ota_status.state != OTAState.RECEIVING:
                return False
            return True
        except BleakError:
            return False

    async def ota_send_fw_data(self, data, max_retries=3):
        if not self.is_connected:
            return False
        for attempt in range(max_retries):
            try:
                await self.client.write_gatt_char(BLE_OTA_BT_FW_DATA_CHAR_UUID, data, response=False)
                return True
            except BleakError:
                if attempt < max_retries - 1:
                    await asyncio.sleep(0.05 * (attempt + 1))
                else:
                    return False

    async def ota_verify(self):
        if not self.is_connected:
            return False
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_VERIFY]))
            for _ in range(30):
                await asyncio.sleep(0.5)
                if self.ota_status:
                    if self.ota_status.state == OTAState.VERIFY_OK:
                        return True
                    elif self.ota_status.state in [OTAState.VERIFY_FAIL, OTAState.ERROR]:
                        return False
            return False
        except BleakError:
            return False

    async def ota_apply(self):
        if not self.is_connected:
            return True
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_APPLY]))
            return True
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "cancel", "aborted", "reset"]):
                return True
            return False

    async def ota_abort(self):
        if not self.is_connected:
            return True
        try:
            await self.client.write_gatt_char(BLE_OTA_BT_CMD_CHAR_UUID, bytes([BLE_OTA_BT_CMD_ABORT]))
            return True
        except (BleakError, OSError):
            return True

    async def ota_update(self, fw_path, progress_cb=None):
        if not os.path.exists(fw_path):
            return False, "固件文件不存在"
        with open(fw_path, 'rb') as f:
            fw_data = f.read()
        fw_size = len(fw_data)
        fw_crc = zlib.crc32(fw_data) & 0xFFFFFFFF
        fw_version = 0x01000000
        mtu = self.client.mtu_size if self.client else 247
        chunk_size = max(20, mtu - 3) if mtu - 3 >= 20 else 244
        self.ota_status = None
        if not await self.ota_start(fw_size, fw_crc, chunk_size, fw_version):
            return False, "OTA启动失败"
        try:
            start_time = time.time()
            offset = 0
            sent_bytes = 0
            failures = 0
            max_inflight = 64 * 1024
            while offset < fw_size:
                if self.ota_status and self.ota_status.bytes_written > 0:
                    if sent_bytes - self.ota_status.bytes_written > max_inflight:
                        if progress_cb:
                            progress_cb(self.ota_status.bytes_written, fw_size, sent_bytes, start_time)
                        await asyncio.sleep(0.005)
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
                        if not self.is_connected:
                            return False, "连接已断开"
                        failures = 0
                    await asyncio.sleep(0.05)
                if self.ota_status and self.ota_status.bytes_written > 0 and progress_cb:
                    progress_cb(self.ota_status.bytes_written, fw_size, sent_bytes, start_time)
            elapsed = time.time() - start_time
            speed = fw_size / elapsed if elapsed > 0 else 1.5
            remain = fw_size - (self.ota_status.bytes_written if self.ota_status else 0)
            expect = remain / speed if speed > 0 else 60
            timeout = max(120, int(expect * 2))
            for _ in range(timeout):
                await asyncio.sleep(1)
                if self.ota_status:
                    if progress_cb:
                        progress_cb(self.ota_status.bytes_written, fw_size, fw_size, start_time)
                    if self.ota_status.bytes_written >= fw_size:
                        break
            else:
                await self.ota_abort()
                return False, "设备写入超时"
            if not await self.ota_verify():
                await self.ota_abort()
                return False, "OTA校验失败"
            await self.ota_apply()
            return True, "OTA升级成功"
        except (BleakError, OSError) as e:
            msg = str(e).lower()
            if any(k in msg for k in ["disconnect", "disconnected", "unreachable", "cancel", "aborted", "reset"]):
                return True, "设备已断开（OTA可能已完成）"
            await self.ota_abort()
            return False, str(e)
        except Exception as e:
            await self.ota_abort()
            return False, str(e)

    async def ota_url_start(self, url=None):
        if not self.is_connected:
            return False, "未连接设备"
        try:
            await self.client.start_notify(BLE_OTA_STATUS_CHAR_UUID, self._ota_status_handler)
            if url:
                cmd = bytes([BLE_OTA_URL_CMD_START_URL]) + url.encode('utf-8')
            else:
                cmd = bytes([BLE_OTA_URL_CMD_START_DEFAULT])
            await self.client.write_gatt_char(BLE_OTA_URL_CHAR_UUID, cmd)
            return True, "URL OTA已触发"
        except BleakError as e:
            return False, str(e)


class BLEApp:
    def __init__(self):
        self.loop = asyncio.new_event_loop()
        self.ble = BLECore(self.loop)
        self.devices = []
        self._scan_lock = False
        self._ota_running = False
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()

    def _run_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def _run_async(self, coro, callback=None):
        def _done(fut):
            try:
                result = fut.result()
                if callback:
                    callback(result)
            except Exception as e:
                if callback:
                    callback(e)
        fut = asyncio.run_coroutine_threadsafe(coro, self.loop)
        fut.add_done_callback(_done)
        return fut

    def main(self, page: ft.Page):
        page.title = "BLE Device Manager"
        page.window.width = 1020
        page.window.height = 720
        page.window.min_width = 900
        page.window.min_height = 600
        page.theme_mode = ft.ThemeMode.SYSTEM
        page.padding = 0
        page.spacing = 0
        page.fonts = {
            "mono": "Consolas" if sys.platform == "win32" else "SF Mono",
        }
        page.theme = ft.Theme(
            color_scheme_seed=ft.Colors.BLUE,
            font_family="system-ui",
        )
        page.bgcolor = ft.Colors.with_opacity(0.03, "black")

        self.page = page
        self._build_ui()

    def _build_ui(self):
        page = self.page

        self.status_badge = ft.Container(
            content=ft.Row([
                ft.Icon(ft.Icons.BLUETOOTH_DISABLED, size=16, color=ft.Colors.RED_400),
                ft.Text("未连接", size=12, color=ft.Colors.RED_400, weight=ft.FontWeight.W_500),
            ], spacing=6),
            bgcolor=ft.Colors.with_opacity(0.08, ft.Colors.RED_400),
            border_radius=20, padding=ft.padding.symmetric(12, 16),
        )

        page.appbar = ft.AppBar(
            title=ft.Text("BLE Device Manager", size=16, weight=ft.FontWeight.W_600),
            center_title=False,
            bgcolor=ft.Colors.SURFACE,
            elevation=0,
            actions=[self.status_badge],
        )

        self.device_list = ft.ListView(spacing=2, expand=True)
        self.filter_field = ft.TextField(
            label="设备名过滤", value="BLE-SRV", dense=True,
            prefix_icon=ft.Icons.FILTER_LIST, text_size=13,
            border_radius=10, height=44,
        )
        self.scan_btn = ft.ElevatedButton(
            "扫描设备", icon=ft.Icons.SEARCH, on_click=self._do_scan,
            style=ft.ButtonStyle(bgcolor=ft.Colors.BLUE, color=ft.Colors.WHITE, shape=ft.RoundedRectangleBorder(radius=10)),
        )
        self.connect_btn = ft.ElevatedButton(
            "连接", icon=ft.Icons.LINK, on_click=self._do_connect, disabled=True,
            style=ft.ButtonStyle(bgcolor=ft.Colors.GREEN, color=ft.Colors.WHITE, shape=ft.RoundedRectangleBorder(radius=10)),
        )
        self.disconnect_btn = ft.ElevatedButton(
            "断开", icon=ft.Icons.LINK_OFF, on_click=self._do_disconnect, disabled=True,
            style=ft.ButtonStyle(bgcolor=ft.Colors.RED_700, color=ft.Colors.WHITE, shape=ft.RoundedRectangleBorder(radius=10)),
        )

        left_panel = ft.Container(
            content=ft.Column([
                self.filter_field,
                ft.Divider(height=4, color=ft.Colors.TRANSPARENT),
                ft.Container(
                    content=self.device_list,
                    border=ft.border.all(1, ft.Colors.OUTLINE_VARIANT),
                    border_radius=12,
                    bgcolor=ft.Colors.SURFACE_CONTAINER_LOW,
                    expand=True,
                    padding=6,
                ),
                ft.Divider(height=4, color=ft.Colors.TRANSPARENT),
                ft.Row([self.scan_btn, self.connect_btn, self.disconnect_btn], spacing=8, alignment=ft.MainAxisAlignment.CENTER),
            ], spacing=0, expand=True),
            width=300, padding=ft.padding.all(16),
            bgcolor=ft.Colors.SURFACE,
            border_radius=ft.border_radius.only(top_right=16, bottom_right=16),
            shadow=ft.BoxShadow(blur_radius=8, color=ft.Colors.with_opacity(0.06, "black"), offset=ft.Offset(2, 0)),
        )

        self.tabs = ft.Tabs(
            selected_index=0,
            animation_duration=200,
            expand=True,
            tabs=[
                ft.Tab(text="设备信息", icon=ft.Icons.INFO_OUTLINE, content=self._build_info_tab()),
                ft.Tab(text="LED 控制", icon=ft.Icons.LIGHTBULB_OUTLINE, content=self._build_led_tab()),
                ft.Tab(text="WiFi", icon=ft.Icons.WIFI, content=self._build_wifi_tab()),
                ft.Tab(text="OTA 升级", icon=ft.Icons.SYSTEM_UPDATE, content=self._build_ota_tab()),
            ],
        )

        self.log_view = ft.ListView(spacing=1, expand=True, auto_scroll=True)
        self.log_container = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Icon(ft.Icons.TERMINAL, size=16, color=ft.Colors.ON_SURFACE_VARIANT),
                    ft.Text("数据日志", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE_VARIANT),
                    ft.Container(expand=True),
                    ft.IconButton(ft.Icons.DELETE_OUTLINE, icon_size=16, on_click=self._clear_log, tooltip="清空"),
                ], spacing=6),
                ft.Container(
                    content=self.log_view,
                    bgcolor="#1e1e2e",
                    border_radius=10,
                    padding=10,
                    expand=True,
                ),
            ], spacing=8, expand=True),
            padding=ft.padding.all(16),
            bgcolor=ft.Colors.SURFACE,
            border_radius=ft.border_radius.only(top_left=16),
            shadow=ft.BoxShadow(blur_radius=8, color=ft.Colors.with_opacity(0.06, "black"), offset=ft.Offset(-2, 0)),
            height=200,
        )

        right_panel = ft.Column([
            ft.Container(content=self.tabs, padding=ft.padding.only(left=16, top=16, right=16), expand=True),
            self.log_container,
        ], spacing=0, expand=True)

        page.add(
            ft.Row([left_panel, right_panel], spacing=0, expand=True)
        )

    def _build_info_tab(self):
        self.info_display = ft.TextField(
            multiline=True, read_only=True, min_lines=8, max_lines=20,
            border_radius=12, text_size=12, font_family="mono",
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOW,
            label="设备数据",
        )
        return ft.Container(
            content=ft.Column([
                ft.Row([
                    self._action_btn("设备信息", ft.Icons.INFO, self._read_info),
                    self._action_btn("内存", ft.Icons.MEMORY, self._read_memory),
                    self._action_btn("CPU", ft.Icons.SPEED, self._read_cpu),
                    self._action_btn("Flash", ft.Icons.STORAGE, self._read_flash),
                    self._action_btn("分区", ft.Icons.FOLDER_OUTLINE, self._read_partitions),
                    self._action_btn("温度", ft.Icons.THERMOSTAT, self._read_temp),
                    self._action_btn("重启", ft.Icons.RESTART_ALT, self._do_restart, color=ft.Colors.RED),
                ], spacing=6, wrap=True),
                ft.Container(height=8),
                self.info_display,
            ], spacing=0, scroll=ft.ScrollMode.AUTO),
            padding=16,
        )

    def _build_led_tab(self):
        self.led_status_text = ft.Text("状态: 未知", size=13, color=ft.Colors.ON_SURFACE_VARIANT)
        self.r_slider = ft.Slider(min=0, max=255, value=255, label="{value}", width=200, on_change=self._color_changed)
        self.g_slider = ft.Slider(min=0, max=255, value=0, label="{value}", width=200, on_change=self._color_changed)
        self.b_slider = ft.Slider(min=0, max=255, value=0, label="{value}", width=200, on_change=self._color_changed)
        self.color_box = ft.Container(width=50, height=50, bgcolor="#ff0000", border_radius=8, border=ft.border.all(1, ft.Colors.OUTLINE_VARIANT))
        self.effect_dropdown = ft.Dropdown(
            options=[ft.dropdown.Option(k) for k in EFFECT_MAP.keys()],
            value="无", width=140, dense=True, border_radius=10,
        )
        self.speed_slider = ft.Slider(min=1, max=255, value=50, label="{value}", width=180)

        return ft.Container(
            content=ft.Column([
                ft.Row([
                    self._action_btn("开", ft.Icons.LIGHTBULB, self._led_on, color=ft.Colors.GREEN),
                    self._action_btn("关", ft.Icons.LIGHTBULB_OUTLINE, self._led_off, color=ft.Colors.RED),
                    self._action_btn("查询状态", ft.Icons.HELP_OUTLINE, self._led_status),
                    self.led_status_text,
                ], spacing=8),
                ft.Divider(),
                ft.Text("颜色设置", size=14, weight=ft.FontWeight.W_600),
                ft.Row([
                    ft.Column([
                        ft.Row([ft.Text("R", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.RED), self.r_slider]),
                        ft.Row([ft.Text("G", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.GREEN), self.g_slider]),
                        ft.Row([ft.Text("B", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.BLUE), self.b_slider]),
                    ], spacing=4),
                    self.color_box,
                ], spacing=20),
                self._action_btn("应用颜色", ft.Icons.PALETTE, self._led_set_color),
                ft.Divider(),
                ft.Text("特效设置", size=14, weight=ft.FontWeight.W_600),
                ft.Row([
                    ft.Text("特效:", size=13), self.effect_dropdown,
                    ft.Text("速度:", size=13), self.speed_slider,
                ], spacing=8),
                self._action_btn("应用特效", ft.Icons.AUTO_AWESOME, self._led_set_effect),
            ], spacing=12, scroll=ft.ScrollMode.AUTO),
            padding=16,
        )

    def _build_wifi_tab(self):
        self.ssid_field = ft.TextField(label="SSID", dense=True, border_radius=10, width=300)
        self.pass_field = ft.TextField(label="密码", dense=True, border_radius=10, width=300, password=True, can_reveal_password=True)
        self.wifi_display = ft.TextField(
            multiline=True, read_only=True, min_lines=4, max_lines=8,
            border_radius=12, text_size=12, font_family="mono",
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOW, label="WiFi 状态",
        )
        return ft.Container(
            content=ft.Column([
                ft.Row([
                    self.ssid_field, self.pass_field,
                ], spacing=12),
                ft.Row([
                    self._action_btn("连接WiFi", ft.Icons.WIFI, self._wifi_connect, color=ft.Colors.BLUE),
                    self._action_btn("查询状态", ft.Icons.HELP_OUTLINE, self._wifi_status),
                    self._action_btn("忘记网络", ft.Icons.WIFI_OFF, self._wifi_forget, color=ft.Colors.RED),
                    self._action_btn("NTP同步", ft.Icons.SCHEDULE, self._wifi_ntp),
                ], spacing=8),
                ft.Divider(),
                self.wifi_display,
            ], spacing=12, scroll=ft.ScrollMode.AUTO),
            padding=16,
        )

    def _build_ota_tab(self):
        self.fw_path_field = ft.TextField(
            label="固件文件路径", dense=True, border_radius=10,
            suffix=ft.IconButton(ft.Icons.FOLDER_OPEN, on_click=self._pick_firmware, icon_size=20),
        )
        self.ota_url_field = ft.TextField(label="固件 URL", dense=True, border_radius=10)
        self.ota_progress = ft.ProgressBar(width=400, bar_height=8, value=0, bgcolor=ft.Colors.SURFACE_CONTAINER_HIGHEST)
        self.ota_status_text = ft.Text("", size=12, color=ft.Colors.ON_SURFACE_VARIANT)
        self.ota_bt_btn = self._action_btn("开始蓝牙OTA升级", ft.Icons.UPLOAD_FILE, self._do_ota_bt)
        self.ota_abort_btn = ft.ElevatedButton(
            "中止OTA", icon=ft.Icons.STOP, on_click=self._do_ota_abort, disabled=True,
            style=ft.ButtonStyle(bgcolor=ft.Colors.RED, color=ft.Colors.WHITE, shape=ft.RoundedRectangleBorder(radius=10)),
        )
        return ft.Container(
            content=ft.Column([
                ft.Text("蓝牙 OTA", size=14, weight=ft.FontWeight.W_600),
                self.fw_path_field,
                self.ota_bt_btn,
                ft.Divider(),
                ft.Text("URL OTA", size=14, weight=ft.FontWeight.W_600),
                self.ota_url_field,
                ft.Row([
                    self._action_btn("URL升级", ft.Icons.CLOUD_DOWNLOAD, self._do_ota_url),
                    self._action_btn("默认URL升级", ft.Icons.CLOUD, self._do_ota_default),
                ], spacing=8),
                ft.Divider(),
                ft.Row([self.ota_abort_btn], spacing=8),
                self.ota_progress,
                self.ota_status_text,
            ], spacing=12, scroll=ft.ScrollMode.AUTO),
            padding=16,
        )

    def _action_btn(self, text, icon, on_click, color=ft.Colors.BLUE):
        return ft.ElevatedButton(
            text, icon=icon, on_click=on_click,
            style=ft.ButtonStyle(
                bgcolor=color, color=ft.Colors.WHITE,
                shape=ft.RoundedRectangleBorder(radius=10),
                text_style=ft.TextStyle(size=12),
            ),
        )

    def _log(self, msg, level="info"):
        colors = {"info": ft.Colors.BLUE_200, "success": ft.Colors.GREEN_300,
                  "error": ft.Colors.RED_300, "warn": ft.Colors.ORANGE_300}
        icons = {"info": ft.Icons.INFO, "success": ft.Icons.CHECK_CIRCLE,
                 "error": ft.Icons.ERROR, "warn": ft.Icons.WARNING}
        c = colors.get(level, ft.Colors.BLUE_200)
        ts = time.strftime("%H:%M:%S")
        self.log_view.controls.append(
            ft.Row([
                ft.Text(f"[{ts}]", size=11, color=ft.Colors.GREY_500, font_family="mono"),
                ft.Icon(icons.get(level, ft.Icons.INFO), size=14, color=c),
                ft.Text(msg, size=11, color=c, font_family="mono", expand=True),
            ], spacing=6)
        )
        if len(self.log_view.controls) > 500:
            self.log_view.controls = self.log_view.controls[-300:]
        self.page.update()

    def _clear_log(self, e=None):
        self.log_view.controls.clear()
        self.page.update()

    def _update_connection_ui(self, connected):
        if connected:
            self.status_badge.content = ft.Row([
                ft.Icon(ft.Icons.BLUETOOTH_CONNECTED, size=16, color=ft.Colors.GREEN),
                ft.Text(f"已连接: {self.ble.device_name}", size=12, color=ft.Colors.GREEN, weight=ft.FontWeight.W_500),
            ], spacing=6)
            self.status_badge.bgcolor = ft.Colors.with_opacity(0.08, ft.Colors.GREEN)
            self.connect_btn.disabled = True
            self.disconnect_btn.disabled = False
        else:
            self.status_badge.content = ft.Row([
                ft.Icon(ft.Icons.BLUETOOTH_DISABLED, size=16, color=ft.Colors.RED_400),
                ft.Text("未连接", size=12, color=ft.Colors.RED_400, weight=ft.FontWeight.W_500),
            ], spacing=6)
            self.status_badge.bgcolor = ft.Colors.with_opacity(0.08, ft.Colors.RED_400)
            self.connect_btn.disabled = True
            self.disconnect_btn.disabled = True
        self.page.update()

    def _check_connected(self):
        if not self.ble.is_connected:
            self._log("未连接设备，请先连接", "error")
            return False
        return True

    def _do_scan(self, e=None):
        if self._scan_lock:
            return
        self._scan_lock = True
        self.scan_btn.disabled = True
        self.scan_btn.text = "扫描中..."
        self.page.update()
        self._log("开始扫描BLE设备...", "info")
        nf = self.filter_field.value.strip() or None

        def on_done(result):
            self._scan_lock = False
            self.scan_btn.disabled = False
            self.scan_btn.text = "扫描设备"
            if isinstance(result, Exception):
                self._log(f"扫描失败: {result}", "error")
                self.page.update()
                return
            self.devices = result
            self.device_list.controls.clear()
            if not result:
                self._log("未发现BLE设备", "warn")
                self.page.update()
                return
            for i, d in enumerate(result):
                rssi_str = f"  RSSI: {d['rssi']}dBm" if d['rssi'] is not None else ""
                self.device_list.controls.append(
                    ft.ListTile(
                        leading=ft.Icon(ft.Icons.BLUETOOTH, color=ft.Colors.BLUE),
                        title=ft.Text(d['name'], size=13, weight=ft.FontWeight.W_500),
                        subtitle=ft.Text(f"{d['address']}{rssi_str}", size=11, color=ft.Colors.ON_SURFACE_VARIANT),
                        on_click=self._on_device_click,
                        data=i,
                        style=ft.ListTileStyle.LIST,
                    )
                )
            self._log(f"发现 {len(result)} 个设备", "success")
            self.page.update()

        self._run_async(self.ble.scan(timeout=5, name_filter=nf), on_done)

    def _on_device_click(self, e):
        idx = e.control.data
        if idx < len(self.devices):
            self.ble.selected_device_info = self.devices[idx]
            self.connect_btn.disabled = self.ble.is_connected
            self._log(f"已选择: {self.devices[idx]['name']}", "info")
            self.page.update()

    def _do_connect(self, e=None):
        dev = getattr(self.ble, 'selected_device_info', None)
        if not dev:
            self._log("请先选择一个设备", "warn")
            return
        if self.ble.is_connected:
            self._log("已连接设备，请先断开", "warn")
            return
        self.connect_btn.disabled = True
        self.connect_btn.text = "连接中..."
        self.page.update()
        self._log(f"正在连接 {dev['name']}...", "info")

        def on_done(result):
            self.connect_btn.text = "连接"
            if isinstance(result, Exception):
                self._update_connection_ui(False)
                self._log(f"连接异常: {result}", "error")
                return
            ok, mtu_or_err = result
            if ok:
                self._log(f"连接成功 (MTU={mtu_or_err})", "success")
                self._update_connection_ui(True)
            else:
                self._log(f"连接失败: {mtu_or_err}", "error")
                self._update_connection_ui(False)

        self._run_async(self.ble.connect(dev), on_done)

    def _do_disconnect(self, e=None):
        self._log("正在断开连接...", "info")

        def on_done(result):
            self._update_connection_ui(False)
            self._log("已断开连接", "info")

        self._run_async(self.ble.disconnect(), on_done)

    def _read_info(self, e=None):
        if not self._check_connected():
            return
        self._log("读取设备信息...", "info")
        def cb(r):
            if isinstance(r, Exception) or r is None:
                self._log("读取设备信息失败", "error"); return
            self.info_display.value = str(r)
            self._log("设备信息读取成功", "success"); self.page.update()
        self._run_async(self.ble.read_device_info(), cb)

    def _read_memory(self, e=None):
        if not self._check_connected():
            return
        self._log("读取内存信息...", "info")
        def cb(r):
            if isinstance(r, Exception) or r is None:
                self._log("读取内存信息失败", "error"); return
            self.info_display.value = str(r)
            self._log("内存信息读取成功", "success"); self.page.update()
        self._run_async(self.ble.read_memory_info(), cb)

    def _read_cpu(self, e=None):
        if not self._check_connected():
            return
        self._log("读取CPU信息...", "info")
        def cb(r):
            if isinstance(r, Exception) or r is None:
                self._log("读取CPU信息失败", "error"); return
            self.info_display.value = str(r)
            self._log("CPU信息读取成功", "success"); self.page.update()
        self._run_async(self.ble.read_cpu_info(), cb)

    def _read_flash(self, e=None):
        if not self._check_connected():
            return
        self._log("读取Flash信息...", "info")
        def cb(r):
            if isinstance(r, Exception) or r is None:
                self._log("读取Flash信息失败", "error"); return
            self.info_display.value = str(r)
            self._log("Flash信息读取成功", "success"); self.page.update()
        self._run_async(self.ble.read_flash_info(), cb)

    def _read_partitions(self, e=None):
        if not self._check_connected():
            return
        self._log("读取分区信息...", "info")
        def cb(r):
            if isinstance(r, Exception):
                self._log(f"读取分区信息失败: {r}", "error"); return
            if r:
                lines = [f"分区列表 ({len(r)} 个):\n"]
                for i, p in enumerate(r):
                    lines.append(f"\n--- 分区 {i} ---\n{p}")
                self.info_display.value = "\n".join(lines)
                self._log(f"读取到 {len(r)} 个分区", "success")
            else:
                self._log("读取分区信息失败", "error")
            self.page.update()
        self._run_async(self.ble.read_all_partitions(), cb)

    def _read_temp(self, e=None):
        if not self._check_connected():
            return
        self._log("读取温度...", "info")
        def cb(r):
            if isinstance(r, Exception):
                self._log(f"读取温度失败: {r}", "error"); return
            if r is None:
                self._log("读取温度失败", "error")
            elif r <= -900.0:
                self.info_display.value = "温度传感器: 不支持或未启用"
                self._log("温度传感器不支持", "warn")
            else:
                self.info_display.value = f"当前温度: {r:.2f}°C"
                self._log(f"温度: {r:.2f}°C", "success")
            self.page.update()
        self._run_async(self.ble.read_temperature(), cb)

    def _do_restart(self, e=None):
        if not self._check_connected():
            return
        def _confirm(dlg):
            self.page.close(dlg)
            self._log("发送重启命令...", "warn")
            def cb(r):
                if isinstance(r, Exception):
                    self._log(f"重启失败: {r}", "error"); return
                if r:
                    self._log("重启命令已发送，设备即将重启", "success")
                    self.page.run_thread(lambda: (time.sleep(3), self._update_connection_ui(False)))
                else:
                    self._log("发送重启命令失败", "error")
            self._run_async(self.ble.restart_device(), cb)

        dlg = ft.AlertDialog(
            title=ft.Text("确认重启"),
            content=ft.Text("确定要重启设备吗？"),
            actions=[
                ft.TextButton("取消", on_click=lambda e: self.page.close(dlg)),
                ft.TextButton("重启", on_click=lambda e: _confirm(dlg)),
            ],
        )
        self.page.open(dlg)

    def _color_changed(self, e=None):
        try:
            r, g, b = int(self.r_slider.value), int(self.g_slider.value), int(self.b_slider.value)
            self.color_box.bgcolor = f"#{r:02x}{g:02x}{b:02x}"
            self.page.update()
        except Exception:
            pass

    def _led_on(self, e=None):
        if not self._check_connected():
            return
        def cb(r): self._log("LED已打开" if r else "打开LED失败", "success" if r else "error")
        self._run_async(self.ble.led_on(), cb)

    def _led_off(self, e=None):
        if not self._check_connected():
            return
        def cb(r): self._log("LED已关闭" if r else "关闭LED失败", "success" if r else "error")
        self._run_async(self.ble.led_off(), cb)

    def _led_status(self, e=None):
        if not self._check_connected():
            return
        def cb(r):
            if r is None: self._log("读取LED状态失败", "error"); self.led_status_text.value = "状态: 未知"
            elif r: self._log("LED状态: 开", "success"); self.led_status_text.value = "状态: 开 ●"
            else: self._log("LED状态: 关", "info"); self.led_status_text.value = "状态: 关 ○"
            self.page.update()
        self._run_async(self.ble.led_status(), cb)

    def _led_set_color(self, e=None):
        if not self._check_connected():
            return
        r, g, b = int(self.r_slider.value), int(self.g_slider.value), int(self.b_slider.value)
        def cb(res):
            self._log(f"LED颜色已设置: #{r:02x}{g:02x}{b:02x}" if res else "设置LED颜色失败", "success" if res else "error")
        self._run_async(self.ble.led_set_color(r, g, b), cb)

    def _led_set_effect(self, e=None):
        if not self._check_connected():
            return
        name = self.effect_dropdown.value
        eid = EFFECT_MAP.get(name, 0)
        speed = int(self.speed_slider.value)
        def cb(r):
            self._log(f"LED特效已设置: {name}, 速度: {speed}" if r else "设置LED特效失败", "success" if r else "error")
        self._run_async(self.ble.led_set_effect(eid, speed), cb)

    def _wifi_connect(self, e=None):
        if not self._check_connected():
            return
        ssid = self.ssid_field.value.strip() if self.ssid_field.value else ""
        if not ssid:
            self._log("请输入WiFi SSID", "warn"); return
        pwd = self.pass_field.value or ""
        self._log(f"发送WiFi连接命令 (SSID: {ssid})...", "info")
        def cb(r): self._log(f"WiFi连接命令已发送" if r else "发送WiFi连接命令失败", "success" if r else "error")
        self._run_async(self.ble.wifi_connect(ssid, pwd), cb)

    def _wifi_status(self, e=None):
        if not self._check_connected():
            return
        def cb(r):
            if isinstance(r, Exception) or r is None:
                self._log("读取WiFi状态失败", "error"); return
            self.wifi_display.value = str(r)
            self._log("WiFi状态读取成功", "success"); self.page.update()
        self._run_async(self.ble.wifi_status(), cb)

    def _wifi_forget(self, e=None):
        if not self._check_connected():
            return
        def cb(r): self._log("WiFi配置已清除" if r else "清除WiFi配置失败", "success" if r else "error")
        self._run_async(self.ble.wifi_forget(), cb)

    def _wifi_ntp(self, e=None):
        if not self._check_connected():
            return
        def cb(r): self._log("NTP时间同步命令已发送" if r else "NTP同步失败", "success" if r else "error")
        self._run_async(self.ble.wifi_ntp_sync(), cb)

    def _pick_firmware(self, e=None):
        def _on_result(e):
            if e.data and e.data.get("path"):
                self.fw_path_field.value = e.data["path"]
                self.page.update()
        self.page.open(ft.FilePicker(on_result=_on_result))

    def _do_ota_bt(self, e=None):
        if not self._check_connected():
            return
        fw_path = self.fw_path_field.value.strip() if self.fw_path_field.value else ""
        if not fw_path or not os.path.exists(fw_path):
            self._log("请选择有效的固件文件", "warn"); return
        self._ota_running = True
        self.ota_bt_btn.disabled = True
        self.ota_abort_btn.disabled = False
        self.ota_progress.value = 0
        self._log(f"开始蓝牙OTA升级: {os.path.basename(fw_path)}", "info")
        self.page.update()

        def progress_cb(written, total, sent, start_time):
            pct = min(100, int(written * 100 / total)) if total > 0 else 0
            self.ota_progress.value = pct / 100.0
            elapsed = time.time() - start_time
            speed = written / elapsed if elapsed > 0 else 0
            s = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            self.ota_status_text.value = f"进度: {pct}% | 速度: {s} | {written}/{total} bytes"
            self.page.update()

        def on_done(result):
            self._ota_running = False
            self.ota_bt_btn.disabled = False
            self.ota_abort_btn.disabled = True
            if isinstance(result, Exception):
                self._log(f"OTA异常: {result}", "error"); self.page.update(); return
            ok, msg = result
            if ok:
                self._log(f"OTA升级成功: {msg}", "success")
                self.ota_progress.value = 1.0
                self.ota_status_text.value = "OTA升级成功"
            else:
                self._log(f"OTA升级失败: {msg}", "error")
                self.ota_status_text.value = f"OTA失败: {msg}"
            self.page.update()

        self._run_async(self.ble.ota_update(fw_path, progress_cb=progress_cb), on_done)

    def _do_ota_url(self, e=None):
        if not self._check_connected():
            return
        url = self.ota_url_field.value.strip() if self.ota_url_field.value else ""
        if not url:
            self._log("请输入固件URL", "warn"); return
        self._log(f"开始URL OTA: {url}", "info")
        def cb(r):
            if isinstance(r, Exception):
                self._log(f"URL OTA异常: {r}", "error"); return
            ok, msg = r
            self._log(f"URL OTA已触发: {msg}" if ok else f"URL OTA失败: {msg}", "success" if ok else "error")
        self._run_async(self.ble.ota_url_start(url), cb)

    def _do_ota_default(self, e=None):
        if not self._check_connected():
            return
        self._log("开始默认URL OTA...", "info")
        def cb(r):
            if isinstance(r, Exception):
                self._log(f"URL OTA异常: {r}", "error"); return
            ok, msg = r
            self._log(f"默认URL OTA已触发: {msg}" if ok else f"默认URL OTA失败: {msg}", "success" if ok else "error")
        self._run_async(self.ble.ota_url_start(None), cb)

    def _do_ota_abort(self, e=None):
        self._log("中止OTA...", "warn")
        def cb(r):
            self._ota_running = False
            self.ota_bt_btn.disabled = False
            self.ota_abort_btn.disabled = True
            self._log("OTA已中止", "warn"); self.page.update()
        self._run_async(self.ble.ota_abort(), cb)

    def run(self):
        ft.app(target=self.main)


if __name__ == "__main__":
    app = BLEApp()
    app.run()
