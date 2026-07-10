"""
WiFi配置Tab组件
"""

import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class WiFiTabComponent(BaseTabComponent):
    """WiFi配置Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.ssid_field = None
        self.password_field = None
        self.wifi_display = None
        self.wifi_connect_btn = None
        self.wifi_status_btn = None
        self.wifi_forget_btn = None
        self.wifi_ntp_btn = None
        self.wifi_overlay = None

    def build(self):
        """构建WiFi配置Tab"""
        # 统一的输入框样式
        input_style = {
            "dense": True,
            "border_radius": 6,
            "filled": True,
            "fill_color": ft.Colors.SURFACE_CONTAINER_HIGHEST,
            "border_color": ft.Colors.OUTLINE_VARIANT,
            "height": 40,
            "text_size": 13,
            "content_padding": ft.padding.Padding(12, 8, 12, 8),
        }

        self.ssid_field = ft.TextField(
            label="SSID",
            width=320,
            prefix_icon=ft.Icons.WIFI,
            **input_style,
        )
        self.password_field = ft.TextField(
            label="密码",
            width=320,
            password=True,
            can_reveal_password=True,
            prefix_icon=ft.Icons.LOCK,
            **input_style,
        )
        self.wifi_display = ft.TextField(
            multiline=True,
            read_only=True,
            min_lines=5,
            max_lines=10,
            border_radius=6,
            text_size=12,
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
            border_color=ft.Colors.OUTLINE_VARIANT,
            filled=True,
            label="WiFi 状态",
        )

        self.wifi_connect_btn = self._action_btn("连接WiFi", ft.Icons.WIFI, "wifi_connect", color=ft.Colors.BLUE)
        self.wifi_status_btn = self._action_btn("查询状态", ft.Icons.HELP_OUTLINE, "wifi_status")
        self.wifi_forget_btn = self._action_btn("忘记网络", ft.Icons.WIFI_OFF, "wifi_forget", color=ft.Colors.RED)
        self.wifi_ntp_btn = self._action_btn("NTP同步", ft.Icons.SCHEDULE, "wifi_ntp")

        self.wifi_overlay = self._build_overlay()

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    ft.Text("WiFi 配置", size=16, weight=ft.FontWeight.W_700, color=ft.Colors.ON_SURFACE),
                    ft.Container(height=12),
                    ft.Row([
                        self.ssid_field,
                        self.password_field,
                    ], spacing=16, alignment=ft.MainAxisAlignment.START),
                    ft.Container(height=16),
                    ft.Row([
                        self.wifi_connect_btn,
                        self.wifi_status_btn,
                        self.wifi_forget_btn,
                        self.wifi_ntp_btn,
                    ], spacing=10, wrap=True),
                    ft.Container(height=20),
                    self.wifi_display,
                ], spacing=0, scroll=ft.ScrollMode.AUTO),
                padding=20,
                bgcolor=ft.Colors.WHITE,
            ),
            self.wifi_overlay,
        ], expand=True)
