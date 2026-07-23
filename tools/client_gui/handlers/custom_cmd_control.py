"""
自定义命令处理模块
处理自定义命令的发送、接收和定时发送功能
"""

import time
import asyncio
import threading

import flet as ft

from client_gui.handlers.base import BaseHandler


class CustomCmdHandler(BaseHandler):
    """自定义命令处理类"""

    def __init__(self, app):
        super().__init__(app)
        self.cmd_format = "ascii"
        self.auto_send_running = False
        self._auto_send_thread = None
        self._auto_send_stop_event = None

    def _format_data(self, data, direction="tx"):
        """格式化数据为显示字符串
        Args:
            data: bytes数据
            direction: tx/rx
        Returns:
            str: 格式化后的字符串
        """
        if self.cmd_format == "hex":
            hex_str = " ".join(f"{b:02X}" for b in data)
            return hex_str
        else:
            try:
                return data.decode('utf-8', errors='replace')
            except Exception:
                return repr(data)

    def _parse_input(self, text):
        """解析输入文本为bytes
        Args:
            text: 输入的文本
        Returns:
            bytes: 解析后的字节数据，失败返回None
        """
        if not text or not text.strip():
            return None
        text = text.strip()
        if self.cmd_format == "hex":
            try:
                parts = text.replace(',', ' ').replace('\n', ' ').split()
                byte_list = []
                for p in parts:
                    p = p.strip()
                    if not p:
                        continue
                    if p.startswith('0x') or p.startswith('0X'):
                        p = p[2:]
                    byte_list.append(int(p, 16) & 0xFF)
                return bytes(byte_list)
            except Exception as e:
                self.log(f"HEX解析失败: {e}", "error")
                return None
        else:
            return text.encode('utf-8')

    def add_cmd_log(self, data, direction="tx", auto=False):
        """添加命令日志
        Args:
            data: bytes数据
            direction: tx/rx
            auto: 是否为定时自动发送
        """
        display_str = self._format_data(data, direction)
        timestamp = time.strftime("%H:%M:%S")

        if direction == "tx":
            icon = ft.Icons.ARROW_UPWARD
            color = ft.Colors.CYAN_300
            prefix = "AUTO TX" if auto else "TX"
        else:
            icon = ft.Icons.ARROW_DOWNWARD
            color = ft.Colors.AMBER_300
            prefix = "RX"

        self.ui.cmd_log_view.controls.append(
            ft.Row([
                ft.Text(f"[{timestamp}]", size=10, color=ft.Colors.GREY_500, font_family="mono", selectable=True),
                ft.Icon(icon, size=12, color=color),
                ft.Text(f"{prefix} ({len(data)}B)", size=10, color=color, font_family="mono", selectable=True),
                ft.Text(display_str, size=10, color=color, font_family="mono", expand=True, selectable=True),
            ], spacing=4)
        )

        if len(self.ui.cmd_log_view.controls) > 500:
            self.ui.cmd_log_view.controls = self.ui.cmd_log_view.controls[-300:]

        self.safe_update()

    def on_custom_cmd_response(self, data):
        """自定义命令响应回调"""
        try:
            self.add_cmd_log(data, direction="rx")
        except Exception as e:
            print(f"[CMD] Response handler error: {e}")

    def send_custom_cmd(self, event=None):
        """发送自定义命令"""
        if not self.check_connected():
            return

        text = self.ui.cmd_input.value.strip() if self.ui.cmd_input.value else ""
        if not text:
            self.log("请输入命令内容", "warn")
            return

        data = self._parse_input(text)
        if data is None:
            return

        if len(data) > 512:
            self.log("命令数据过长（最大512字节）", "warn")
            return

        self.add_cmd_log(data, direction="tx")

        btn = self.ui.custom_cmd_tab.send_btn

        def callback(result):
            if isinstance(result, Exception):
                self.log(f"发送失败: {result}", "error")
                return
            ok = result
            if not ok:
                self.log("发送失败", "error")

        self._run_with_loading(btn, self.ble.custom_cmd_send(data), callback,
                               loading_text="发送中", timeout=5)

    def clear_custom_cmd_log(self, event=None):
        """清空自定义命令日志"""
        self.ui.cmd_log_view.controls.clear()
        self.safe_update()

    def on_custom_cmd_format_change(self, event=None):
        """格式切换"""
        selected = self.ui.custom_cmd_tab.format_switch.selected
        if selected and len(selected) > 0:
            self.cmd_format = selected[0]

    def on_auto_send_toggle(self, event=None):
        """定时发送开关切换"""
        enabled = self.ui.custom_cmd_tab.auto_send_switch.value
        self.ui.custom_cmd_tab.interval_field.disabled = not enabled
        self.safe_update()

        if enabled:
            self._start_auto_send()
        else:
            self._stop_auto_send()

    def _start_auto_send(self):
        """启动定时发送"""
        if self.auto_send_running:
            return

        text = self.ui.cmd_input.value.strip() if self.ui.cmd_input.value else ""
        if not text:
            self.log("请输入要发送的命令内容", "warn")
            self.ui.custom_cmd_tab.auto_send_switch.value = False
            self.ui.custom_cmd_tab.interval_field.disabled = True
            self.safe_update()
            return

        data = self._parse_input(text)
        if data is None:
            self.ui.custom_cmd_tab.auto_send_switch.value = False
            self.ui.custom_cmd_tab.interval_field.disabled = True
            self.safe_update()
            return

        try:
            interval_ms = int(self.ui.custom_cmd_tab.interval_field.value or "2000")
            if interval_ms < 10:
                interval_ms = 10
            if interval_ms > 60000:
                interval_ms = 60000
        except ValueError:
            self.log("请输入有效的间隔时间", "warn")
            self.ui.custom_cmd_tab.auto_send_switch.value = False
            self.ui.custom_cmd_tab.interval_field.disabled = True
            self.safe_update()
            return

        self.auto_send_running = True
        self._auto_send_stop_event = threading.Event()

        interval_sec = interval_ms / 1000.0

        def on_auto_send_done(result):
            if isinstance(result, Exception):
                self.log(f"定时发送失败: {result}", "error")
                return
            if result:
                self.add_cmd_log(data, direction="tx", auto=True)

        def auto_send_loop():
            try:
                while not self._auto_send_stop_event.is_set():
                    if not self.ble.connected:
                        break
                    try:
                        self.app.run_async(self.ble.custom_cmd_send(data), on_auto_send_done)
                    except Exception as e:
                        self.app.ui_call(lambda: self.log(f"定时发送失败: {e}", "error"))
                        break
                    self._auto_send_stop_event.wait(interval_sec)
            except Exception as e:
                self.app.ui_call(lambda: self.log(f"定时发送线程异常: {e}", "error"))
            finally:
                self.auto_send_running = False
                def _cleanup():
                    self.ui.custom_cmd_tab.auto_send_switch.value = False
                    self.ui.custom_cmd_tab.interval_field.disabled = True
                    self.safe_update()
                    self.log("定时发送已停止", "info")
                self.app.ui_call(_cleanup)

        self._auto_send_thread = threading.Thread(target=auto_send_loop, daemon=True)
        self._auto_send_thread.start()
        self.log(f"定时发送已启动 (间隔 {interval_ms}ms)", "info")

    def _stop_auto_send(self):
        """停止定时发送"""
        if self._auto_send_stop_event:
            self._auto_send_stop_event.set()

    def _reset_custom_cmd_ui_on_disconnect(self):
        """断开连接时重置自定义命令UI状态"""
        self._stop_auto_send()
        self.auto_send_running = False
        self.ui.custom_cmd_tab.auto_send_switch.value = False
        self.ui.custom_cmd_tab.interval_field.disabled = True
        self.ui.custom_cmd_overlay.visible = True
        self.safe_update()

    def _update_custom_cmd_ui_on_connect(self):
        """连接成功时更新自定义命令UI状态"""
        self.ui.custom_cmd_overlay.visible = False
        self.ble.set_custom_cmd_callback(self.on_custom_cmd_response)
        self.safe_update()
