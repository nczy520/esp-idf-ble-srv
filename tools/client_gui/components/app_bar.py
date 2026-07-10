"""
顶部状态栏组件
"""

import flet as ft
from client_gui.components.base import BaseComponent


class AppBarComponent(BaseComponent):
    """顶部状态栏组件"""

    def __init__(self, app):
        super().__init__(app)
        self.status_text = None
        self.connect_toggle_btn = None
        self.status_badge = None

    def build(self, page):
        """构建顶部状态栏"""
        self.status_text = ft.Text("未选择设备", size=13, color=ft.Colors.ON_SURFACE_VARIANT, weight=ft.FontWeight.W_500)
        self.connect_toggle_btn = ft.ElevatedButton(
            "连接",
            icon=ft.Icons.LINK_ROUNDED,
            on_click=lambda e: self.safe_call("handle_connect_toggle", e),
            disabled=True,
            bgcolor=ft.Colors.BLUE,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=6), padding=ft.padding.Padding(16, 0, 16, 0)),
        )
        self.status_badge = ft.Container(
            content=ft.Row([
                ft.Icon(ft.Icons.CIRCLE, size=10, color=ft.Colors.RED_400),
                ft.Text("未连接", size=12, color=ft.Colors.RED_400, weight=ft.FontWeight.W_500),
            ], spacing=8, alignment=ft.MainAxisAlignment.CENTER),
            bgcolor=ft.Colors.with_opacity(0.1, ft.Colors.RED_400),
            border_radius=16,
            padding=ft.padding.Padding(14, 6, 14, 6),
            border=ft.border.BorderSide(1, ft.Colors.with_opacity(0.2, ft.Colors.RED_400)),
        )

        page.appbar = ft.AppBar(
            title=ft.Row([
                ft.Icon(ft.Icons.BLUETOOTH, size=22, color=ft.Colors.BLUE),
                ft.Text("BLE Device Manager", size=18, weight=ft.FontWeight.W_700, color=ft.Colors.ON_SURFACE),
            ], spacing=10),
            center_title=False,
            bgcolor=ft.Colors.SURFACE,
            elevation=2,
            actions=[
                ft.Container(
                    content=ft.Row([
                        ft.Icon(ft.Icons.DEVICES, size=16, color=ft.Colors.ON_SURFACE_VARIANT),
                        self.status_text,
                        ft.Container(width=12),
                        self.connect_toggle_btn,
                        ft.Container(width=12),
                        self.status_badge,
                    ], spacing=6, alignment=ft.MainAxisAlignment.CENTER),
                    padding=ft.padding.Padding(0, 0, 16, 0),
                ),
            ],
        )
