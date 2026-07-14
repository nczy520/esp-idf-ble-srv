"""
GUI事件处理模块
处理所有UI事件和BLE交互逻辑
"""

import flet as ft

from client_gui.handlers.base import BaseHandler
from client_gui.handlers.connection import ConnectionHandler
from client_gui.handlers.device_info import DeviceInfoHandler
from client_gui.handlers.led_control import LEDControlHandler
from client_gui.handlers.wifi_control import WiFiControlHandler
from client_gui.handlers.ota_control import OTAControlHandler
from client_gui.handlers.custom_cmd_control import CustomCmdHandler


class GuiHandlers:
    """GUI事件处理类 - 聚合所有功能模块"""

    def __init__(self, app):
        self.app = app
        self.page = None
        self.ui = None

        # 初始化各功能模块
        self.connection = ConnectionHandler(app)
        self.device_info = DeviceInfoHandler(app)
        self.led_control = LEDControlHandler(app)
        self.wifi_control = WiFiControlHandler(app)
        self.ota_control = OTAControlHandler(app)
        self.custom_cmd = CustomCmdHandler(app)

    def set_ui(self, ui):
        self.ui = ui
        self.page = ui.page
        # 设置各模块的UI
        self.connection.set_ui(ui)
        self.device_info.set_ui(ui)
        self.led_control.set_ui(ui)
        self.wifi_control.set_ui(ui)
        self.ota_control.set_ui(ui)
        self.custom_cmd.set_ui(ui)

    # === 日志功能（委托给BaseHandler）===
    def log(self, msg, level="info"):
        self.connection.log(msg, level)

    def clear_log(self, event=None):
        self.connection.clear_log(event)

    def clear_ble_log(self, event=None):
        self.connection.clear_ble_log(event)

    # === 连接管理（委托给ConnectionHandler）===
    def handle_scan(self, event=None):
        self.connection.handle_scan(event)

    def handle_device_click(self, event):
        self.connection.handle_device_click(event)

    def handle_connect_toggle(self, event=None):
        self.connection.handle_connect_toggle(event)

    def handle_connect(self, event=None):
        self.connection.handle_connect(event)

    def handle_disconnect(self, event=None):
        self.connection.handle_disconnect(event)

    # === 设备信息（委托给DeviceInfoHandler）===
    def read_device_info(self, event=None):
        self.device_info.read_device_info(event)

    def read_memory_info(self, event=None):
        self.device_info.read_memory_info(event)

    def read_cpu_info(self, event=None):
        self.device_info.read_cpu_info(event)

    def read_flash_info(self, event=None):
        self.device_info.read_flash_info(event)

    def read_partitions(self, event=None):
        self.device_info.read_partitions(event)

    def restart_device(self, event=None):
        self.device_info.restart_device(event)

    # === LED控制（委托给LEDControlHandler）===
    def color_changed(self, event=None):
        self.led_control.color_changed(event)

    def led_on(self, event=None):
        self.led_control.led_on(event)

    def led_off(self, event=None):
        self.led_control.led_off(event)

    def led_status(self, event=None):
        self.led_control.led_status(event)

    def led_set_color(self, event=None):
        self.led_control.led_set_color(event)

    def led_set_effect(self, event=None):
        self.led_control.led_set_effect(event)

    # === WiFi控制（委托给WiFiControlHandler）===
    def wifi_connect(self, event=None):
        self.wifi_control.wifi_connect(event)

    def wifi_status(self, event=None):
        self.wifi_control.wifi_status(event)

    def wifi_forget(self, event=None):
        self.wifi_control.wifi_forget(event)

    def wifi_ntp(self, event=None):
        self.wifi_control.wifi_ntp(event)

    # === OTA升级（委托给OTAControlHandler）===
    def pick_firmware(self, event=None):
        self.ota_control.pick_firmware(event)

    def start_ota_bt(self, event=None):
        self.ota_control.start_ota_bt(event)

    def start_ota_url(self, event=None):
        self.ota_control.start_ota_url(event)

    def start_ota_default(self, event=None):
        self.ota_control.start_ota_default(event)

    def abort_ota(self, event=None):
        self.ota_control.abort_ota(event)

    # === 自定义命令（委托给CustomCmdHandler）===
    def send_custom_cmd(self, event=None):
        self.custom_cmd.send_custom_cmd(event)

    def clear_custom_cmd_log(self, event=None):
        self.custom_cmd.clear_custom_cmd_log(event)

    def on_custom_cmd_format_change(self, event=None):
        self.custom_cmd.on_custom_cmd_format_change(event)

    def on_auto_send_toggle(self, event=None):
        self.custom_cmd.on_auto_send_toggle(event)
