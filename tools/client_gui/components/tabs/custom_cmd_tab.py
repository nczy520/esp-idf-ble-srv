"""
自定义命令Tab组件
"""

import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class CustomCmdTabComponent(BaseTabComponent):
    """自定义命令Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.cmd_log_view = None
        self.cmd_input = None
        self.send_btn = None
        self.format_switch = None
        self.auto_send_switch = None
        self.interval_field = None
        self.clear_btn = None
        self.custom_cmd_overlay = None

    def build(self):
        """构建自定义命令Tab"""
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

        self.cmd_log_view = ft.ListView(
            spacing=1,
            expand=True,
            auto_scroll=True,
            padding=4,
            controls=[],
        )

        self.cmd_input = ft.TextField(
            label="输入命令",
            prefix_icon=ft.Icons.CODE,
            expand=True,
            **input_style,
            on_submit=lambda e: self.safe_call("send_custom_cmd", e),
        )
        self.send_btn = self._action_btn("发送", ft.Icons.SEND, "send_custom_cmd")

        self.clear_btn = ft.IconButton(
            ft.Icons.DELETE_OUTLINE,
            icon_size=20,
            on_click=lambda e: self.safe_call("clear_custom_cmd_log", e),
            tooltip="清空日志",
            icon_color=ft.Colors.ON_SURFACE_VARIANT,
        )

        self.format_switch = ft.SegmentedButton(
            selected=["ascii"],
            allow_multiple_selection=False,
            segments=[
                ft.Segment(value="ascii", label=ft.Text("ASCII")),
                ft.Segment(value="hex", label=ft.Text("HEX")),
            ],
            on_change=lambda e: self.safe_call("on_custom_cmd_format_change", e),
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(8, 0, 8, 0),
            ),
        )

        self.auto_send_switch = ft.Switch(
            label="定时发送",
            value=False,
            on_change=lambda e: self.safe_call("on_auto_send_toggle", e),
            label_position=ft.LabelPosition.LEFT,
            active_color=ft.Colors.BLUE,
        )

        self.interval_field = ft.TextField(
            label="间隔(ms)",
            value="2000",
            width=120,
            prefix_icon=ft.Icons.TIMER,
            disabled=True,
            keyboard_type=ft.KeyboardType.NUMBER,
            **input_style,
        )

        self.custom_cmd_overlay = self._build_overlay()

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    ft.Row([
                        ft.Row([
                            ft.Icon(ft.Icons.TERMINAL, size=14, color=ft.Colors.BLUE),
                            ft.Text("通讯日志", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE),
                        ], spacing=6),
                        ft.Container(expand=True),
                        ft.Row([
                            ft.Text("显示格式:", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                            self.format_switch,
                        ], spacing=6),
                        self.clear_btn,
                    ], spacing=4, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                    ft.Container(height=4),
                    ft.Container(
                        content=self.cmd_log_view,
                        bgcolor="#0d1117",
                        border_radius=6,
                        padding=6,
                        expand=True,
                        border=ft.border.BorderSide(1, ft.Colors.with_opacity(0.1, ft.Colors.WHITE)),
                    ),
                    ft.Container(height=12),
                    ft.Row([
                        ft.Text("命令输入", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                    ], spacing=8, alignment=ft.MainAxisAlignment.START),
                    ft.Container(height=8),
                    ft.Row([
                        ft.Container(content=self.cmd_input, expand=1),
                        self.send_btn,
                        ft.Container(width=50),
                        self.auto_send_switch,
                        self.interval_field,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                    ft.Container(height=6),
                    ft.Row([
                        ft.Text("提示: HEX格式输入空格分隔的十六进制，如: 01 02 03", size=11, color=ft.Colors.ON_SURFACE_VARIANT),
                    ], spacing=12, alignment=ft.MainAxisAlignment.START),
                ], spacing=0, expand=True),
                padding=16,
                bgcolor=ft.Colors.WHITE,
            ),
            self.custom_cmd_overlay,
        ], expand=True)
