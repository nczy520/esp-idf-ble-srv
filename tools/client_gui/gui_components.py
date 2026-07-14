"""
GUI组件构建模块
负责创建和管理所有UI组件
"""

import flet as ft

from client_gui.components.app_bar import AppBarComponent
from client_gui.components.left_panel import LeftPanelComponent
from client_gui.components.log_panel import LogPanelComponent
from client_gui.components.tabs.info_tab import InfoTabComponent
from client_gui.components.tabs.led_tab import LEDTabComponent
from client_gui.components.tabs.wifi_tab import WiFiTabComponent
from client_gui.components.tabs.log_tab import LogTabComponent
from client_gui.components.tabs.ota_tab import OTATabComponent
from client_gui.components.tabs.custom_cmd_tab import CustomCmdTabComponent


class GuiComponents:
    """GUI组件构建类"""

    def __init__(self, app):
        self.app = app
        self.page = None

    def build_ui(self, page):
        """构建主UI"""
        self.page = page

        # 构建顶部状态栏
        app_bar = AppBarComponent(self.app)
        app_bar.build(page)
        self.status_text = app_bar.status_text
        self.connect_toggle_btn = app_bar.connect_toggle_btn
        self.status_badge = app_bar.status_badge

        # 构建左侧设备面板
        left_panel_comp = LeftPanelComponent(self.app)
        left_panel = left_panel_comp.build()
        self.filter_field = left_panel_comp.filter_field
        self.pin_field = left_panel_comp.pin_field
        self.device_list = left_panel_comp.device_list
        self.scan_loading = left_panel_comp.scan_loading
        self.scan_btn = left_panel_comp.scan_btn
        self.scan_timeout_dropdown = left_panel_comp
        self.scan_timeout_btn = left_panel_comp.scan_timeout_btn

        # 构建Tab组件
        info_tab_comp = InfoTabComponent(self.app)
        info_tab = info_tab_comp.build()
        self.info_tab = info_tab_comp
        self.info_display = info_tab_comp.info_display
        self.info_overlay = info_tab_comp.info_overlay

        led_tab_comp = LEDTabComponent(self.app)
        led_tab = led_tab_comp.build()
        self.led_tab = led_tab_comp
        self.led_status_text = led_tab_comp.led_status_text
        self.r_slider = led_tab_comp.r_slider
        self.g_slider = led_tab_comp.g_slider
        self.b_slider = led_tab_comp.b_slider
        self.r_val_text = led_tab_comp.r_val_text
        self.g_val_text = led_tab_comp.g_val_text
        self.b_val_text = led_tab_comp.b_val_text
        self.color_box = led_tab_comp.color_box
        self.color_code_text = led_tab_comp.color_code_text
        self.effect_menu = led_tab_comp.effect_menu
        self.speed_slider = led_tab_comp.speed_slider
        self.led_overlay = led_tab_comp.led_overlay

        wifi_tab_comp = WiFiTabComponent(self.app)
        wifi_tab = wifi_tab_comp.build()
        self.wifi_tab = wifi_tab_comp
        self.ssid_field = wifi_tab_comp.ssid_field
        self.password_field = wifi_tab_comp.password_field
        self.wifi_display = wifi_tab_comp.wifi_display
        self.wifi_overlay = wifi_tab_comp.wifi_overlay

        log_tab_comp = LogTabComponent(self.app)
        log_tab = log_tab_comp.build()
        self.log_tab = log_tab_comp
        self.log_file_list = log_tab_comp.log_file_list
        self.log_content_display = log_tab_comp.log_content_display
        self.log_overlay = log_tab_comp.disconnected_overlay

        ota_tab_comp = OTATabComponent(self.app)
        ota_tab = ota_tab_comp.build()
        self.ota_tab = ota_tab_comp
        self.fw_path_field = ota_tab_comp.fw_path_field
        self.ota_url_field = ota_tab_comp.ota_url_field
        self.ota_progress = ota_tab_comp.ota_progress
        self.ota_status_text = ota_tab_comp.ota_status_text
        self.ota_bt_btn = ota_tab_comp.ota_bt_btn
        self.ota_url_btn = ota_tab_comp.ota_url_btn
        self.ota_default_btn = ota_tab_comp.ota_default_btn
        self.ota_abort_btn = ota_tab_comp.ota_abort_btn
        self.ota_overlay = ota_tab_comp.ota_overlay

        custom_cmd_tab_comp = CustomCmdTabComponent(self.app)
        custom_cmd_tab = custom_cmd_tab_comp.build()
        self.custom_cmd_tab = custom_cmd_tab_comp
        self.cmd_log_view = custom_cmd_tab_comp.cmd_log_view
        self.cmd_input = custom_cmd_tab_comp.cmd_input
        self.custom_cmd_overlay = custom_cmd_tab_comp.custom_cmd_overlay

        # 构建TabBar
        self.tabs = ft.Tabs(
            length=6,
            expand=True,
            on_change=self._on_tab_changed,
            content=ft.Column(
                expand=True,
                controls=[
                    ft.TabBar(
                        tabs=[
                            ft.Tab(label="设备信息", icon=ft.Icons.INFO_OUTLINE),
                            ft.Tab(label="LED 控制", icon=ft.Icons.LIGHTBULB_OUTLINE),
                            ft.Tab(label="WiFi", icon=ft.Icons.WIFI),
                            ft.Tab(label="日志", icon=ft.Icons.ARTICLE_OUTLINED),
                            ft.Tab(label="OTA 升级", icon=ft.Icons.SYSTEM_UPDATE),
                            ft.Tab(label="自定义命令", icon=ft.Icons.CODE),
                        ],
                        indicator_color=ft.Colors.BLUE,
                    ),
                    ft.TabBarView(
                        expand=True,
                        controls=[info_tab, led_tab, wifi_tab, log_tab, ota_tab, custom_cmd_tab],
                    ),
                ],
            ),
        )

        # 构建日志面板
        log_panel_comp = LogPanelComponent(self.app)
        bottom_logs = log_panel_comp.build()
        self.log_view = log_panel_comp.log_view
        self.ble_log_view = log_panel_comp.ble_log_view

        # 右侧内容区域
        right_panel = ft.Container(
            content=ft.Column([
                ft.Container(content=self.tabs, padding=ft.padding.Padding(20, 16, 20, 0), bgcolor=ft.Colors.WHITE, expand=True),
                ft.Container(content=bottom_logs, bgcolor=ft.Colors.WHITE, padding=ft.padding.Padding(20, 0, 20, 20), height=280),
            ], spacing=0, expand=True),
            bgcolor=ft.Colors.WHITE,
            expand=True,
        )

        # 全局Loading覆盖层
        self.loading_overlay = ft.Container(
            content=ft.Column([
                ft.ProgressRing(width=50, height=50, stroke_width=4, color=ft.Colors.BLUE),
                ft.Text("处理中...", size=16, color=ft.Colors.WHITE, weight=ft.FontWeight.W_500),
            ], spacing=16, horizontal_alignment=ft.CrossAxisAlignment.CENTER, alignment=ft.MainAxisAlignment.CENTER),
            alignment=ft.alignment.Alignment(0, 0),
            expand=True,
            bgcolor=ft.Colors.with_opacity(0.6, ft.Colors.BLACK),
            visible=False,
        )

        page.add(
            ft.Stack([
                ft.Container(
                    content=ft.Row([left_panel, right_panel], spacing=0, expand=True),
                    bgcolor=ft.Colors.WHITE,
                    expand=True,
                ),
                self.loading_overlay,
            ], expand=True)
        )

    def _on_tab_changed(self, e):
        """Tab切换时触发对应handler的on_tab_selected"""
        try:
            idx = e.control.selected_index if hasattr(e, 'control') else None
            if idx is None:
                return
            handlers = self.app.handlers
            if idx == 3:  # 日志Tab
                handlers.log_control.on_tab_selected()
        except Exception:
            pass
