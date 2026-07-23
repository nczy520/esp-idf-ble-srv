"""
GUI事件处理基础模块
提供通用的工具方法和基础功能
"""

import time
import asyncio
import threading

import flet as ft


class BaseHandler:
    _update_lock = threading.Lock()

    def __init__(self, app):
        self.app = app
        self.ble = app.ble
        self.page = None
        self.ui = None
        self._loading_count = 0

    def safe_update(self):
        with BaseHandler._update_lock:
            self.page.update()

    def set_ui(self, ui):
        self.ui = ui
        self.page = ui.page

    def log(self, msg, level="info"):
        """添加系统日志消息"""
        colors = {
            "info": ft.Colors.BLUE_200,
            "success": ft.Colors.GREEN_300,
            "error": ft.Colors.RED_300,
            "warn": ft.Colors.ORANGE_300
        }
        icons = {
            "info": ft.Icons.INFO,
            "success": ft.Icons.CHECK_CIRCLE,
            "error": ft.Icons.ERROR,
            "warn": ft.Icons.WARNING
        }
        color = colors.get(level, ft.Colors.BLUE_200)
        timestamp = time.strftime("%H:%M:%S")
        self.ui.log_view.controls.append(
            ft.Row([
                ft.Text(f"[{timestamp}]", size=11, color=ft.Colors.GREY_500, font_family="mono", selectable=True),
                ft.Icon(icons.get(level, ft.Icons.INFO), size=14, color=color),
                ft.Text(msg, size=11, color=color, font_family="mono", expand=True, selectable=True),
            ], spacing=6)
        )
        if len(self.ui.log_view.controls) > 500:
            self.ui.log_view.controls = self.ui.log_view.controls[-300:]
        self.safe_update()

    def ble_log(self, msg, direction="info"):
        """添加蓝牙通讯日志"""
        if direction == "tx":
            icon = ft.Icons.ARROW_UPWARD
            color = ft.Colors.CYAN_300
            prefix = "TX"
        elif direction == "rx":
            icon = ft.Icons.ARROW_DOWNWARD
            color = ft.Colors.AMBER_300
            prefix = "RX"
        elif direction == "success":
            icon = ft.Icons.CHECK_CIRCLE
            color = ft.Colors.GREEN_300
            prefix = "OK"
        elif direction == "error":
            icon = ft.Icons.ERROR
            color = ft.Colors.RED_300
            prefix = "ERR"
        elif direction == "warn":
            icon = ft.Icons.WARNING
            color = ft.Colors.ORANGE_300
            prefix = "!!"
        else:
            icon = ft.Icons.INFO
            color = ft.Colors.GREY_400
            prefix = "  "

        timestamp = time.strftime("%H:%M:%S")
        self.ui.ble_log_view.controls.append(
            ft.Row([
                ft.Text(f"[{timestamp}]", size=10, color=ft.Colors.GREY_500, font_family="mono", selectable=True),
                ft.Icon(icon, size=12, color=color),
                ft.Text(f"{prefix} {msg}", size=10, color=color, font_family="mono", expand=True, selectable=True),
            ], spacing=4)
        )
        if len(self.ui.ble_log_view.controls) > 500:
            self.ui.ble_log_view.controls = self.ui.ble_log_view.controls[-300:]
        self.safe_update()

    def clear_log(self, event=None):
        """清空日志"""
        self.ui.log_view.controls.clear()
        self.safe_update()

    def clear_ble_log(self, event=None):
        """清空蓝牙通讯日志"""
        self.ui.ble_log_view.controls.clear()
        self.safe_update()

    def check_connected(self):
        """检查是否已连接设备"""
        if not self.ble.connected:
            self.log("未连接设备，请先连接", "error")
            return False
        return True

    def _get_all_action_buttons(self):
        """获取所有操作按钮（包含scan_btn和connect_toggle_btn）"""
        buttons = []
        if hasattr(self.ui, 'scan_btn') and self.ui.scan_btn:
            buttons.append(self.ui.scan_btn)
        if hasattr(self.ui, 'connect_toggle_btn') and self.ui.connect_toggle_btn:
            buttons.append(self.ui.connect_toggle_btn)
        if hasattr(self.ui, 'info_tab') and self.ui.info_tab:
            buttons.extend(self.ui.info_tab.info_btns)
        if hasattr(self.ui, 'led_tab') and self.ui.led_tab:
            buttons.extend([
                self.ui.led_tab.led_on_btn,
                self.ui.led_tab.led_off_btn,
                self.ui.led_tab.led_status_btn,
                self.ui.led_tab.led_set_color_btn,
                self.ui.led_tab.led_set_effect_btn,
                self.ui.led_tab.led_set_layout_btn,
                self.ui.led_tab.led_get_layout_btn,
            ])
        if hasattr(self.ui, 'wifi_tab') and self.ui.wifi_tab:
            buttons.extend([
                self.ui.wifi_tab.wifi_connect_btn,
                self.ui.wifi_tab.wifi_status_btn,
                self.ui.wifi_tab.wifi_forget_btn,
                self.ui.wifi_tab.wifi_ntp_btn,
            ])
        if hasattr(self.ui, 'ota_tab') and self.ui.ota_tab:
            buttons.extend([
                self.ui.ota_tab.ota_bt_btn,
                self.ui.ota_tab.ota_url_btn,
                self.ui.ota_tab.ota_default_btn,
                self.ui.ota_tab.ota_abort_btn,
            ])
        if hasattr(self.ui, 'custom_cmd_tab') and self.ui.custom_cmd_tab:
            buttons.extend([
                self.ui.custom_cmd_tab.send_btn,
            ])
        if hasattr(self.ui, 'log_tab') and self.ui.log_tab:
            buttons.extend([
                self.ui.log_tab.refresh_btn,
                self.ui.log_tab.format_btn,
                self.ui.log_tab.write_marker_btn,
                self.ui.log_tab.open_browser_btn,
            ])
        return buttons

    def _get_excluded_buttons(self):
        """获取操作期间不禁用的按钮：扫描按钮、连接/断开按钮、OTA运行时的中止按钮"""
        excluded = []
        if hasattr(self.ui, 'scan_btn') and self.ui.scan_btn:
            excluded.append(self.ui.scan_btn)
        if hasattr(self.ui, 'connect_toggle_btn') and self.ui.connect_toggle_btn:
            excluded.append(self.ui.connect_toggle_btn)
        ota_handler = getattr(self.app.handlers, 'ota_control', None)
        if ota_handler and ota_handler.ota_running:
            if hasattr(self.ui, 'ota_tab') and self.ui.ota_tab and self.ui.ota_tab.ota_abort_btn:
                excluded.append(self.ui.ota_tab.ota_abort_btn)
        return excluded

    def _apply_disabled_style(self, btn):
        """为按钮应用禁用视觉样式：原色50%透明、无阴影"""
        if not hasattr(btn, '_saved_bgcolor'):
            btn._saved_bgcolor = btn.bgcolor
        if not hasattr(btn, '_saved_color'):
            btn._saved_color = btn.color
        if not hasattr(btn, '_saved_elevation'):
            btn._saved_elevation = btn.style.elevation if btn.style else 2
        btn.bgcolor = ft.Colors.with_opacity(0.5, btn._saved_bgcolor) if btn._saved_bgcolor else btn.bgcolor
        btn.color = ft.Colors.with_opacity(0.5, btn._saved_color) if btn._saved_color else btn.color
        if btn.style:
            btn.style.elevation = 0

    def _apply_loading_style(self, btn, loading_text="加载中"):
        """为按钮应用Loading视觉样式：原色70%透明、无阴影，文本改为加载中，图标改为Loading"""
        if not hasattr(btn, '_saved_bgcolor'):
            btn._saved_bgcolor = btn.bgcolor
        if not hasattr(btn, '_saved_color'):
            btn._saved_color = btn.color
        if not hasattr(btn, '_saved_elevation'):
            btn._saved_elevation = btn.style.elevation if btn.style else 2
        # Flet 0.80+ 的 ElevatedButton 文本存于 content 属性（字符串或控件）
        if not hasattr(btn, '_saved_content'):
            btn._saved_content = btn.content
        if not hasattr(btn, '_saved_icon'):
            btn._saved_icon = btn.icon
        btn.bgcolor = ft.Colors.with_opacity(0.7, btn._saved_bgcolor) if btn._saved_bgcolor else btn.bgcolor
        btn.color = ft.Colors.with_opacity(0.7, btn._saved_color) if btn._saved_color else btn.color
        if btn.style:
            btn.style.elevation = 0
        btn.content = loading_text
        btn.icon = ft.Icons.SYNC

    def _restore_button_style(self, btn):
        """恢复按钮原始视觉样式"""
        if hasattr(btn, '_saved_bgcolor'):
            btn.bgcolor = btn._saved_bgcolor
            del btn._saved_bgcolor
        if hasattr(btn, '_saved_color'):
            btn.color = btn._saved_color
            del btn._saved_color
        if hasattr(btn, '_saved_elevation') and btn.style:
            btn.style.elevation = btn._saved_elevation
            del btn._saved_elevation
        if hasattr(btn, '_saved_content'):
            btn.content = btn._saved_content
            del btn._saved_content
        if hasattr(btn, '_saved_icon'):
            btn.icon = btn._saved_icon
            del btn._saved_icon

    def _show_btn_loading(self, active_btn, loading_text="加载中"):
        """显示按钮Loading状态 - 禁用除scan/connect外的所有按钮，激活按钮显示Loading"""
        self._loading_count += 1
        excluded = self._get_excluded_buttons()
        for btn in self._get_all_action_buttons():
            if btn in excluded:
                continue
            if btn is active_btn:
                btn.disabled = True
                self._apply_loading_style(btn, loading_text)
            else:
                btn.disabled = True
                self._apply_disabled_style(btn)
        self.safe_update()

    def _hide_btn_loading(self, active_btn):
        """恢复按钮原始状态"""
        self._loading_count = max(0, self._loading_count - 1)
        if self._loading_count > 0:
            return
        excluded = self._get_excluded_buttons()
        for btn in self._get_all_action_buttons():
            if btn in excluded:
                continue
            btn.disabled = False
            self._restore_button_style(btn)
        self._restore_ota_abort_btn_state()
        self.safe_update()

    def _force_restore_all_buttons(self):
        """强制恢复所有按钮的原始样式（用于断开连接等异常场景）"""
        self._loading_count = 0
        for btn in self._get_all_action_buttons():
            self._restore_button_style(btn)
            btn.disabled = False
        self._restore_ota_abort_btn_state()
        self.safe_update()

    def _restore_ota_abort_btn_state(self):
        """恢复OTA中止按钮的正确状态"""
        if hasattr(self.ui, 'ota_tab') and self.ui.ota_tab:
            ota_handler = getattr(self.app.handlers, 'ota_control', None)
            abort_btn = self.ui.ota_tab.ota_abort_btn
            is_running = ota_handler and ota_handler.ota_running
            if is_running:
                abort_btn.disabled = False
                self._restore_button_style(abort_btn)
                abort_btn.bgcolor = ft.Colors.RED_700
                abort_btn.color = ft.Colors.WHITE
            else:
                abort_btn.disabled = True
                self._apply_disabled_style(abort_btn)

    def _run_with_loading(self, btn, coro, callback, loading_text="加载中", timeout=3):
        """带Loading状态的异步执行"""
        self._show_btn_loading(btn, loading_text)

        def on_done(result):
            self._hide_btn_loading(btn)
            callback(result)

        async def coro_with_timeout():
            return await asyncio.wait_for(coro, timeout=timeout)

        self.app.run_async(coro_with_timeout(), on_done)

    def _show_loading(self, message="处理中..."):
        """显示全局Loading状态"""
        if hasattr(self.ui, 'loading_overlay') and self.ui.loading_overlay:
            self.ui.loading_overlay.content.controls[1].value = message
            self.ui.loading_overlay.visible = True
            self.safe_update()
        self.log(message, "info")

    def _hide_loading(self):
        """隐藏全局Loading状态"""
        if hasattr(self.ui, 'loading_overlay') and self.ui.loading_overlay:
            self.ui.loading_overlay.visible = False
            self.safe_update()
