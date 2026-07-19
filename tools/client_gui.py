#!/usr/bin/env python3
"""
ESP32 BLE Device Manager - Flet GUI Client v2.0.1
跨平台蓝牙BLE设备管理器图形界面客户端 (macOS / Windows / Linux)

开发者: 赵宇
联系邮箱: support@mdeve.com

依赖: pip install flet bleak

用法:
    python client_gui.py              # 正常启动
    python client_gui.py --version    # 显示版本号
    python client_gui.py --debug      # 调试模式启动（自动清理 __pycache__）
    python client_gui.py -h           # 显示帮助信息

使用注意事项:
  1. 首次使用请先安装依赖: pip install flet bleak
  2. Windows系统请确保蓝牙已开启并授权应用访问
  3. macOS系统需要在系统设置中允许终端访问蓝牙
  4. Linux系统需要运行蓝牙服务并具备相应权限
  5. 每次OTA建议重启设备，OTA失败需要重启设备再次OTA
  6. 蓝牙OTA传输时请保持设备靠近电脑，避免信号干扰
  7. URL OTA需要设备先通过GUI连接WiFi
  8. GUI窗口大小固定为1280x800，不支持缩放
"""

__version__ = "2.0.1"

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
    parser = argparse.ArgumentParser(
        description=f'ESP32 BLE Device Manager GUI v{__version__}',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用注意事项:
  - 首次使用请安装依赖: pip install flet bleak
  - Windows/macOS/Linux 均支持
  - 每次OTA后建议重启设备
  - OTA失败需要重启设备后再次OTA
  - URL OTA需要设备先连接WiFi
        """
    )
    parser.add_argument('-v', '--version', action='version', version=f'%(prog)s {__version__}')
    parser.add_argument('--debug', action='store_true', help='调试模式（自动清理 __pycache__）')
    args = parser.parse_args()

    if args.debug:
        print("=" * 50)
        print("调试模式已开启")
        print("=" * 50)
        clear_pycache()
        print("=" * 50)

    from client_gui import main as app_main
    app_main(__version__)


if __name__ == "__main__":
    main()
