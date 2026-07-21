"""
日志浏览Tab组件
"""

import webbrowser
import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class LogTabComponent(BaseTabComponent):
    """日志浏览Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.disconnected_overlay = None
        self.loading_overlay = None
        self.http_switch = None
        self.http_url_text = None
        self.http_status_icon = None
        self.http_status_text = None
        self.open_browser_btn = None
        self._current_url = ""
        self.storage_info_text = None
        self.storage_usage_bar = None
        self.refresh_btn = None
        self.format_btn = None
        self.level_dropdown = None
        self.level_label = None
        self._level_value = "3"
        self._level_updating = False
        self.marker_input = None
        self.write_marker_btn = None

    def _build_loading_overlay(self):
        return ft.Container(
            content=ft.Column([
                ft.ProgressRing(width=24, height=24, stroke_width=2),
                ft.Text("处理中...", size=11, color=ft.Colors.GREY_600),
            ], spacing=6, horizontal_alignment=ft.CrossAxisAlignment.CENTER, alignment=ft.MainAxisAlignment.CENTER),
            alignment=ft.alignment.Alignment(0, 0),
            expand=True,
            bgcolor=ft.Colors.with_opacity(0.5, ft.Colors.WHITE),
            visible=False,
        )

    def update_storage_info(self, storage_info=None):
        if storage_info is None:
            self.storage_info_text.value = "存储类型: -- | 总容量: -- | 已用: -- | 可用: -- | 文件数: --"
            self.storage_usage_bar.value = 0
            return

        storage_type = storage_info.get_storage_type_name()
        total = storage_info._fmt_size(storage_info.total_size)
        used = storage_info._fmt_size(storage_info.used_size)
        free = storage_info._fmt_size(storage_info.free_size)
        count = storage_info.file_count

        self.storage_info_text.value = f"{storage_type} | 总: {total} | 已用: {used} | 可用: {free} | {count}个文件"

        if storage_info.total_size > 0:
            usage_percent = storage_info.used_size / storage_info.total_size
            self.storage_usage_bar.value = usage_percent
        else:
            self.storage_usage_bar.value = 0

        # 同步日志级别（避免触发on_select）
        if hasattr(storage_info, 'log_level') and self.level_label:
            level_map = {"1": "ERROR", "2": "WARN", "3": "INFO", "4": "DEBUG", "5": "VERBOSE"}
            self._level_updating = True
            self._level_value = str(storage_info.log_level)
            self.level_label.value = level_map.get(self._level_value, "INFO")
            self._level_updating = False

    def build(self):
        self.disconnected_overlay = self._build_overlay()
        self.loading_overlay = self._build_loading_overlay()

        self.http_status_icon = ft.Icon(
            ft.Icons.CIRCLE,
            size=10,
            color=ft.Colors.GREY_400,
        )

        self.http_status_text = ft.Text(
            "已停止",
            size=11,
            color=ft.Colors.GREY_600,
        )

        self.http_switch = ft.Switch(
            value=False,
            on_change=lambda e: self.app.run_async(self._http_switch_changed(e)),
        )

        self.http_url_text = ft.Text(
            "",
            size=11,
            color=ft.Colors.BLUE_700,
            selectable=True,
            visible=False,
        )

        self.storage_info_text = ft.Text(
            "存储类型: -- | 总容量: -- | 已用: -- | 可用: -- | 文件数: --",
            size=12,
            color=ft.Colors.ON_SURFACE_VARIANT,
        )

        self.storage_usage_bar = ft.ProgressBar(
            value=0,
            bar_height=8,
            border_radius=4,
            color=ft.Colors.BLUE_500,
            bgcolor=ft.Colors.SURFACE_CONTAINER_HIGH,
        )

        self.refresh_btn = ft.ElevatedButton(
            "刷新",
            icon=ft.Icons.REFRESH,
            on_click=lambda e: self.safe_call("log_control.log_refresh", e),
            bgcolor=ft.Colors.BLUE,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(16, 0, 16, 0),
                elevation=2,
            ),
        )

        self.format_btn = ft.ElevatedButton(
            "格式化",
            icon=ft.Icons.STORAGE,
            on_click=lambda e: self.safe_call("log_control.log_format", e),
            bgcolor=ft.Colors.ORANGE,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(16, 0, 16, 0),
                elevation=2,
            ),
        )

        self.level_label = ft.Text("INFO", size=13, color=ft.Colors.ON_SURFACE, text_align=ft.TextAlign.CENTER)

        def _on_level_selected(e):
            self._level_value = e.control.data
            level_map = {"1": "ERROR", "2": "WARN", "3": "INFO", "4": "DEBUG", "5": "VERBOSE"}
            self.level_label.value = level_map.get(self._level_value, "INFO")
            self.safe_call("log_control.log_set_level", e)
            self.app.page.update()

        self.level_dropdown = ft.PopupMenuButton(
            content=ft.Container(
                content=ft.Row([
                    ft.Text("级别:", size=13, color=ft.Colors.ON_SURFACE_VARIANT),
                    self.level_label,
                    ft.Icon(ft.Icons.ARROW_DROP_DOWN, size=16, color=ft.Colors.ON_SURFACE_VARIANT),
                ], spacing=4, alignment=ft.MainAxisAlignment.CENTER),
                padding=ft.padding.Padding(12, 0, 8, 0),
                height=32,
                alignment=ft.alignment.Alignment(0, 0),
                border=ft.border.BorderSide(1, ft.Colors.OUTLINE_VARIANT),
                border_radius=6,
                bgcolor=ft.Colors.SURFACE_CONTAINER_HIGHEST,
            ),
            items=[
                ft.PopupMenuItem(content=ft.Text("ERROR", size=13), data="1", on_click=_on_level_selected),
                ft.PopupMenuItem(content=ft.Text("WARN", size=13), data="2", on_click=_on_level_selected),
                ft.PopupMenuItem(content=ft.Text("INFO", size=13), data="3", on_click=_on_level_selected),
                ft.PopupMenuItem(content=ft.Text("DEBUG", size=13), data="4", on_click=_on_level_selected),
                ft.PopupMenuItem(content=ft.Text("VERBOSE", size=13), data="5", on_click=_on_level_selected),
            ],
        )

        self.marker_input = ft.TextField(
            hint_text="标记日志内容",
            dense=True,
            filled=True,
            fill_color=ft.Colors.SURFACE_CONTAINER_HIGHEST,
            border_color=ft.Colors.OUTLINE_VARIANT,
            border_radius=6,
            width=300,
            text_size=13,
            content_padding=ft.padding.Padding(12, 8, 12, 8),
            on_submit=lambda e: self.safe_call("log_control.log_write_marker", e),
        )

        self.write_marker_btn = ft.ElevatedButton(
            "写入",
            icon=ft.Icons.EDIT_NOTE,
            on_click=lambda e: self.safe_call("log_control.log_write_marker", e),
            bgcolor=ft.Colors.TEAL,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(16, 0, 16, 0),
                elevation=2,
            ),
        )

        self.open_browser_btn = ft.ElevatedButton(
            "打开浏览器",
            icon=ft.Icons.OPEN_IN_NEW,
            on_click=lambda e: self.safe_call("log_control.log_open_browser", e),
            visible=False,
            bgcolor=ft.Colors.BLUE,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(
                shape=ft.RoundedRectangleBorder(radius=6),
                padding=ft.padding.Padding(16, 0, 16, 0),
                elevation=2,
            ),
        )

        storage_card = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Icon(ft.Icons.STORAGE, size=14, color=ft.Colors.BLUE_600),
                    ft.Text("存储信息", size=13, weight=ft.FontWeight.W_600),
                ], spacing=6),
                ft.Container(height=4),
                self.storage_info_text,
                ft.Container(height=4),
                self.storage_usage_bar,
            ], spacing=0),
            padding=8,
            bgcolor=ft.Colors.WHITE,
            border_radius=6,
            border=ft.Border(
                left=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                top=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                right=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                bottom=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
            ),
        )

        http_control_card = ft.Container(
            content=ft.Row([
                ft.Icon(ft.Icons.HTTP, size=14, color=ft.Colors.BLUE_600),
                ft.Text("日志HTTP服务", size=12, weight=ft.FontWeight.W_600),
                self.http_status_icon,
                self.http_status_text,
                self.http_switch,
                ft.Container(expand=True),
                self.http_url_text,
                self.open_browser_btn,
            ], spacing=6),
            padding=ft.padding.Padding(10, 2, 10, 2),
            bgcolor=ft.Colors.WHITE,
            border_radius=6,
            border=ft.Border(
                left=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                top=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                right=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                bottom=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
            ),
        )

        def _wrap(control):
            """统一包装为32高度居中容器"""
            return ft.Container(content=control, height=32, alignment=ft.alignment.Alignment(0, 0))

        operations_card = ft.Container(
            content=ft.Row([
                _wrap(self.refresh_btn),
                _wrap(self.format_btn),
                self.level_dropdown,
                _wrap(self.marker_input),
                _wrap(self.write_marker_btn),
            ], spacing=8, alignment=ft.MainAxisAlignment.START),
            padding=8,
            bgcolor=ft.Colors.WHITE,
            border_radius=6,
            border=ft.Border(
                left=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                top=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                right=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                bottom=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
            ),
        )

        tips_card = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Icon(ft.Icons.INFO, size=14, color=ft.Colors.BLUE_600),
                    ft.Text("说明", size=13, weight=ft.FontWeight.W_600),
                ], spacing=6),
                ft.Container(height=4),
                ft.Text("• 日志HTTP服务：启动后用浏览器访问下载日志", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                ft.Text("• 日志级别：控制设备日志输出等级（ERROR/WARN/INFO/DEBUG/VERBOSE）", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                ft.Text("• 写入标记：在设备日志中插入客户端标记，便于跨端对齐调试", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                ft.Text("• 格式化：完全格式化存储分区（危险）", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
            ], spacing=2),
            padding=8,
            bgcolor=ft.Colors.WHITE,
            border_radius=6,
            border=ft.Border(
                left=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                top=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                right=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
                bottom=ft.BorderSide(width=1, color=ft.Colors.OUTLINE_VARIANT),
            ),
        )

        content = ft.Container(
            content=ft.Column([
                storage_card,
                http_control_card,
                operations_card,
                tips_card,
            ], spacing=8, expand=True),
            padding=8,
            expand=True,
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
        )

        return ft.Stack([
            content,
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
            self.http_status_text.value = "运行中"
            self.http_status_text.color = ft.Colors.GREEN
            self.http_url_text.value = url
            self.http_url_text.visible = True
            self.open_browser_btn.visible = True
        else:
            self.http_status_icon.color = ft.Colors.GREY_400
            self.http_status_text.value = "已停止"
            self.http_status_text.color = ft.Colors.GREY_600
            self.http_url_text.value = ""
            self.http_url_text.visible = False
            self._current_url = ""
            self.open_browser_btn.visible = False
