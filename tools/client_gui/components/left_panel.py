"""
左侧设备列表面板组件
"""

import flet as ft
from client_gui.components.base import BaseComponent


class LeftPanelComponent(BaseComponent):
    """左侧设备列表面板"""

    def __init__(self, app):
        super().__init__(app)
        self.device_list = None
        self.scan_btn = None
        self.scan_loading = None
        self.filter_field = None
        self.pin_field = None
        self.scan_timeout_btn = None
        self.scan_timeout_value = "5"

    def _on_timeout_changed(self, e):
        """扫描时间选择变化"""
        self.scan_timeout_value = e.control.data
        # 更新按钮显示的文本
        self.scan_timeout_btn.content.content.value = e.control.text
        self.scan_timeout_btn.update()

    def get_scan_timeout(self):
        """获取当前扫描时间"""
        try:
            return int(self.scan_timeout_value) if self.scan_timeout_value else 5
        except (ValueError, TypeError):
            return 5

    def build(self):
        """构建左侧设备面板"""
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

        self.filter_field = ft.TextField(
            label="设备名过滤",
            value="BLE-SRV",
            prefix_icon=ft.Icons.FILTER_LIST,
            **input_style,
        )

        self.pin_field = ft.TextField(
            label="PIN",
            prefix_icon=ft.Icons.LOCK_OUTLINE,
            password=True,
            value="112233",
            input_filter=ft.NumbersOnlyInputFilter(),
            max_length=8,
            counter_style=ft.TextStyle(size=0),
            **input_style,
        )

        self.device_list = ft.ListView(spacing=4, expand=True, padding=4)

        self.scan_loading = ft.Container(
            content=ft.Column([
                ft.ProgressRing(width=40, height=40, stroke_width=3, color=ft.Colors.BLUE),
                ft.Text("扫描中...", size=13, color=ft.Colors.ON_SURFACE_VARIANT, weight=ft.FontWeight.W_500),
            ], spacing=12, horizontal_alignment=ft.CrossAxisAlignment.CENTER),
            alignment=ft.alignment.Alignment(0, 0.25),
            expand=True,
            visible=False,
            bgcolor=ft.Colors.with_opacity(0.7, ft.Colors.WHITE),
            border_radius=6,
        )

        self.scan_btn = ft.ElevatedButton(
            "扫描设备",
            icon=ft.Icons.SEARCH_ROUNDED,
            on_click=lambda e: self.safe_call("handle_scan", e),
            bgcolor=ft.Colors.BLUE,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=6), padding=ft.padding.Padding(20, 0, 20, 0)),
            height=36,
        )

        # 扫描时间选择按钮
        timeout_items = [
            ft.PopupMenuItem(content=ft.Text("5秒"), data="5", on_click=self._on_timeout_changed),
            ft.PopupMenuItem(content=ft.Text("10秒"), data="10", on_click=self._on_timeout_changed),
            ft.PopupMenuItem(content=ft.Text("15秒"), data="15", on_click=self._on_timeout_changed),
            ft.PopupMenuItem(content=ft.Text("20秒"), data="20", on_click=self._on_timeout_changed),
            ft.PopupMenuItem(content=ft.Text("30秒"), data="30", on_click=self._on_timeout_changed),
        ]
        self.scan_timeout_btn = ft.Container(
            content=ft.PopupMenuButton(
                content=ft.Container(
                    content=ft.Text("5秒", size=13, color=ft.Colors.ON_SURFACE, text_align=ft.TextAlign.CENTER),
                    width=60,
                    height=36,
                    alignment=ft.alignment.Alignment(0, 0),
                    border=ft.border.BorderSide(1, ft.Colors.OUTLINE_VARIANT),
                    border_radius=6,
                    bgcolor=ft.Colors.SURFACE_CONTAINER_HIGHEST,
                ),
                items=timeout_items,
            ),
            height=36,
        )

        return ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Icon(ft.Icons.DEVICES, size=18, color=ft.Colors.BLUE),
                    ft.Text("设备列表", size=15, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE),
                ], spacing=6),
                ft.Container(height=10),
                ft.Row([
                    ft.Container(content=self.filter_field, expand=True),
                    ft.Container(content=self.pin_field, width=120),
                ], spacing=8),
                ft.Container(height=10),
                ft.Stack([
                    ft.Container(
                        content=self.device_list,
                        border=ft.border.BorderSide(1, ft.Colors.OUTLINE_VARIANT),
                        bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
                        expand=True,
                        padding=4,
                    ),
                    self.scan_loading,
                ], expand=True),
                ft.Container(height=10),
                ft.Row([
                    ft.Container(content=self.scan_btn, height=36, alignment=ft.alignment.Alignment(0, 0)),
                    ft.Container(content=self.scan_timeout_btn, height=36, alignment=ft.alignment.Alignment(0, 0)),
                ], spacing=8, alignment=ft.MainAxisAlignment.CENTER),
            ], spacing=0, expand=True),
            width=300,
            padding=12,
            bgcolor="#f5f5f5",
        )
