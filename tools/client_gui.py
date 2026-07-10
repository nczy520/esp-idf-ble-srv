#!/usr/bin/env python3
"""
ESP32 BLE Device Manager - Flet GUI Client
跨平台蓝牙BLE设备管理器图形界面客户端 (macOS / Windows)

依赖: pip install flet bleak

用法:
    python client_gui.py              # 正常启动
    python client_gui.py --debug      # 调试模式启动（自动清理 __pycache__）
"""

__version__ = "1.2.1"

import sys
import os
import shutil
import argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.path.dirname(os.path.abspath(sys.argv[0])))


def clear_pycache():
    """清除所有 __pycache__ 目录"""
    root = os.path.dirname(os.path.abspath(__file__))
    count = 0
    for dirpath, dirnames, filenames in os.walk(root):
        if '__pycache__' in dirnames:
            cache_path = os.path.join(dirpath, '__pycache__')
            try:
                shutil.rmtree(cache_path)
                count += 1
                print(f"已清除: {cache_path}")
            except Exception as e:
                print(f"清除失败 {cache_path}: {e}")
    if count > 0:
        print(f"共清除 {count} 个 __pycache__ 目录")
    else:
        print("未发现 __pycache__ 目录")


def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='BLE Device Manager')
    parser.add_argument('--debug', action='store_true', help='调试模式（自动清理 __pycache__）')
    args = parser.parse_args()

    # 调试模式：清除 __pycache__
    if args.debug:
        print("=" * 50)
        print("调试模式已开启")
        print("=" * 50)
        clear_pycache()
        print("=" * 50)

    # 启动应用
    from client_gui import main as app_main
    app_main(__version__)


if __name__ == "__main__":
    main()
