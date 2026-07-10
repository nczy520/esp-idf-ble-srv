"""
OTA升级Tab组件
"""

import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class OTATabComponent(BaseTabComponent):
    """OTA升级Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.fw_path_field = None
        self.ota_url_field = None
        self.ota_progress = None
        self.ota_status_text = None
        self.ota_bt_btn = None
        self.ota_abort_btn = None
        self.ota_url_btn = None
        self.ota_default_btn = None
        self.ota_overlay = None

    def build(self):
        """构建OTA升级Tab"""
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

        self.fw_path_field = ft.TextField(
            label="固件文件路径",
            suffix=ft.IconButton(ft.Icons.FOLDER_OPEN, on_click=self._on_pick_firmware, icon_size=20),
            prefix_icon=ft.Icons.FILE_PRESENT,
            **input_style,
        )
        self.ota_url_field = ft.TextField(
            label="固件 URL",
            prefix_icon=ft.Icons.LINK,
            **input_style,
        )
        self.ota_progress = ft.ProgressBar(
            width=400,
            bar_height=12,
            value=0,
            bgcolor=ft.Colors.SURFACE_CONTAINER_HIGHEST,
            color=ft.Colors.BLUE,
            border_radius=6,
        )
        self.ota_status_text = ft.Text("", size=13, color=ft.Colors.ON_SURFACE_VARIANT, weight=ft.FontWeight.W_500)
        self.ota_bt_btn = self._action_btn("蓝牙OTA", ft.Icons.UPLOAD_FILE, "start_ota_bt")
        self.ota_abort_btn = ft.ElevatedButton(
            "中止",
            icon=ft.Icons.STOP,
            on_click=lambda e: self.safe_call("abort_ota", e),
            disabled=True,
            bgcolor=ft.Colors.RED,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=6)),
        )
        self.ota_url_btn = self._action_btn("URL升级", ft.Icons.CLOUD_DOWNLOAD, "start_ota_url")
        self.ota_default_btn = self._action_btn("默认URL", ft.Icons.CLOUD, "start_ota_default")

        self.ota_overlay = self._build_overlay()

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    # 蓝牙OTA区域
                    ft.Row([
                        ft.Text("蓝牙OTA", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                        self.fw_path_field,
                        self.ota_bt_btn,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                    ft.Divider(height=16, color=ft.Colors.OUTLINE_VARIANT),
                    # URL OTA区域
                    ft.Row([
                        ft.Text("URL OTA", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                        self.ota_url_field,
                        self.ota_url_btn,
                        self.ota_default_btn,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                    ft.Divider(height=16, color=ft.Colors.OUTLINE_VARIANT),
                    # 进度条和状态
                    ft.Row([
                        ft.Text("进度", size=13, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                        self.ota_status_text,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                    ft.Container(height=8),
                    self.ota_progress,
                    ft.Container(height=12),
                    # 中止按钮放在进度条下面
                    ft.Row([
                        self.ota_abort_btn,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START),
                ], spacing=0, scroll=ft.ScrollMode.AUTO),
                padding=16,
                bgcolor=ft.Colors.WHITE,
            ),
            self.ota_overlay,
        ], expand=True)

    def _on_pick_firmware(self, event):
        """选择固件文件回调"""
        self.safe_call("pick_firmware", event)
