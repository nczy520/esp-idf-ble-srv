"""
UI组件基类
提供通用的回调处理和工具方法
"""


class BaseComponent:
    """UI组件基类"""

    def __init__(self, app):
        self.app = app

    def safe_call(self, method_name, event=None):
        """安全调用handlers方法"""
        if self.app and self.app.handlers:
            method = getattr(self.app.handlers, method_name, None)
            if method:
                method(event)

    def get_handler(self):
        """获取handlers实例"""
        if self.app and self.app.handlers:
            return self.app.handlers
        return None
