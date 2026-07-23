"""
OTA升级Tab组件
"""

import flet as ft
from typing import Any
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
        self.file_picker = None

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

        # 文件选择器：macOS 使用 Flet FilePicker（避免 _tkinter 缺失与 osascript 权限问题），
        # Windows/Linux 使用 tkinter filedialog 走原生对话框。
        # Flet FilePicker 通过 page.overlay.append 挂载，on_result 回调带回 path。
        import sys
        is_macos = sys.platform == "darwin"
        if is_macos:
            self.file_picker = ft.FilePicker(on_result=self._on_file_picked)
            # FilePicker 必须挂到 page.overlay，构建时延迟挂载。
            self._pending_picker = self.file_picker
        else:
            self._pending_picker = None

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
            bgcolor=ft.Colors.RED_700,
            color=ft.Colors.WHITE,
            style=ft.ButtonStyle(shape=ft.RoundedRectangleBorder(radius=6), padding=ft.padding.Padding(16, 0, 16, 0)),
        )
        self.ota_url_btn = self._action_btn("URL升级", ft.Icons.CLOUD_DOWNLOAD, "start_ota_url")
        self.ota_default_btn = self._action_btn("默认URL", ft.Icons.CLOUD, "start_ota_default")

        self.ota_overlay = self._build_overlay()

        ota_notice = ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Icon(ft.Icons.WARNING_AMBER_ROUNDED, size=18, color=ft.Colors.ORANGE_700),
                    ft.Text("OTA升级注意事项", size=13, weight=ft.FontWeight.W_700, color=ft.Colors.ORANGE_700),
                ], spacing=6, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                ft.Container(height=4),
                ft.Text("• 升级中请勿断开蓝牙/电源，确保固件与设备型号匹配，否则可能变砖", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                ft.Text("• 蓝牙OTA速度较慢，大固件建议URL升级（需设备已连WiFi）", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
                ft.Text("• 升级完成后设备将自动重启，属于正常现象", size=12, color=ft.Colors.ON_SURFACE_VARIANT),
            ], spacing=2),
            padding=ft.padding.Padding(12, 10, 12, 10),
            bgcolor=ft.Colors.with_opacity(0.12, ft.Colors.ORANGE),
            border_radius=8,
            border=ft.border.BorderSide(1, ft.Colors.with_opacity(0.3, ft.Colors.ORANGE)),
        )

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    ota_notice,
                    ft.Container(height=16),
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
                    ft.Row([
                        self.ota_progress,
                        self.ota_abort_btn,
                    ], spacing=8, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                ], spacing=0, scroll=ft.ScrollMode.AUTO),
                padding=16,
                bgcolor=ft.Colors.WHITE,
            ),
            self.ota_overlay,
        ], expand=True)

    def _on_pick_firmware(self, event):
        """选择固件文件回调
        macOS: Flet FilePicker（原生 macOS 面板，无 osascript 权限问题）
        Windows/Linux: tkinter.filedialog 弹原生对话框
        """
        import sys

        if sys.platform == "darwin":
            # FilePicker 需先挂到 page.overlay 才能 pick_files。
            # 该挂载由 app 在 build 后统一调用 mount_picker 完成。
            if self.file_picker is None:
                return
            try:
                self.file_picker.pick_files(
                    dialog_title="选择固件文件",
                    allowed_extensions=["bin"],
                    allow_multiple=False,
                )
            except Exception as e:
                # 首次 pick_files 之前 page.overlay 未挂载时 fallback 到延迟挂载。
                self._pending_picker = self.file_picker
                if hasattr(self.app, "page") and self.app.page is not None:
                    self._mount_picker()
                    self.file_picker.pick_files(
                        dialog_title="选择固件文件",
                        allowed_extensions=["bin"],
                        allow_multiple=False,
                    )
                else:
                    print(f"FilePicker 不可用: {e}")
        else:
            # Windows/Linux: 使用 tkinter.filedialog
            try:
                import tkinter as tk
                from tkinter import filedialog
                root = tk.Tk()
                root.withdraw()
                root.wm_attributes('-topmost', 1)
                file_path = filedialog.askopenfilename(
                    title="选择固件文件",
                    filetypes=[("Binary files", "*.bin"), ("All files", "*.*")]
                )
                root.destroy()
                if file_path:
                    self.fw_path_field.value = file_path
                    self.app.safe_update()
            except Exception:
                pass

    def _on_file_picked(self, e: Any):
        """Flet FilePicker 选择完成回调（仅 macOS 使用）"""
        if not e.files or len(e.files) == 0:
            return
        # macOS FilePicker 返回的对象可能用 .path 或 .name，按 .path 优先。
        picked = e.files[0]
        path = getattr(picked, "path", None) or getattr(picked, "name", None)
        if path and self.fw_path_field is not None:
            self.fw_path_field.value = path
            try:
                self.app.safe_update()
            except Exception:
                pass

    def _mount_picker(self):
        """将 FilePicker 挂到 page.overlay。app 在初始化阶段统一调用。"""
        if self._pending_picker is None:
            return
        page = getattr(self.app, "page", None)
        if page is None:
            return
        if self._pending_picker not in page.overlay:
            page.overlay.append(self._pending_picker)
            try:
                self.app.safe_update()
            except Exception:
                pass
        self._pending_picker = None
