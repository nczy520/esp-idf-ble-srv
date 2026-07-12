"""
LED控制Tab组件
"""

import flet as ft
from client_gui.components.tabs.base_tab import BaseTabComponent


class LEDTabComponent(BaseTabComponent):
    """LED控制Tab"""

    def __init__(self, app):
        super().__init__(app)
        self.led_status_text = None
        self.r_slider = None
        self.g_slider = None
        self.b_slider = None
        self.r_val_text = None
        self.g_val_text = None
        self.b_val_text = None
        self.color_box = None
        self.color_code_text = None
        self.color_preview_col = None
        self.effect_menu = None
        self.effect_menu_text = None
        self.effect_value = "无"
        self.speed_slider = None
        self.led_on_btn = None
        self.led_off_btn = None
        self.led_status_btn = None
        self.led_set_color_btn = None
        self.led_set_effect_btn = None
        self.led_overlay = None

    def _on_effect_selected(self, e):
        """特效选择回调"""
        effect_text = e.control.content.value if hasattr(e.control, 'content') else "无"
        self.effect_value = effect_text
        self.effect_menu_text.value = self.effect_value
        self.effect_menu.update()

    def build(self):
        """构建LED控制Tab"""
        self.led_status_text = ft.Text("状态: 未知", size=13, color=ft.Colors.ON_SURFACE_VARIANT, weight=ft.FontWeight.W_500)
        self.r_slider = ft.Slider(min=0, max=255, value=0, label="{value}", width=170, on_change=self._on_color_changed)
        self.g_slider = ft.Slider(min=0, max=255, value=0, label="{value}", width=170, on_change=self._on_color_changed)
        self.b_slider = ft.Slider(min=0, max=255, value=0, label="{value}", width=170, on_change=self._on_color_changed)
        self.r_val_text = ft.Text("00", size=11, weight=ft.FontWeight.W_600, color=ft.Colors.RED, width=28, text_align=ft.TextAlign.RIGHT, font_family="monospace")
        self.g_val_text = ft.Text("00", size=11, weight=ft.FontWeight.W_600, color=ft.Colors.GREEN, width=28, text_align=ft.TextAlign.RIGHT, font_family="monospace")
        self.b_val_text = ft.Text("00", size=11, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE, width=28, text_align=ft.TextAlign.RIGHT, font_family="monospace")
        self.color_box = ft.Container(
            width=44,
            height=44,
            bgcolor="#000000",
            border_radius=6,
            border=ft.border.BorderSide(2, ft.Colors.OUTLINE_VARIANT),
            shadow=ft.BoxShadow(blur_radius=4, color=ft.Colors.with_opacity(0.15, "black"), offset=ft.Offset(0, 1)),
        )
        self.color_code_text = ft.Text("#000000", size=10, weight=ft.FontWeight.W_500, color=ft.Colors.ON_SURFACE_VARIANT, text_align=ft.TextAlign.CENTER, font_family="monospace", width=48)
        self.color_preview_col = ft.Container(
            content=ft.Column([self.color_box, self.color_code_text], spacing=2, horizontal_alignment=ft.CrossAxisAlignment.CENTER),
            margin=ft.margin.Margin(24, 0, 0, 0),
        )
        # 特效选择菜单（类似左侧时间选择菜单）
        effect_items = [
            ft.PopupMenuItem(content=ft.Text(effect), on_click=self._on_effect_selected)
            for effect in self.app.effect_map.keys()
        ]
        self.effect_menu_text = ft.Text("无", size=13, color=ft.Colors.ON_SURFACE, text_align=ft.TextAlign.LEFT)
        self.effect_menu = ft.Container(
            content=ft.PopupMenuButton(
                content=ft.Container(
                    content=ft.Row([
                        self.effect_menu_text,
                        ft.Icon(ft.Icons.ARROW_DROP_DOWN, size=18, color=ft.Colors.ON_SURFACE_VARIANT),
                    ], spacing=4, alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                    width=120,
                    height=40,
                    padding=ft.padding.Padding(12, 10, 8, 10),
                    border=ft.border.BorderSide(1, ft.Colors.OUTLINE_VARIANT),
                    border_radius=6,
                    bgcolor=ft.Colors.SURFACE_CONTAINER_HIGHEST,
                ),
                items=effect_items,
            ),
            height=40,
        )
        # 速度进度条和颜色进度条一样长
        self.speed_slider = ft.Slider(min=1, max=255, value=50, label="{value}", width=220)

        self.led_on_btn = self._action_btn("开", ft.Icons.LIGHTBULB, "led_on", color=ft.Colors.GREEN)
        self.led_off_btn = self._action_btn("关", ft.Icons.LIGHTBULB_OUTLINE, "led_off", color=ft.Colors.RED)
        self.led_status_btn = self._action_btn("查询", ft.Icons.HELP_OUTLINE, "led_status")
        self.led_set_color_btn = self._action_btn("应用", ft.Icons.PALETTE, "led_set_color")
        self.led_set_effect_btn = self._action_btn("应用", ft.Icons.AUTO_AWESOME, "led_set_effect")

        self.led_overlay = self._build_overlay()

        return ft.Stack([
            ft.Container(
                content=ft.Column([
                    # 第一行：开关和状态
                    ft.Row([
                        self.led_on_btn,
                        self.led_off_btn,
                        self.led_status_btn,
                        self.led_status_text,
                    ], spacing=8),
                    ft.Divider(height=12, color=ft.Colors.OUTLINE_VARIANT),
                    # 第二行：颜色设置和特效设置并排
                    ft.Row([
                        # 左侧：颜色设置
                        ft.Column([
                            ft.Text("颜色", size=12, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                            ft.Row([
                                ft.Text("R", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.RED, size=11),
                                self.r_slider,
                                self.r_val_text,
                                self.color_preview_col,
                            ], spacing=4, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                            ft.Row([
                                ft.Text("G", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.GREEN, size=11),
                                self.g_slider,
                                self.g_val_text,
                            ], spacing=4, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                            ft.Row([
                                ft.Text("B", width=16, weight=ft.FontWeight.BOLD, color=ft.Colors.BLUE, size=11),
                                self.b_slider,
                                self.b_val_text,
                            ], spacing=4, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                            self.led_set_color_btn,
                        ], spacing=2, expand=3),
                        # 右侧：特效设置
                        ft.Column([
                            ft.Text("特效", size=12, weight=ft.FontWeight.W_600, color=ft.Colors.BLUE),
                            ft.Row([
                                ft.Text("模式", size=11, width=32),
                                self.effect_menu,
                            ], spacing=4, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                            ft.Row([
                                ft.Text("速度", size=11, width=32),
                                self.speed_slider,
                            ], spacing=4, vertical_alignment=ft.CrossAxisAlignment.CENTER),
                            self.led_set_effect_btn,
                        ], spacing=2, expand=2),
                    ], spacing=24, alignment=ft.MainAxisAlignment.START, vertical_alignment=ft.CrossAxisAlignment.START, expand=True),
                ], spacing=0, scroll=ft.ScrollMode.AUTO, expand=True),
                padding=16,
                bgcolor=ft.Colors.WHITE,
                expand=True,
            ),
            self.led_overlay,
        ], expand=True)

    def _on_color_changed(self, event):
        """颜色变化回调"""
        self.safe_call("color_changed", event)
