"""
设备信息Tab组件
"""

import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class InfoTabComponent(BaseTabComponent):
    """设备信息Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.info_display = None
        self.info_overlay = None
        self.info_btns = []

    def build(self):
        """构建设备信息Tab"""
        self.info_display = ft.TextField(
            multiline=True,
            read_only=True,
            min_lines=10,
            max_lines=25,
            border_radius=6,
            text_size=12,
            bgcolor=ft.Colors.SURFACE_CONTAINER_LOWEST,
            border_color=ft.Colors.OUTLINE_VARIANT,
            filled=True,
            label="设备数据",
            expand=True,
        )

        self.info_overlay = self._build_overlay()

        # 创建按钮并保存引用
        btn_configs = [
            ("设备信息", ft.Icons.INFO, "read_device_info", ft.Colors.BLUE),
            ("内存", ft.Icons.MEMORY, "read_memory_info", ft.Colors.BLUE),
            ("CPU", ft.Icons.SPEED, "read_cpu_info", ft.Colors.BLUE),
            ("Flash", ft.Icons.STORAGE, "read_flash_info", ft.Colors.BLUE),
            ("分区", ft.Icons.FOLDER_OPEN, "read_partitions", ft.Colors.BLUE),
            ("温度", ft.Icons.THERMOSTAT, "read_temperature", ft.Colors.BLUE),
            ("重启", ft.Icons.RESTART_ALT, "restart_device", ft.Colors.RED),
        ]
        self.info_btns = []
        for text, icon, handler_name, color in btn_configs:
            btn = self._action_btn(text, icon, handler_name, color)
            self.info_btns.append(btn)

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    ft.Text("设备信息", size=16, weight=ft.FontWeight.W_700, color=ft.Colors.ON_SURFACE),
                    ft.Container(height=12),
                    ft.Row(self.info_btns, spacing=8, wrap=True),
                    ft.Container(height=16),
                    self.info_display,
                ], spacing=0, scroll=ft.ScrollMode.AUTO),
                padding=20,
                bgcolor=ft.Colors.WHITE,
            ),
            self.info_overlay,
        ], expand=True)
