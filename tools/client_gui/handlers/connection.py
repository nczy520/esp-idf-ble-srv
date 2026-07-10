"""
连接管理模块
处理蓝牙设备的扫描、连接和断开
"""

import asyncio
import flet as ft

from client_gui.handlers.base import BaseHandler


class ConnectionHandler(BaseHandler):
    """连接管理处理类"""

    def __init__(self, app):
        super().__init__(app)
        self.devices = []
        self.scan_lock = False
        self._device_rssi_controls = {}
        self._device_connect_btns = {}

    def update_connection_ui(self, connected):
        """更新连接状态UI"""
        device = getattr(self.ble, 'selected_device_info', None)
        connected_address = device.get('address') if device else None
        
        if connected:
            self.ui.status_badge.content = ft.Row([
                ft.Icon(ft.Icons.CIRCLE, size=10, color=ft.Colors.GREEN),
                ft.Text("已连接", size=12, color=ft.Colors.GREEN, weight=ft.FontWeight.W_500),
            ], spacing=8, alignment=ft.MainAxisAlignment.CENTER)
            self.ui.status_badge.bgcolor = ft.Colors.with_opacity(0.1, ft.Colors.GREEN)
            self.ui.status_badge.border = ft.border.BorderSide(1, ft.Colors.with_opacity(0.2, ft.Colors.GREEN))
            self.ui.connect_toggle_btn.content = "断开"
            self.ui.connect_toggle_btn.icon = ft.Icons.LINK_OFF_ROUNDED
            self.ui.connect_toggle_btn.bgcolor = ft.Colors.RED_700
            self.ui.connect_toggle_btn.disabled = False
            self._set_overlays_visible(False)
        else:
            self.ui.status_badge.content = ft.Row([
                ft.Icon(ft.Icons.CIRCLE, size=10, color=ft.Colors.RED_400),
                ft.Text("未连接", size=12, color=ft.Colors.RED_400, weight=ft.FontWeight.W_500),
            ], spacing=8, alignment=ft.MainAxisAlignment.CENTER)
            self.ui.status_badge.bgcolor = ft.Colors.with_opacity(0.1, ft.Colors.RED_400)
            self.ui.status_badge.border = ft.border.BorderSide(1, ft.Colors.with_opacity(0.2, ft.Colors.RED_400))
            self.ui.connect_toggle_btn.content = "连接"
            self.ui.connect_toggle_btn.icon = ft.Icons.LINK_ROUNDED
            self.ui.connect_toggle_btn.bgcolor = ft.Colors.BLUE
            self.ui.connect_toggle_btn.disabled = (device is None)
            self._set_overlays_visible(True)
        
        for addr, btn in self._device_connect_btns.items():
            if connected and addr == connected_address:
                btn.icon = ft.Icons.LINK_OFF_ROUNDED
                btn.icon_color = ft.Colors.RED
                btn.tooltip = "断开连接"
                btn.disabled = False
            elif connected:
                btn.icon = ft.Icons.LINK_ROUNDED
                btn.icon_color = ft.Colors.GREY
                btn.tooltip = "请先断开当前设备"
                btn.disabled = True
            else:
                btn.icon = ft.Icons.LINK_ROUNDED
                btn.icon_color = ft.Colors.GREEN
                btn.tooltip = "连接此设备"
                btn.disabled = False
        
        self.page.update()

    def _get_rssi_color(self, rssi):
        """根据信号强度获取颜色"""
        if rssi >= -60:
            return ft.Colors.GREEN
        elif rssi >= -80:
            return ft.Colors.ORANGE
        else:
            return ft.Colors.RED

    def _get_rssi_icon(self, rssi):
        """根据信号强度获取图标"""
        if rssi >= -60:
            return ft.Icons.SIGNAL_CELLULAR_ALT
        elif rssi >= -80:
            return ft.Icons.SIGNAL_CELLULAR_ALT_2_BAR
        else:
            return ft.Icons.SIGNAL_CELLULAR_ALT_1_BAR

    def _set_overlays_visible(self, visible):
        """设置所有遮罩层的可见性"""
        self.ui.info_overlay.visible = visible
        self.ui.led_overlay.visible = visible
        self.ui.wifi_overlay.visible = visible
        self.ui.ota_overlay.visible = visible

    def _add_device_to_list(self, device, index):
        """添加设备到列表"""
        rssi = device.get('rssi')
        if rssi is not None:
            rssi_color = self._get_rssi_color(rssi)
            rssi_icon = self._get_rssi_icon(rssi)
            rssi_text = f"  {rssi}dBm"
        else:
            rssi_color = ft.Colors.GREY
            rssi_icon = ft.Icons.SIGNAL_CELLULAR_OFF
            rssi_text = "  未知"
        
        rssi_icon_ctrl = ft.Icon(rssi_icon, size=12, color=rssi_color)
        rssi_text_ctrl = ft.Text(rssi_text, size=10, color=rssi_color)
        
        self._device_rssi_controls[device['address']] = {
            'icon': rssi_icon_ctrl,
            'text': rssi_text_ctrl
        }
        
        connect_btn = ft.IconButton(
            ft.Icons.LINK_ROUNDED,
            icon_color=ft.Colors.GREEN,
            tooltip="连接此设备",
            icon_size=18,
            on_click=self._on_connect_device_click,
            data=index,
        )
        
        self._device_connect_btns[device['address']] = connect_btn
        
        self.ui.device_list.controls.append(
            ft.ListTile(
                leading=ft.Icon(ft.Icons.BLUETOOTH, color=ft.Colors.BLUE, size=20),
                title=ft.Row([
                    ft.Column([
                        ft.Text(device['name'], size=13, weight=ft.FontWeight.W_500),
                        ft.Row([
                            ft.Text(device['address'], size=10, color=ft.Colors.ON_SURFACE_VARIANT),
                            rssi_icon_ctrl,
                            rssi_text_ctrl,
                        ], spacing=4),
                    ], spacing=0, expand=True),
                    ft.Container(width=8),
                    connect_btn,
                ], spacing=0, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                on_click=self.handle_device_click,
                data=index,
                style=ft.ListTileStyle.LIST,
                dense=True,
            )
        )

    def handle_scan(self, event=None):
        """扫描BLE设备"""
        if self.scan_lock:
            return
        self.scan_lock = True
        self.ui.scan_btn.disabled = True
        self.ui.scan_btn.content = "扫描中..."
        self.ui.scan_btn.icon = ft.Icons.SYNC
        self.page.update()
        self.log("开始扫描BLE设备...", "info")
        name_filter = self.ui.filter_field.value.strip() or None
        timeout = self.ui.scan_timeout_dropdown.get_scan_timeout() if hasattr(self.ui.scan_timeout_dropdown, 'get_scan_timeout') else 5

        # 清空设备列表
        self.devices = []
        self.ui.device_list.controls.clear()
        self._device_rssi_controls.clear()
        self._device_connect_btns.clear()
        self.page.update()

        def on_device_found(device_info):
            """扫描到设备时的回调"""
            # 检查是否已存在
            for existing in self.devices:
                if existing['address'] == device_info['address']:
                    return
            self.devices.append(device_info)
            idx = len(self.devices) - 1
            self._add_device_to_list(device_info, idx)
            self.page.update()

        def on_done(result):
            self.scan_lock = False
            self.ui.scan_btn.disabled = False
            self.ui.scan_btn.content = "扫描设备"
            self.ui.scan_btn.icon = ft.Icons.SEARCH_ROUNDED
            if isinstance(result, Exception):
                self.log(f"扫描失败: {result}", "error")
                self.page.update()
                return
            self.log(f"扫描完成，发现 {len(result)} 个设备", "success")
            self.page.update()

        self.app.run_async(
            self.ble.scan_devices(timeout=timeout, name_filter=name_filter, on_device_found=on_device_found),
            on_done
        )

    def _on_connect_device_click(self, event):
        """点击连接/断开图标"""
        idx = event.control.data
        if idx < len(self.devices):
            device = self.devices[idx]
            device_address = device.get('address')
            current_device = getattr(self.ble, 'selected_device_info', None)
            current_address = current_device.get('address') if current_device else None
            
            if self.ble.connected and device_address == current_address:
                self.handle_disconnect()
            else:
                self.ble.selected_device_info = device
                self.ui.status_text.value = f"{device['name']} | {device['address']}"
                self.ui.status_text.color = ft.Colors.ON_SURFACE
                self.ui.connect_toggle_btn.disabled = False
                self.log(f"已选择: {device['name']}", "info")
                self.page.update()
                self.handle_connect()

    def handle_device_click(self, event):
        """选择设备"""
        idx = event.control.data
        if idx < len(self.devices):
            device = self.devices[idx]
            self.ble.selected_device_info = device
            self.ui.status_text.value = f"{device['name']} | {device['address']}"
            self.ui.status_text.color = ft.Colors.ON_SURFACE
            self.ui.connect_toggle_btn.disabled = False
            self.log(f"已选择: {device['name']}", "info")
            self.page.update()

    def handle_connect_toggle(self, event=None):
        """连接/断开切换"""
        if self.ble.connected:
            self.handle_disconnect()
        else:
            self.handle_connect()

    def handle_connect(self, event=None):
        """连接设备"""
        device = getattr(self.ble, 'selected_device_info', None)
        if not device:
            self.log("请先选择一个设备", "warn")
            return
        if self.ble.connected:
            self.log("已连接设备，请先断开", "warn")
            return
        self.ui.connect_toggle_btn.disabled = True
        self.ui.connect_toggle_btn.content = "连接中..."
        self.ui.connect_toggle_btn.icon = ft.Icons.SYNC
        self.log(f"正在连接 {device['name']}...", "info")
        self.ble_log(f"连接到 {device['name']} ({device['address']})", "info")
        self.page.update()

        def on_done(result):
            if isinstance(result, Exception):
                self.update_connection_ui(False)
                self.log(f"连接异常: {result}", "error")
                self.ble_log(f"连接失败: {result}", "error")
                return
            ok, mtu_or_err = result
            if ok:
                self.log(f"连接成功 (MTU={mtu_or_err})", "success")
                self.ble_log(f"连接成功 MTU={mtu_or_err}", "success")
                self.update_connection_ui(True)
            else:
                self.log(f"连接失败: {mtu_or_err}", "error")
                self.ble_log(f"连接失败: {mtu_or_err}", "error")
                self.update_connection_ui(False)

        self.app.run_async(self.ble.connect_device(device), on_done)

    def handle_disconnect(self, event=None):
        """断开连接"""
        self.ui.connect_toggle_btn.disabled = True
        self.ui.connect_toggle_btn.content = "断开中..."
        self.ui.connect_toggle_btn.icon = ft.Icons.SYNC
        self.log("正在断开连接...", "info")
        self.ble_log("断开连接", "info")
        self.page.update()

        def on_done(result):
            if isinstance(result, Exception):
                self.update_connection_ui(False)
                self.log(f"断开连接异常: {result}", "error")
                self.ble_log(f"断开连接失败: {result}", "error")
                return
            self.update_connection_ui(False)
            self.log("已断开连接", "info")
            self.ble_log("已断开", "info")

        self.app.run_async(self.ble.disconnect_device(), on_done)
