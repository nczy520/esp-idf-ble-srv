"""
日志浏览控制处理器
"""

import threading
import webbrowser
import flet as ft
from client_gui.handlers.base import BaseHandler


class LogControlHandler(BaseHandler):
    """日志浏览控制处理器"""

    def __init__(self, app):
        super().__init__(app)
        self._lock = threading.Lock()
        self._http_url = ""
        self._http_running = False

    def _log_page(self):
        ui = self.ui
        if ui and hasattr(ui, 'log_tab') and ui.log_tab:
            return ui.log_tab
        return None

    def update_buttons_state(self):
        """根据连接状态更新按钮状态"""
        page = self._log_page()
        if not page:
            return

        ble_connected = self.ble.connected if self.ble else False

        buttons = [
            page.refresh_btn,
            page.format_btn,
            page.write_marker_btn,
            page.open_browser_btn,
        ]

        for btn in buttons:
            if not ble_connected:
                if not btn.disabled:
                    self._apply_disabled_style(btn)
                    btn.disabled = True
            else:
                if btn.disabled:
                    self._restore_button_style(btn)
                    btn.disabled = False

        page.http_switch.disabled = not ble_connected
        page.level_dropdown.disabled = not ble_connected
        page.marker_input.disabled = not ble_connected

        self.safe_update()

    async def toggle_http_server(self, enable):
        if not self.ble.connected:
            self.app.show_snack("请先连接设备")
            page = self._log_page()
            if page:
                page.http_switch.value = False
                self.safe_update()
            return

        if enable:
            wifi_status = await self.ble.wifi_status()
            if not wifi_status or not wifi_status.connected:
                self.app.show_snack("请先连接WiFi")
                page = self._log_page()
                if page:
                    page.http_switch.value = False
                    self.safe_update()
                return

        page = self._log_page()
        if not page:
            return

        if enable:
            def on_start(result):
                if isinstance(result, Exception):
                    self._update_http_ui(False, "")
                    self.app.show_snack(f"HTTP服务器启动失败: {result}")
                    return
                def on_status(status):
                    if isinstance(status, Exception) or not status:
                        self._update_http_ui(False, "")
                        self.app.show_snack("HTTP服务器启动失败，请检查WiFi连接")
                        return
                    if status.get("running"):
                        url = status.get("url", "")
                        self._update_http_ui(True, url)
                        self.app.show_snack("HTTP服务器已启动")
                    else:
                        self._update_http_ui(False, "")
                        self.app.show_snack("HTTP服务器启动失败，请检查WiFi连接")
                self.app.run_async(self.ble.log_http_get_status(), on_status)
            self.app.run_async(self.ble.log_http_start(), on_start)
        else:
            def on_stop(result):
                self._update_http_ui(False, "")
                self.app.show_snack("HTTP服务器已停止")
            self.app.run_async(self.ble.log_http_stop(), on_stop)

    def _update_http_ui(self, running, url):
        page = self._log_page()
        self._http_running = running
        self._http_url = url if url else ""
        if page:
            page.update_http_status(running, url)
            self.safe_update()

    async def check_http_status(self):
        if not self.ble.connected:
            return
        page = self._log_page()
        if not page:
            return

        def on_status(status):
            if isinstance(status, Exception) or not status:
                return
            self._update_http_ui(status.get("running", False), status.get("url", ""))

        self.app.run_async(self.ble.log_http_get_status(), on_status)

    def log_refresh(self, event=None):
        """刷新日志页面信息"""
        if not self.check_connected():
            return
        page = self._log_page()
        if not page:
            return
        btn = page.refresh_btn

        def callback(result):
            if isinstance(result, Exception):
                self.app.show_snack(f"读取存储信息失败: {result}")
                return
            page.update_storage_info(result)
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_log_storage_info(), callback, loading_text="刷新中...")

    def log_open_browser(self, event=None):
        """在浏览器中打开HTTP服务器"""
        if self._http_url:
            webbrowser.open(self._http_url)

    def log_set_level(self, event=None):
        """设置日志级别"""
        page = self._log_page()
        if not page or not self.check_connected():
            return
        # 忽略程序化设置值时的触发
        if page._level_updating:
            return
        level = int(page._level_value) if page._level_value else 3
        self.log(f"设置日志级别: {level}", "info")

        def callback(result):
            if isinstance(result, Exception):
                self.app.show_snack(f"设置日志级别失败: {result}")
                return
            self.app.show_snack("日志级别已更新")

        self._run_with_loading(None, self.ble.log_set_level(level), callback, loading_text="设置中...")

    def log_write_marker(self, event=None):
        """写入客户端标记日志"""
        page = self._log_page()
        if not page or not self.check_connected():
            return
        msg = page.marker_input.value.strip() if page.marker_input.value else ""
        if not msg:
            self.app.show_snack("请输入标记内容")
            return

        def callback(result):
            if isinstance(result, Exception):
                self.app.show_snack(f"写入失败: {result}")
                return
            self.app.show_snack("标记已写入设备日志")
            page.marker_input.value = ""
            self.safe_update()

        self._run_with_loading(page.write_marker_btn, self.ble.log_write_marker(msg), callback, loading_text="写入中...")

    def log_format(self, event=None):
        """确认格式化LittleFS分区"""
        if not self.check_connected():
            return
        page = self._log_page()
        if not page:
            return

        def do_format(dlg):
            dlg.open = False
            self.safe_update()
            self.format_littlefs()

        dlg = ft.AlertDialog(
            title=ft.Text("危险操作"),
            content=ft.Text("确定要格式化LittleFS分区吗？所有数据将永久丢失！"),
            actions=[
                ft.TextButton("取消", on_click=lambda e: setattr(dlg, 'open', False) or self.safe_update()),
                ft.TextButton("格式化", on_click=lambda e: do_format(dlg)),
            ],
        )
        self.page.overlay.append(dlg)
        dlg.open = True
        self.safe_update()

    def format_littlefs(self, event=None):
        """执行格式化LittleFS分区"""
        page = self._log_page()
        if not page:
            return
        btn = page.format_btn

        def callback(result):
            if isinstance(result, Exception):
                self.app.show_snack(f"格式化失败: {result}")
                return
            self.app.show_snack("LittleFS分区格式化完成")
            # 重新读取存储信息
            def storage_callback(storage_result):
                if not isinstance(storage_result, Exception):
                    page.update_storage_info(storage_result)
                    self.safe_update()
            self.app.run_async(self.ble.read_log_storage_info(), storage_callback)

        self._run_with_loading(btn, self.ble.log_format_littlefs(), callback, loading_text="格式化中...")

    def on_tab_selected(self):
        self.update_buttons_state()
        if self.ble.connected:
            self.log_refresh()
            self.app.run_async(self.check_http_status())

    def on_ble_disconnected(self):
        """设备断开连接时重置HTTP状态和按钮状态"""
        self._update_http_ui(False, "")
        self._http_running = False
        self._http_url = ""
        self.update_buttons_state()
