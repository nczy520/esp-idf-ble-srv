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
        self._conn_time_task = None
        self._selected_device_name = None
        self._selected_device_addr = None

    def _format_conn_status_text(self):
        """格式化状态栏设备信息文本，包含连接时长"""
        if not self._selected_device_name or not self._selected_device_addr:
            return "未选择设备"
        dur = self.ble.get_connection_duration_str()
        if dur:
            return f"{self._selected_device_name} | {self._selected_device_addr} | {dur}"
        return f"{self._selected_device_name} | {self._selected_device_addr}"

    async def _update_conn_time_loop(self):
        """连接时间定时更新协程"""
        try:
            while self.ble.connected and self.ble.connected_time is not None:
                self.ui.status_text.value = self._format_conn_status_text()
                try:
                    self.page.update(self.ui.status_text)
                except Exception:
                    break
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            self.log(f"连接时间更新异常: {e}", "error")

    def _start_conn_time_updater(self):
        """启动连接时间更新器"""
        self._stop_conn_time_updater()
        if self.page:
            self._conn_time_task = self.page.run_task(self._update_conn_time_loop)

    def _stop_conn_time_updater(self):
        """停止连接时间更新器"""
        if self._conn_time_task:
            try:
                self._conn_time_task.cancel()
            except Exception:
                pass
            self._conn_time_task = None

    def update_connection_ui(self, connected):
        """更新连接状态UI"""
        device = getattr(self.ble, 'selected_device_info', None)
        connected_address = device.get('address') if device else None
        
        if connected:
            if device:
                self._selected_device_name = device.get('name', 'Unknown')
                self._selected_device_addr = device.get('address', '')
                self.ui.status_text.value = self._format_conn_status_text()
                self.ui.status_text.color = ft.Colors.ON_SURFACE
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
            self._start_conn_time_updater()
            # 已连接时禁用扫描按钮
            self.ui.scan_btn.disabled = True
            self.ui.scan_timeout_btn.disabled = True
        else:
            self._stop_conn_time_updater()
            if self._selected_device_name and self._selected_device_addr:
                self.ui.status_text.value = f"{self._selected_device_name} | {self._selected_device_addr}"
                self.ui.status_text.color = ft.Colors.ON_SURFACE
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
            # 断开后恢复扫描按钮（扫描进行中除外）
            if not self.scan_lock:
                self.ui.scan_btn.disabled = False
            self.ui.scan_timeout_btn.disabled = False
        
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
        
        # 更新日志页面按钮状态
        handlers = getattr(self.app, 'handlers', None)
        if handlers and hasattr(handlers, 'log_control') and handlers.log_control:
            handlers.log_control.update_buttons_state()
        
        self.safe_update()

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
        if hasattr(self.ui, 'custom_cmd_overlay') and self.ui.custom_cmd_overlay:
            self.ui.custom_cmd_overlay.visible = visible

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
                leading=ft.Icon(ft.Icons.BLUETOOTH, color=ft.Colors.BLUE, size=18),
                title=ft.Text(device['name'], size=13, weight=ft.FontWeight.W_500),
                subtitle=ft.Row([
                    ft.Text(device['address'], size=10, color=ft.Colors.ON_SURFACE_VARIANT),
                    rssi_icon_ctrl,
                    rssi_text_ctrl,
                ], spacing=2),
                trailing=connect_btn,
                on_click=self.handle_device_click,
                data=index,
                style=ft.ListTileStyle.LIST,
                dense=True,
                content_padding=ft.padding.Padding(8, 6, 8, 6),
            )
        )

    def handle_scan(self, event=None):
        """扫描BLE设备"""
        if self.scan_lock:
            return
        self.scan_lock = True
        self.ui.scan_btn.disabled = True
        self._apply_loading_style(self.ui.scan_btn)
        self.safe_update()
        self.log("开始扫描BLE设备...", "info")
        name_filter = self.ui.filter_field.value.strip() or None
        timeout = self.ui.scan_timeout_dropdown.get_scan_timeout() if hasattr(self.ui.scan_timeout_dropdown, 'get_scan_timeout') else 5

        self.devices = []
        self.ui.device_list.controls.clear()
        self._device_rssi_controls.clear()
        self._device_connect_btns.clear()
        self.safe_update()

        def _handle_scan_device_found(device_info):
            for existing in self.devices:
                if existing['address'] == device_info['address']:
                    return
            self.devices.append(device_info)
            idx = len(self.devices) - 1
            self._add_device_to_list(device_info, idx)
            self.safe_update()

        def on_done(result):
            self.scan_lock = False
            self.ui.scan_btn.disabled = False
            self._restore_button_style(self.ui.scan_btn)
            if isinstance(result, Exception):
                self.log(f"扫描失败: {result}", "error")
                self.safe_update()
                return
            self.log(f"扫描完成，发现 {len(result)} 个设备", "success")
            self.safe_update()

        self.app.run_async(
            self.ble.scan_devices(timeout=timeout, name_filter=name_filter, on_device_found=_handle_scan_device_found),
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
                self._selected_device_name = device['name']
                self._selected_device_addr = device['address']
                self.ui.status_text.value = f"{device['name']} | {device['address']}"
                self.ui.status_text.color = ft.Colors.ON_SURFACE
                self.ui.connect_toggle_btn.disabled = False
                self.log(f"已选择: {device['name']}", "info")
                self.safe_update()
                self.handle_connect()

    def handle_device_click(self, event):
        """选择设备"""
        idx = event.control.data
        if idx < len(self.devices):
            device = self.devices[idx]
            self.ble.selected_device_info = device
            self._selected_device_name = device['name']
            self._selected_device_addr = device['address']
            self.ui.status_text.value = f"{device['name']} | {device['address']}"
            self.ui.status_text.color = ft.Colors.ON_SURFACE
            self.ui.connect_toggle_btn.disabled = False
            self.log(f"已选择: {device['name']}", "info")
            self.safe_update()

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
        pin = self.ui.pin_field.value.strip() if self.ui.pin_field else None
        self.log(f"正在连接 {device['name']}...", "info")
        self.ble_log(f"连接到 {device['name']} ({device['address']})", "info")
        btn = self.ui.connect_toggle_btn

        def callback(result):
            if isinstance(result, Exception):
                self.update_connection_ui(False)
                self.log(f"连接异常: {result}", "error")
                self.ble_log(f"连接失败: {result}", "error")
                return
            ok, mtu_or_err = result
            if ok:
                from client_gui import save_last_device
                save_last_device(device['name'], device['address'])
                self.log(f"连接成功 (MTU={mtu_or_err})", "success")
                self.ble_log(f"连接成功 MTU={mtu_or_err}", "success")
                self.update_connection_ui(True)
                if hasattr(self.app.handlers, 'custom_cmd'):
                    self.app.handlers.custom_cmd._update_custom_cmd_ui_on_connect()
            else:
                self.log(f"连接失败: {mtu_or_err}", "error")
                self.ble_log(f"连接失败: {mtu_or_err}", "error")
                self.update_connection_ui(False)

        self._run_with_loading(btn, self.ble.connect_device(device, pin=pin), callback, loading_text="连接中...", timeout=15)

    def handle_disconnect(self, event=None):
        """断开连接"""
        self.log("正在断开连接...", "info")
        self.ble_log("断开连接", "info")
        btn = self.ui.connect_toggle_btn

        def callback(result):
            if isinstance(result, Exception):
                self.update_connection_ui(False)
                self.log(f"断开连接异常: {result}", "error")
                self.ble_log(f"断开连接失败: {result}", "error")
                return
            self.update_connection_ui(False)
            self.log("已断开连接", "info")
            self.ble_log("已断开", "info")
            if hasattr(self.app.handlers, 'custom_cmd'):
                self.app.handlers.custom_cmd._reset_custom_cmd_ui_on_disconnect()

        self._run_with_loading(btn, self.ble.disconnect_device(), callback, loading_text="断开中...", timeout=5)
