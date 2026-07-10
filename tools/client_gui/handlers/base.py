"""
GUI事件处理基础模块
提供通用的工具方法和基础功能
"""

import time
import asyncio

import flet as ft


class BaseHandler:
    """基础处理类，提供通用功能"""

    def __init__(self, app):
        self.app = app
        self.ble = app.ble
        self.page = None
        self.ui = None
        self._loading_count = 0

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
        self.page.update()

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
        self.page.update()

    def clear_log(self, event=None):
        """清空日志"""
        self.ui.log_view.controls.clear()
        self.page.update()

    def clear_ble_log(self, event=None):
        """清空蓝牙通讯日志"""
        self.ui.ble_log_view.controls.clear()
        self.page.update()

    def check_connected(self):
        """检查是否已连接设备"""
        if not self.ble.connected:
            self.log("未连接设备，请先连接", "error")
            return False
        return True

    def _get_all_action_buttons(self):
        """获取所有操作按钮"""
        buttons = []
        # 设备信息按钮
        if hasattr(self.ui, 'info_tab') and self.ui.info_tab:
            buttons.extend(self.ui.info_tab.info_btns)
        # LED控制按钮
        if hasattr(self.ui, 'led_tab') and self.ui.led_tab:
            buttons.extend([
                self.ui.led_tab.led_on_btn,
                self.ui.led_tab.led_off_btn,
                self.ui.led_tab.led_status_btn,
                self.ui.led_tab.led_set_color_btn,
                self.ui.led_tab.led_set_effect_btn,
            ])
        # WiFi按钮
        if hasattr(self.ui, 'wifi_tab') and self.ui.wifi_tab:
            buttons.extend([
                self.ui.wifi_tab.wifi_connect_btn,
                self.ui.wifi_tab.wifi_status_btn,
                self.ui.wifi_tab.wifi_forget_btn,
                self.ui.wifi_tab.wifi_ntp_btn,
            ])
        # OTA按钮
        if hasattr(self.ui, 'ota_tab') and self.ui.ota_tab:
            buttons.extend([
                self.ui.ota_tab.ota_url_btn,
                self.ui.ota_tab.ota_default_btn,
            ])
        return buttons

    def _show_btn_loading(self, active_btn, loading_text="处理中..."):
        """显示按钮Loading状态 - 禁用所有按钮，激活按钮显示Loading"""
        self._loading_count += 1
        # 保存所有按钮的原始状态
        for btn in self._get_all_action_buttons():
            if btn is active_btn:
                # 激活按钮：显示Loading
                if not hasattr(btn, '_original_content'):
                    btn._original_content = btn.content
                if not hasattr(btn, '_original_icon'):
                    btn._original_icon = btn.icon
                btn.content = loading_text
                btn.icon = ft.Icons.SYNC
            else:
                # 其他按钮：禁用但不改变外观
                btn.disabled = True
        self.page.update()

    def _hide_btn_loading(self, active_btn):
        """恢复按钮原始状态"""
        self._loading_count = max(0, self._loading_count - 1)
        if self._loading_count > 0:
            return
        # 恢复所有按钮
        for btn in self._get_all_action_buttons():
            btn.disabled = False
            if btn is active_btn:
                if hasattr(btn, '_original_content'):
                    btn.content = btn._original_content
                if hasattr(btn, '_original_icon') and btn._original_icon:
                    btn.icon = btn._original_icon
        self.page.update()

    def _run_with_loading(self, btn, coro, callback, loading_text="读取中...", timeout=3):
        """带Loading状态的异步执行"""
        self._show_btn_loading(btn, loading_text)

        def on_done(result):
            self._hide_btn_loading(btn)
            callback(result)

        # 设置超时
        async def coro_with_timeout():
            return await asyncio.wait_for(coro, timeout=timeout)

        self.app.run_async(coro_with_timeout(), on_done)

    def _show_loading(self, message="处理中..."):
        """显示全局Loading状态"""
        if hasattr(self.ui, 'loading_overlay') and self.ui.loading_overlay:
            self.ui.loading_overlay.content.controls[1].value = message
            self.ui.loading_overlay.visible = True
            self.page.update()
        self.log(message, "info")

    def _hide_loading(self):
        """隐藏全局Loading状态"""
        if hasattr(self.ui, 'loading_overlay') and self.ui.loading_overlay:
            self.ui.loading_overlay.visible = False
            self.page.update()
