"""
设备信息处理模块
处理设备信息、内存、CPU、Flash、分区、温度等读取操作
"""

import time

import flet as ft

from client_gui.handlers.base import BaseHandler


class DeviceInfoHandler(BaseHandler):
    """设备信息处理类"""

    def _reset_device_info_ui_on_disconnect(self):
        """断开连接时重置设备信息UI状态"""
        self.ui.info_display.value = "未连接"
        self.safe_update()

    def read_device_info(self, event=None):
        if not self.check_connected():
            return
        self.log("读取设备信息...", "info")
        btn = self.ui.info_tab.info_btns[0]

        def callback(result):
            if isinstance(result, Exception) or result is None:
                self.log("读取设备信息失败", "error")
                return
            self.ui.info_display.value = str(result)
            self.log("设备信息读取成功", "success")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_device_info(), callback, loading_text="读取中...")

    def read_memory_info(self, event=None):
        if not self.check_connected():
            return
        self.log("读取内存信息...", "info")
        btn = self.ui.info_tab.info_btns[1]

        def callback(result):
            if isinstance(result, Exception) or result is None:
                self.log("读取内存信息失败", "error")
                return
            self.ui.info_display.value = str(result)
            self.log("内存信息读取成功", "success")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_memory_info(), callback, loading_text="读取中...")

    def read_cpu_info(self, event=None):
        if not self.check_connected():
            return
        self.log("读取CPU信息...", "info")
        btn = self.ui.info_tab.info_btns[2]

        def callback(result):
            if isinstance(result, Exception) or result is None:
                self.log("读取CPU信息失败", "error")
                return
            self.ui.info_display.value = str(result)
            self.log("CPU信息读取成功", "success")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_cpu_info(), callback, loading_text="读取中...")

    def read_flash_info(self, event=None):
        if not self.check_connected():
            return
        self.log("读取Flash信息...", "info")
        btn = self.ui.info_tab.info_btns[3]

        def callback(result):
            if isinstance(result, Exception) or result is None:
                self.log("读取Flash信息失败", "error")
                return
            self.ui.info_display.value = str(result)
            self.log("Flash信息读取成功", "success")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_flash_info(), callback, loading_text="读取中...")

    def read_partitions(self, event=None):
        if not self.check_connected():
            return
        self.log("读取分区信息...", "info")
        btn = self.ui.info_tab.info_btns[4]

        def callback(result):
            if isinstance(result, Exception):
                self.log(f"读取分区信息失败: {result}", "error")
                return
            if result:
                lines = [f"分区列表 ({len(result)} 个):"]
                lines.append("-" * 80)
                for partition in result:
                    lines.append(str(partition))
                self.ui.info_display.value = "\n".join(lines)
                self.log(f"读取到 {len(result)} 个分区", "success")
            else:
                self.log("读取分区信息失败", "error")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_all_partitions(), callback, loading_text="读取中...")

    def read_temperature(self, event=None):
        if not self.check_connected():
            return
        self.log("读取温度...", "info")
        btn = self.ui.info_tab.info_btns[5]

        def callback(result):
            if isinstance(result, Exception):
                self.log(f"读取温度失败: {result}", "error")
                return
            if result is None:
                self.log("读取温度失败", "error")
            elif result <= -900.0:
                self.ui.info_display.value = "温度传感器: 不支持或未启用"
                self.log("温度传感器不支持", "warn")
            else:
                self.ui.info_display.value = f"当前温度: {result:.2f}°C"
                self.log(f"温度: {result:.2f}°C", "success")
            self.safe_update()

        self._run_with_loading(btn, self.ble.read_temperature(), callback, loading_text="读取中...")

    def restart_device(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.info_tab.info_btns[6]

        def do_restart(dlg):
            dlg.open = False
            self.safe_update()
            self.log("发送重启命令...", "warn")

            def callback(result):
                if isinstance(result, Exception):
                    self.log(f"重启失败: {result}", "error")
                    return
                if result:
                    self.log("设备已确认重启，蓝牙连接已断开", "success")
                else:
                    self.log("重启失败", "error")

            self._run_with_loading(btn, self.ble.restart_device(), callback, loading_text="重启中...", timeout=5)

        dlg = ft.AlertDialog(
            title=ft.Text("确认重启"),
            content=ft.Text("确定要重启设备吗？"),
            actions=[
                ft.TextButton("取消", on_click=lambda e: setattr(dlg, 'open', False) or self.safe_update()),
                ft.TextButton("重启", on_click=lambda e: do_restart(dlg)),
            ],
        )
        self.page.overlay.append(dlg)
        dlg.open = True
        self.safe_update()
