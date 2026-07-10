"""
日志面板组件
"""

import flet as ft
from client_gui.components.base import BaseComponent


class LogPanelComponent(BaseComponent):
    """日志面板组件"""

    def __init__(self, app):
        super().__init__(app)
        self.log_view = None
        self.ble_log_view = None

    def build(self):
        """构建日志面板"""
        self.log_view = ft.ListView(spacing=1, expand=True, auto_scroll=True, padding=4, controls=[])
        log_container = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Row([
                        ft.Icon(ft.Icons.TERMINAL, size=14, color=ft.Colors.BLUE),
                        ft.Text("系统日志", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE),
                    ], spacing=6),
                    ft.Container(expand=True),
                    ft.IconButton(
                        ft.Icons.DELETE_OUTLINE,
                        icon_size=16,
                        on_click=self._on_clear_log,
                        tooltip="清空日志",
                        icon_color=ft.Colors.ON_SURFACE_VARIANT,
                    ),
                ], spacing=4, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                ft.Container(height=4),
                ft.Container(
                    content=self.log_view,
                    bgcolor="#0d1117",
                    border_radius=6,
                    padding=6,
                    expand=True,
                    border=ft.border.BorderSide(1, ft.Colors.with_opacity(0.1, ft.Colors.WHITE)),
                ),
            ], spacing=0, expand=True),
            padding=8,
            bgcolor=ft.Colors.SURFACE,
            border_radius=ft.border_radius.BorderRadius(top_left=6, top_right=0, bottom_left=6, bottom_right=0),
            expand=True,
        )

        self.ble_log_view = ft.ListView(spacing=1, expand=True, auto_scroll=True, padding=4, controls=[])
        ble_log_container = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Row([
                        ft.Icon(ft.Icons.BLUETOOTH, size=14, color=ft.Colors.BLUE),
                        ft.Text("蓝牙通讯", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE),
                    ], spacing=6),
                    ft.Container(expand=True),
                    ft.IconButton(
                        ft.Icons.DELETE_OUTLINE,
                        icon_size=16,
                        on_click=self._on_clear_ble_log,
                        tooltip="清空日志",
                        icon_color=ft.Colors.ON_SURFACE_VARIANT,
                    ),
                ], spacing=4, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                ft.Container(height=4),
                ft.Container(
                    content=self.ble_log_view,
                    bgcolor="#0d1117",
                    border_radius=6,
                    padding=6,
                    expand=True,
                    border=ft.border.BorderSide(1, ft.Colors.with_opacity(0.1, ft.Colors.WHITE)),
                ),
            ], spacing=0, expand=True),
            padding=8,
            bgcolor=ft.Colors.SURFACE,
            border_radius=ft.border_radius.BorderRadius(top_left=0, top_right=6, bottom_left=0, bottom_right=6),
            expand=True,
        )

        return ft.Row([
            log_container,
            ble_log_container,
        ], spacing=4, expand=True)

    def _on_clear_log(self, event):
        handlers = self.get_handler()
        if handlers:
            handlers.clear_log(event)

    def _on_clear_ble_log(self, event):
        handlers = self.get_handler()
        if handlers:
            handlers.clear_ble_log(event)
