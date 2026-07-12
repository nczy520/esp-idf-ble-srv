"""
WiFi控制处理模块
处理WiFi连接、状态查询、忘记网络、NTP同步等操作
"""

from client_gui.handlers.base import BaseHandler


class WiFiControlHandler(BaseHandler):
    """WiFi控制处理类"""

    def _reset_wifi_ui_on_disconnect(self):
        """断开连接时重置WiFi UI状态"""
        self.ui.wifi_display.value = "未连接"
        self.safe_update()

    def wifi_connect(self, event=None):
        if not self.check_connected():
            return
        ssid = self.ui.ssid_field.value.strip() if self.ui.ssid_field.value else ""
        if not ssid:
            self.log("请输入WiFi SSID", "warn")
            return
        password = self.ui.password_field.value or ""
        self.log(f"发送WiFi连接命令 (SSID: {ssid})...", "info")
        btn = self.ui.wifi_tab.wifi_connect_btn
        def callback(result):
            self.log(f"WiFi连接命令已发送" if result else "发送WiFi连接命令失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.wifi_connect(ssid, password), callback)

    def wifi_status(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.wifi_tab.wifi_status_btn
        def callback(result):
            if isinstance(result, Exception) or result is None:
                self.log("读取WiFi状态失败", "error")
                return
            self.ui.wifi_display.value = str(result)
            self.log("WiFi状态读取成功", "success")
            self.safe_update()
        self._run_with_loading(btn, self.ble.wifi_status(), callback)

    def wifi_forget(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.wifi_tab.wifi_forget_btn
        def callback(result):
            self.log("WiFi配置已清除" if result else "清除WiFi配置失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.wifi_forget(), callback)

    def wifi_ntp(self, event=None):
        if not self.check_connected():
            return
        btn = self.ui.wifi_tab.wifi_ntp_btn
        def callback(result):
            self.log("NTP时间同步命令已发送" if result else "NTP同步失败", "success" if result else "error")
        self._run_with_loading(btn, self.ble.wifi_ntp_sync(), callback)
