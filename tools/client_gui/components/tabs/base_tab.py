"""
Tab组件基类
提供Tab组件通用功能
"""

import flet as ft
from client_gui.components.base import BaseComponent


class BaseTabComponent(BaseComponent):
    """Tab组件基类"""

    def _action_btn(self, text, icon, handler_name, color=ft.Colors.BLUE):
        """创建操作按钮"""
        def on_click(event):
            self.safe_call(handler_name, event)

        return ft.ElevatedButton(
            text,
            icon=icon,
            on_click=on_click,
            bgcolor=color,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(16, 0, 16, 0),
                elevation=2,
            ),
        )

    def _build_overlay(self):
        """构建未连接遮罩层"""
        return ft.Container(
            content=ft.Column([
                ft.Icon(ft.Icons.BLUETOOTH_DISABLED, size=48, color=ft.Colors.GREY_400),
                ft.Text("请先连接蓝牙设备", size=16, color=ft.Colors.GREY_400, weight=ft.FontWeight.W_600),
            ], spacing=12, horizontal_alignment=ft.CrossAxisAlignment.CENTER, alignment=ft.MainAxisAlignment.CENTER),
            alignment=ft.alignment.Alignment(0, 0),
            expand=True,
            bgcolor=ft.Colors.with_opacity(0.7, ft.Colors.WHITE),
            visible=False,
        )
