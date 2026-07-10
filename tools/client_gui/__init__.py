"""
ESP32 BLE Device Manager - Flet GUI Client
跨平台蓝牙BLE设备管理器图形界面客户端 (macOS / Windows)

依赖: pip install flet bleak
"""

import asyncio
import time
import os
import sys
import threading
import traceback

import flet as ft

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.path.dirname(os.path.abspath(sys.argv[0])))

from client_gui.ble_core import BleCore, EFFECT_MAP
from client_gui.gui_components import GuiComponents
from client_gui.gui_handlers import GuiHandlers


class BleDeviceManager:
    """BLE设备管理器主应用"""

    def __init__(self, version="1.2.1"):
        self.event_loop = asyncio.new_event_loop()
        self.ble = BleCore(event_loop=self.event_loop)
        self.devices = []
        self.scan_lock = False
        self.ota_running = False
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()
        self.ble.selected_device_info = None
        self.effect_map = EFFECT_MAP
        self.page = None
        self.version = version

    def _run_loop(self):
        asyncio.set_event_loop(self.event_loop)
        self.event_loop.run_forever()

    def run_async(self, coro, callback=None):
        def _done(fut):
            try:
                result = fut.result()
                if callback:
                    def _call_callback(result=result):
                        try:
                            callback(result)
                        except Exception as e:
                            error_msg = f"Callback error: {e}"
                            print(error_msg)
                            traceback.print_exc()
                    if self.page:
                        self.page.run_thread(_call_callback)
                    else:
                        _call_callback()
            except Exception as e:
                error_msg = f"Async task error: {e}"
                print(error_msg)
                traceback.print_exc()
                if callback:
                    def _call_callback_err(err=e):
                        try:
                            callback(err)
                        except Exception as ex:
                            error_msg = f"Callback error: {ex}"
                            print(error_msg)
                            traceback.print_exc()
                    if self.page:
                        self.page.run_thread(_call_callback_err)
                    else:
                        _call_callback_err()
        fut = asyncio.run_coroutine_threadsafe(coro, self.event_loop)
        fut.add_done_callback(_done)
        return fut

    def main(self, page: ft.Page):
        page.title = f"BLE Device Manager v{self.version}"
        page.window.width = 1280
        page.window.height = 800
        page.window.min_width = 1280
        page.window.min_height = 800
        page.window.max_width = 1280
        page.window.max_height = 800
        page.window.resizable = False
        page.window.maximizable = False
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
        self.handlers = GuiHandlers(self)
        self.ui = GuiComponents(self)
        self.ui.build_ui(page)
        self.handlers.set_ui(self.ui)
        
        # 设置BLE日志回调
        def ble_log_callback(msg, direction="info"):
            self.handlers.connection.ble_log(msg, direction)
        self.ble.set_log_callback(ble_log_callback)
        
        # 设置BLE断开连接回调
        def ble_disconnect_callback():
            self.handlers.connection.update_connection_ui(False)
        self.ble.set_disconnect_callback(ble_disconnect_callback)
        
        # 设置页面对象用于UI更新
        self.ble.set_page(page)
        
        # 初始化连接状态UI（未连接状态）
        self.handlers.connection.update_connection_ui(False)
        
        # 进入app时自动开始扫描蓝牙设备
        self.handlers.handle_scan()

    def run(self):
        ft.run(self.main)


def main(version="1.2.1"):
    app = BleDeviceManager(version)
    app.run()


if __name__ == "__main__":
    main()
