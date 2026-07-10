"""
OTA升级处理模块
处理蓝牙OTA和URL OTA升级
"""

import os
import time

import flet as ft

from client_gui.handlers.base import BaseHandler


class OTAControlHandler(BaseHandler):
    """OTA升级处理类"""

    def __init__(self, app):
        super().__init__(app)
        self.ota_running = False

    def pick_firmware(self, event=None):
        """选择固件文件"""
        try:
            import tkinter as tk
            from tkinter import filedialog
            root = tk.Tk()
            root.withdraw()
            root.wm_attributes('-topmost', 1)
            file_path = filedialog.askopenfilename(
                title="选择固件文件",
                filetypes=[("Binary files", "*.bin"), ("All files", "*.*")]
            )
            root.destroy()
            if file_path:
                self.ui.fw_path_field.value = file_path
                self.page.update()
        except Exception as e:
            self.log(f"打开文件对话框失败: {e}", "error")

    def start_ota_bt(self, event=None):
        """开始蓝牙OTA升级"""
        if not self.check_connected():
            return
        fw_path = self.ui.fw_path_field.value.strip() if self.ui.fw_path_field.value else ""
        if not fw_path or not os.path.exists(fw_path):
            self.log("请选择有效的固件文件", "warn")
            return
        self.ota_running = True
        self.ui.ota_bt_btn.disabled = True
        self.ui.ota_abort_btn.disabled = False
        self.ui.ota_progress.value = 0
        self.log(f"开始蓝牙OTA升级: {os.path.basename(fw_path)}", "info")
        self.page.update()

        def progress_cb(written, total, sent, start_time):
            pct = min(100, int(written * 100 / total)) if total > 0 else 0
            self.ui.ota_progress.value = pct / 100.0
            elapsed = time.time() - start_time
            speed = written / elapsed if elapsed > 0 else 0
            s = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            self.ui.ota_status_text.value = f"进度: {pct}% | 速度: {s} | {written}/{total} bytes"
            self.page.update()

        def on_done(result):
            self.ota_running = False
            self.ui.ota_bt_btn.disabled = False
            self.ui.ota_abort_btn.disabled = True
            if isinstance(result, Exception):
                self.log(f"OTA异常: {result}", "error")
                self.page.update()
                return
            ok, msg = result
            if ok:
                self.log(f"OTA升级成功: {msg}", "success")
                self.ui.ota_progress.value = 1.0
                self.ui.ota_status_text.value = "OTA升级成功"
            else:
                self.log(f"OTA升级失败: {msg}", "error")
                self.ui.ota_status_text.value = f"OTA失败: {msg}"
            self.page.update()

        self.app.run_async(self.ble.ota_update(fw_path, progress_cb=progress_cb), on_done)

    def start_ota_url(self, event=None):
        """开始URL OTA升级"""
        if not self.check_connected():
            return
        url = self.ui.ota_url_field.value.strip() if self.ui.ota_url_field.value else ""
        if not url:
            self.log("请输入固件URL", "warn")
            return
        self.log(f"开始URL OTA: {url}", "info")
        btn = self.ui.ota_tab.ota_url_btn
        def callback(result):
            if isinstance(result, Exception):
                self.log(f"URL OTA异常: {result}", "error")
                return
            ok, msg = result
            self.log(f"URL OTA已触发: {msg}" if ok else f"URL OTA失败: {msg}", "success" if ok else "error")
        self._run_with_loading(btn, self.ble.ota_url_start(url), callback)

    def start_ota_default(self, event=None):
        """开始默认URL OTA升级"""
        if not self.check_connected():
            return
        self.log("开始默认URL OTA...", "info")
        btn = self.ui.ota_tab.ota_default_btn
        def callback(result):
            if isinstance(result, Exception):
                self.log(f"URL OTA异常: {result}", "error")
                return
            ok, msg = result
            self.log(f"默认URL OTA已触发: {msg}" if ok else f"默认URL OTA失败: {msg}", "success" if ok else "error")
        self._run_with_loading(btn, self.ble.ota_url_start(None), callback)

    def abort_ota(self, event=None):
        """中止OTA"""
        self.log("中止OTA...", "warn")
        btn = self.ui.ota_tab.ota_abort_btn
        def callback(result):
            self.ota_running = False
            self.ui.ota_bt_btn.disabled = False
            self.ui.ota_abort_btn.disabled = True
            self.log("OTA已中止", "warn")
            self.page.update()
        self._run_with_loading(btn, self.ble.ota_abort(), callback)
