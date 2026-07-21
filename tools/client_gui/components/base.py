"""
UI组件基类
提供通用的回调处理和工具方法
"""


class BaseComponent:
    """UI组件基类"""

    def __init__(self, app):
        self.app = app

    def safe_call(self, method_name, event=None):
        """安全调用handlers方法，支持点号分隔的嵌套方法，例如 log_control.log_refresh"""
        if not (self.app and self.app.handlers):
            return

        obj = self.app.handlers
        parts = method_name.split('.')
        for part in parts:
            obj = getattr(obj, part, None)
            if obj is None:
                return

        if callable(obj):
            obj(event)

    def get_handler(self):
        """获取handlers实例"""
        if self.app and self.app.handlers:
            return self.app.handlers
        return None
