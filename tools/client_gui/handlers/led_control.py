"""
LED控制处理模块
处理LED开关、颜色设置、特效设置等操作
"""

import flet as ft

from client_gui.handlers.base import BaseHandler


class LEDControlHandler(BaseHandler):
    """LED控制处理类"""

    def color_changed(self, event=None):
        """颜色滑块变化"""
        try:
            r = int(self.ui.r_slider.value)
            g = int(self.ui.g_slider.value)
            b = int(self.ui.b_slider.value)
            self.ui.color_box.bgcolor = f"#{r:02x}{g:02x}{b:02x}"
            self.page.update()
        except Exception:
            pass

    def led_on(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.led_tab.led_on_btn
        def callback(result):
            self.log("LED已打开" if result else "打开LED失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.led_on(), callback, loading_text="开启中...")

    def led_off(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.led_tab.led_off_btn
        def callback(result):
            self.log("LED已关闭" if result else "关闭LED失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.led_off(), callback, loading_text="关闭中...")

    def led_status(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.led_tab.led_status_btn
        def callback(result):
            if result is None:
                self.log("读取LED状态失败", "error")
                self.ui.led_status_text.value = "状态: 未知"
            elif result:
                self.log("LED状态: 开", "success")
                self.ui.led_status_text.value = "状态: 开 ●"
            else:
                self.log("LED状态: 关", "info")
                self.ui.led_status_text.value = "状态: 关 ○"
            self.page.update()
        self._run_with_loading(btn, self.ble.led_status(), callback, loading_text="查询中...")

    def led_set_color(self, event=None):
        if not self.check_connected():
            return
        r = int(self.ui.r_slider.value)
        g = int(self.ui.g_slider.value)
        b = int(self.ui.b_slider.value)
        btn = self.ui.led_tab.led_set_color_btn
        def callback(result):
            self.log(f"LED颜色已设置: #{r:02x}{g:02x}{b:02x}" if result else "设置LED颜色失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.led_set_color(r, g, b), callback, loading_text="设置中...")

    def led_set_effect(self, event=None):
        if not self.check_connected():
            return
        name = self.ui.led_tab.effect_value
        effect = self.app.effect_map.get(name, 0)
        speed = int(self.ui.speed_slider.value)
        btn = self.ui.led_tab.led_set_effect_btn
        def callback(result):
            self.log(f"LED特效设置: {name}, 速度: {speed}" if result else "设置LED特效失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.led_set_effect(effect, speed), callback, loading_text="设置中...")
