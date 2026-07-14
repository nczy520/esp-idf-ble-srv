"""
日志浏览Tab组件
"""

import webbrowser
import flet as ft
from datetime import datetime
from client_gui.components.tabs.base_tab import BaseTabComponent


class LogTabComponent(BaseTabComponent):
    """日志浏览Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.log_file_list = None
        self.log_content_display = None
        self.disconnected_overlay = None
        self.loading_overlay = None
        self.loading_text = None
        self.refresh_btn = None
        self.http_switch = None
        self.http_url_text = None
        self.http_status_icon = None
        self.open_browser_btn = None
        self.url_container = None
        self.selected_file_name = None
        self._current_url = ""

    def _format_size(self, size):
        if size >= 1024 * 1024:
            return f"{size / (1024 * 1024):.2f} MB"
        elif size >= 1024:
            return f"{size / 1024:.2f} KB"
        return f"{size} B"

    def _format_time(self, timestamp):
        try:
            return datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S")
        except Exception:
            return "未知"

    def _open_in_browser(self, e):
        if self._current_url:
            webbrowser.open(self._current_url)

    def _build_loading_overlay(self):
        self.loading_text = ft.Text("处理中...", size=14, color=ft.Colors.GREY_600)
        return ft.Container(
            content=ft.Column([
                ft.ProgressRing(width=40, height=40, stroke_width=3),
                self.loading_text,
            ], spacing=12, horizontal_alignment=ft.CrossAxisAlignment.CENTER, alignment=ft.MainAxisAlignment.CENTER),
            alignment=ft.alignment.Alignment(0, 0),
            expand=True,
            bgcolor=ft.Colors.with_opacity(0.5, ft.Colors.WHITE),
            visible=False,
        )

    def build(self):
        self.log_file_list = ft.ListView(
            spacing=4,
            expand=True,
            padding=8,
        )

        self.log_content_display = ft.TextField(
            multiline=True,
            read_only=True,
            min_lines=20,
            max_lines=100,
            border_radius=6,
            text_size=12,
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
            border_color=ft.Colors.OUTLINE_VARIANT,
            filled=True,
            label="日志内容",
            expand=True,
        )

        self.refresh_btn = self._action_btn("刷新列表", ft.Icons.REFRESH, "refresh_log_list")

        self.http_status_icon = ft.Icon(
            ft.Icons.CIRCLE,
            size=12,
            color=ft.Colors.GREY_400,
        )

        self.http_switch = ft.Switch(
            value=False,
            label="HTTP服务器",
            label_position=ft.LabelPosition.LEFT,
            on_change=lambda e: self.app.run_async(self._http_switch_changed(e)),
        )

        self.open_browser_btn = ft.IconButton(
            icon=ft.Icons.OPEN_IN_BROWSER,
            icon_size=18,
            tooltip="在浏览器中打开",
            on_click=self._open_in_browser,
            visible=False,
            padding=4,
        )

        self.http_url_text = ft.Text(
            "",
            size=12,
            color=ft.Colors.BLUE_600,
            selectable=True,
            expand=True,
        )

        self.disconnected_overlay = self._build_overlay()
        self.loading_overlay = self._build_loading_overlay()

        http_control_row = ft.Row([
            self.http_status_icon,
            self.http_switch,
        ], spacing=4, alignment=ft.MainAxisAlignment.START)

        self.url_container = ft.Container(
            content=ft.Row([
                self.http_url_text,
                self.open_browser_btn,
            ], spacing=4, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
            padding=ft.Padding(left=8, right=8, top=4, bottom=4),
            bgcolor=ft.Colors.BLUE_50,
            border_radius=4,
            visible=False,
        )

        left_panel = ft.Container(
            content=ft.Column([
                ft.Text("日志文件", size=14, weight=ft.FontWeight.W_600, color=ft.Colors.ON_SURFACE),
                ft.Container(height=8),
                ft.Container(
                    content=self.log_file_list,
                    border=ft.border.BorderSide(1, ft.Colors.OUTLINE_VARIANT),
                    border_radius=6,
                    bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
                    expand=True,
                ),
            ], spacing=0, expand=True),
            width=320,
            padding=12,
            bgcolor=ft.Colors.WHITE,
        )

        right_panel = ft.Container(
            content=ft.Column([
                ft.Row([
                    self.refresh_btn,
                    ft.VerticalDivider(width=16),
                    http_control_row,
                ], spacing=10),
                self.url_container,
                ft.Container(height=12),
                self.log_content_display,
            ], spacing=0, expand=True),
            padding=12,
            bgcolor=ft.Colors.WHITE,
            expand=True,
        )

        return ft.Stack([
            ft.Row([
                left_panel,
                ft.VerticalDivider(width=1, color=ft.Colors.OUTLINE_VARIANT),
                right_panel,
            ], expand=True),
            self.disconnected_overlay,
            self.loading_overlay,
        ], expand=True)

    async def _http_switch_changed(self, e):
        handler = self.app.handlers.log_control
        if handler:
            await handler.toggle_http_server(e.control.value)

    def update_http_status(self, running, url=""):
        self.http_switch.value = running
        self._current_url = url if url else ""
        if running and url:
            self.http_status_icon.color = ft.Colors.GREEN
            self.http_url_text.value = f"浏览器访问: {url}"
            self.url_container.visible = True
            self.open_browser_btn.visible = True
        else:
            self.http_status_icon.color = ft.Colors.GREY_400
            self.http_url_text.value = ""
            self._current_url = ""
            self.url_container.visible = False
            self.open_browser_btn.visible = False
