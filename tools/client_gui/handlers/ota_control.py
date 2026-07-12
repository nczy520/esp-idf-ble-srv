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
        self._ota_url_check_done = False
        self._ota_url_start_time = 0

    def _set_ota_buttons_disabled(self, disabled):
        """设置OTA按钮的禁用状态
        disabled=True: OTA进行中 → 禁用启动按钮，启用中止按钮（红色）
        disabled=False: OTA结束 → 启用启动按钮，禁用中止按钮（灰色）
        """
        self.ui.ota_bt_btn.disabled = disabled
        self.ui.ota_url_btn.disabled = disabled
        self.ui.ota_default_btn.disabled = disabled
        self.ui.ota_abort_btn.disabled = not disabled
        if disabled:
            self._restore_button_style(self.ui.ota_abort_btn)
            self.ui.ota_abort_btn.bgcolor = ft.Colors.RED_700
            self.ui.ota_abort_btn.color = ft.Colors.WHITE
        else:
            self._apply_disabled_style(self.ui.ota_abort_btn)
        self.safe_update()

    def _reset_ota_ui_on_disconnect(self):
        """断开连接时重置OTA UI状态"""
        self.ota_running = False
        self._ota_url_check_done = False
        self._ota_url_start_time = 0
        self.ui.ota_progress.value = 0
        self.ui.ota_status_text.value = "未连接"
        self.ui.ota_bt_btn.disabled = True
        self.ui.ota_url_btn.disabled = True
        self.ui.ota_default_btn.disabled = True
        self.ui.ota_abort_btn.disabled = True
        self.safe_update()

    def _ota_url_status_callback(self, status, data):
        """URL OTA状态回调"""
        if status == "checking":
            self.ui.ota_status_text.value = "正在检查远程固件版本..."
            self.ui.ota_progress.value = 0
            self.safe_update()
        elif status == "check_ok":
            self._ota_url_check_done = True
            self.ui.ota_status_text.value = "版本检查通过，正在下载固件..."
            self.safe_update()
        elif status == "no_update":
            self._ota_url_check_done = True
            self.ui.ota_progress.value = 0
            self.ui.ota_status_text.value = "已是最新版本，无需升级"
            self.log("固件已是最新版本，无需升级", "info")
            self.ble.set_ota_url_status_callback(None)
            self.ota_running = False
            self._set_ota_buttons_disabled(False)
            self._hide_btn_loading(None)
            self.safe_update()
        elif status == "receiving":
            if data:
                written, total = data
                if total > 0:
                    pct = min(100, int(written * 100 / total))
                    self.ui.ota_progress.value = pct / 100.0
                    elapsed = time.time() - self._ota_url_start_time
                    speed = written / elapsed if elapsed > 0 else 0
                    remaining_bytes = total - written
                    remaining_time = remaining_bytes / speed if speed > 0 else 0
                    if remaining_time >= 60:
                        eta_str = f"{int(remaining_time // 60)}分{int(remaining_time % 60)}秒"
                    elif remaining_time >= 1:
                        eta_str = f"{int(remaining_time)}秒"
                    else:
                        eta_str = "--"
                    s = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
                    self.ui.ota_status_text.value = f"下载进度: {pct}% | 速度: {s} | 剩余时间: {eta_str} | {written}/{total} bytes"
                    self.safe_update()
        elif status == "verifying":
            self.ui.ota_progress.value = 1.0
            self.ui.ota_status_text.value = "正在校验固件..."
            self.safe_update()
        elif status == "verify_ok":
            self.ui.ota_status_text.value = "校验成功，正在应用..."
            self.safe_update()
        elif status == "applying":
            self.ui.ota_status_text.value = "正在应用固件..."
            self.safe_update()
        elif status == "apply_ok":
            self.ui.ota_progress.value = 1.0
            self.ui.ota_status_text.value = "OTA升级成功，设备即将重启..."
            self.log("OTA升级成功，设备即将重启", "success")
            self.ble.set_ota_url_status_callback(None)
            self.ota_running = False
            self._set_ota_buttons_disabled(False)
            self._hide_btn_loading(None)
            self.safe_update()
        elif status == "aborted":
            self.ui.ota_progress.value = 0
            self.ui.ota_status_text.value = "OTA已中止"
            self.log("OTA已中止", "warn")
            self.ble.set_ota_url_status_callback(None)
            self.ota_running = False
            self._set_ota_buttons_disabled(False)
            self._hide_btn_loading(None)
            self.safe_update()
        elif status == "error":
            self.ui.ota_status_text.value = "OTA失败"
            self.log("OTA失败", "error")
            self.ble.set_ota_url_status_callback(None)
            self.ota_running = False
            self._set_ota_buttons_disabled(False)
            self._hide_btn_loading(None)
            self.safe_update()

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
                self.safe_update()
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
        fw_size = os.path.getsize(fw_path)
        est_speed = 20 * 1024
        est_transfer = fw_size / est_speed if est_speed > 0 else 120
        timeout = max(300, int(est_transfer * 3))
        self.ota_running = True
        self.ui.ota_progress.value = 0
        self.ui.ota_status_text.value = "正在启动蓝牙OTA..."
        self.log(f"开始蓝牙OTA升级: {os.path.basename(fw_path)} ({fw_size/1024:.1f}KB, 预计超时{timeout}秒)", "info")
        btn = self.ui.ota_tab.ota_bt_btn
        self._set_ota_buttons_disabled(True)

        def progress_cb(written, total, sent, start_time):
            pct = min(100, int(written * 100 / total)) if total > 0 else 0
            self.ui.ota_progress.value = pct / 100.0
            elapsed = time.time() - start_time
            speed = written / elapsed if elapsed > 0 else 0
            remaining_bytes = total - written
            remaining_time = remaining_bytes / speed if speed > 0 else 0
            if remaining_time >= 60:
                eta_str = f"{int(remaining_time // 60)}分{int(remaining_time % 60)}秒"
            elif remaining_time >= 1:
                eta_str = f"{int(remaining_time)}秒"
            else:
                eta_str = "--"
            s = f"{speed / 1024:.1f}KB/s" if speed >= 1024 else f"{speed:.0f}B/s"
            self.ui.ota_status_text.value = f"进度: {pct}% | 速度: {s} | 剩余时间: {eta_str} | {written}/{total} bytes"
            self.safe_update()

        def callback(result):
            self.ota_running = False
            if isinstance(result, Exception):
                self.log(f"OTA异常: {result}", "error")
                self.ui.ota_status_text.value = f"OTA异常: {result}"
                self.ui.ota_progress.value = 0
                self._set_ota_buttons_disabled(False)
                return
            ok, msg = result
            if ok:
                self.log(f"OTA升级成功: {msg}", "success")
                self.ui.ota_progress.value = 1.0
                self.ui.ota_status_text.value = "OTA升级成功，设备即将重启..."
                self._set_ota_buttons_disabled(False)
                def disconnect_cb(dr):
                    self.log("设备已断开连接", "info")
                    self.safe_update()
                self.app.run_async(self.ble.disconnect_device(), disconnect_cb)
            else:
                self.log(f"OTA升级失败: {msg}", "error")
                self.ui.ota_progress.value = 0
                self.ui.ota_status_text.value = f"OTA失败: {msg}"
                self._set_ota_buttons_disabled(False)

        self._run_with_loading(btn, self.ble.ota_update(fw_path, progress_cb=progress_cb), callback, loading_text="OTA中...", timeout=timeout)

    def start_ota_url(self, event=None):
        """开始URL OTA升级"""
        if not self.check_connected():
            return
        url = self.ui.ota_url_field.value.strip() if self.ui.ota_url_field.value else ""
        if not url:
            self.log("请输入固件URL", "warn")
            return
        self.ota_running = True
        self._ota_url_check_done = False
        self._ota_url_start_time = time.time()
        self.ui.ota_progress.value = 0
        self.ui.ota_status_text.value = "正在启动URL OTA..."
        self.ble.set_ota_url_status_callback(self._ota_url_status_callback)
        self.log(f"开始URL OTA: {url}", "info")
        btn = self.ui.ota_tab.ota_url_btn
        self._set_ota_buttons_disabled(True)
        self._show_btn_loading(btn, "启动中...")

        def callback(result):
            if isinstance(result, Exception):
                self.log(f"URL OTA异常: {result}", "error")
                self.ble.set_ota_url_status_callback(None)
                self.ota_running = False
                self.ui.ota_progress.value = 0
                self._set_ota_buttons_disabled(False)
                self.ui.ota_status_text.value = f"OTA异常: {result}"
                self._hide_btn_loading(btn)
                return
            ok, msg = result
            self.log(f"URL OTA已触发: {msg}" if ok else f"URL OTA失败: {msg}", "success" if ok else "error")
            if not ok:
                self.ble.set_ota_url_status_callback(None)
                self.ota_running = False
                self.ui.ota_progress.value = 0
                self._set_ota_buttons_disabled(False)
                self.ui.ota_status_text.value = f"OTA失败: {msg}"
                self._hide_btn_loading(btn)
            else:
                self._hide_btn_loading(btn)
                self._show_btn_loading(None, "OTA中...")

        self.app.run_async(self.ble.ota_url_start(url), callback)

    def start_ota_default(self, event=None):
        """开始默认URL OTA升级"""
        if not self.check_connected():
            return
        self.ota_running = True
        self._ota_url_check_done = False
        self._ota_url_start_time = time.time()
        self.ui.ota_progress.value = 0
        self.ui.ota_status_text.value = "正在启动默认URL OTA..."
        self.ble.set_ota_url_status_callback(self._ota_url_status_callback)
        self.log("开始默认URL OTA...", "info")
        btn = self.ui.ota_tab.ota_default_btn
        self._set_ota_buttons_disabled(True)
        self._show_btn_loading(btn, "启动中...")

        def callback(result):
            if isinstance(result, Exception):
                self.log(f"URL OTA异常: {result}", "error")
                self.ble.set_ota_url_status_callback(None)
                self.ota_running = False
                self.ui.ota_progress.value = 0
                self._set_ota_buttons_disabled(False)
                self.ui.ota_status_text.value = f"OTA异常: {result}"
                self._hide_btn_loading(btn)
                return
            ok, msg = result
            self.log(f"默认URL OTA已触发: {msg}" if ok else f"默认URL OTA失败: {msg}", "success" if ok else "error")
            if not ok:
                self.ble.set_ota_url_status_callback(None)
                self.ota_running = False
                self.ui.ota_progress.value = 0
                self._set_ota_buttons_disabled(False)
                self.ui.ota_status_text.value = f"OTA失败: {msg}"
                self._hide_btn_loading(btn)
            else:
                self._hide_btn_loading(btn)
                self._show_btn_loading(None, "OTA中...")

        self.app.run_async(self.ble.ota_url_start(None), callback)

    def abort_ota(self, event=None):
        """中止OTA"""
        if not self.ota_running:
            return
        
        def do_abort(dlg):
            dlg.open = False
            self.safe_update()
            self._do_abort_ota()
        
        dlg = ft.AlertDialog(
            title=ft.Text("确认中止"),
            content=ft.Text("确定要中止OTA升级吗？"),
            actions=[
                ft.TextButton("取消", on_click=lambda e: setattr(dlg, 'open', False) or self.safe_update()),
                ft.TextButton("中止", on_click=lambda e: do_abort(dlg)),
            ],
        )
        self.page.overlay.append(dlg)
        dlg.open = True
        self.safe_update()

    def _do_abort_ota(self):
        """执行中止OTA"""
        self.ui.ota_progress.value = 0
        self.ui.ota_status_text.value = "正在中止OTA..."
        self.log("中止OTA...", "warn")
        self.safe_update()
        def callback(result):
            self.ota_running = False
            self._set_ota_buttons_disabled(False)
            self._hide_btn_loading(None)
            self.ui.ota_progress.value = 0
            self.ui.ota_status_text.value = "OTA已中止"
            self.log("OTA已中止", "warn")
        self.app.run_async(self.ble.ota_abort(), callback)
