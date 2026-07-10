#!/usr/bin/env python3
"""
ESP32 BLE设备管理器客户端 - 命令行工具

使用方法:
  python client.py -c scan
  python client.py -c info -d 设备名
  python client.py -c ota-bt -f firmware.bin
  python client.py -c ota-url --url https://example.com/firmware.bin
  python client.py -c temperature -d 设备名

功能模块:
  - client/constants.py    : UUID常量和命令码
  - client/models.py       : 数据结构类
  - client/client.py       : 核心BLE客户端基类
  - client/ota.py          : OTA功能混合类
  - client/wifi.py         : WiFi功能混合类
  - client/led.py          : LED功能混合类
  - client/temperature.py  : 温度传感器混合类
  - client/__init__.py     : 完整客户端组合类
"""

__version__ = "1.2.1"

import asyncio
import argparse
import sys
import os

# 添加脚本目录到路径，支持作为脚本直接运行
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from client import BLEDeviceClient


async def main():
    parser = argparse.ArgumentParser(description='ESP32 BLE Device Manager Client')
    parser.add_argument('-d', '--device', help='设备名称或地址')

    cmd_choices = [
        'scan', 'info', 'memory', 'cpu', 'flash', 'partition', 'restart', 'temperature',
        'ota-bt', 'ota-url',
        'wifi-status', 'wifi-connect', 'wifi-forget', 'ntp-sync',
        'led-on', 'led-off', 'led-color', 'led-status', 'led-effect'
    ]

    cmd_group = parser.add_mutually_exclusive_group(required=True)
    cmd_group.add_argument(
        '-c', choices=cmd_choices,
        metavar='{' + ','.join(cmd_choices) + '}',
        dest='command', help='执行的命令'
    )
    cmd_group.add_argument(
        '--command', choices=cmd_choices,
        metavar='', dest='command', help='执行的命令, 同 -c'
    )

    parser.add_argument('-f', '--firmware', help='OTA固件文件路径')
    parser.add_argument('--url', help='URL OTA 固件URL地址')
    parser.add_argument('--ssid', help='WiFi SSID')
    parser.add_argument('--password', help='WiFi密码')
    parser.add_argument('--color', help='LED颜色（AABBCC格式，如FF0000=红色）')
    parser.add_argument(
        '--effect', choices=['none', 'breath', 'blink', 'rainbow', 'strobe'],
        help='LED特效类型'
    )
    parser.add_argument('--speed', type=int, default=50, help='LED特效速度（1-255，默认50）')
    parser.add_argument('--timeout', type=int, default=3, help='扫描超时时间（秒，默认3）')

    args = parser.parse_args()

    client = BLEDeviceClient(device_name=args.device)
    connected = False

    # 参数校验
    if args.command == 'wifi-connect' and not args.ssid:
        print("请指定WiFi SSID: --ssid <SSID>")
        return
    if args.command == 'ota-bt' and not args.firmware:
        print("请指定固件文件路径: -f <firmware.bin>")
        return
    if args.command == 'led-color' and not args.color:
        print("请指定LED颜色: --color AABBCC（如FF0000=红色）")
        return
    if args.command == 'led-effect' and not args.effect:
        print("请指定LED特效: --effect <类型>")
        print("可用特效: none(无), breath(呼吸灯), blink(闪烁), rainbow(彩虹), strobe(频闪)")
        return

    try:
        # scan 命令不需要连接
        if args.command == 'scan':
            await client.scan(timeout=args.timeout)
            return

        # 其他命令需要先连接
        connected = await client.connect()
        if not connected:
            return

        # 设备信息类命令
        if args.command == 'info':
            info = await client.read_device_info()
            if info:
                print("\n设备信息:")
                print(info)

        elif args.command == 'memory':
            info = await client.read_memory_info()
            if info:
                print("\n内存信息:")
                print(info)

        elif args.command == 'cpu':
            info = await client.read_cpu_info()
            if info:
                print("\nCPU信息:")
                print(info)

        elif args.command == 'flash':
            info = await client.read_flash_info()
            if info:
                print("\nFlash信息:")
                print(info)

        elif args.command == 'partition':
            partitions = await client.read_all_partitions()
            if partitions:
                print(f"\n分区列表 ({len(partitions)} 个):")
                for i, part in enumerate(partitions):
                    print(f"\n--- 分区 {i} ---")
                    print(part)

        elif args.command == 'restart':
            await client.restart_device()

        # 温度传感器命令
        elif args.command == 'temperature':
            temp = await client.read_temperature()
            if temp is not None:
                if temp <= -900.0:
                    print("\n温度传感器: 不支持或未启用")
                else:
                    print(f"\n当前温度: {temp:.2f}°C")
            else:
                print("\n读取温度失败")

        # OTA 命令
        elif args.command == 'ota-bt':
            await client.ota_update(args.firmware)

        elif args.command == 'ota-url':
            if args.url:
                await client.ota_https_start_url(args.url)
            else:
                await client.ota_https_start_default()

        # WiFi 命令
        elif args.command == 'wifi-status':
            status = await client.wifi_status()
            if status:
                print("\nWiFi状态:")
                print(status)

        elif args.command == 'wifi-connect':
            await client.wifi_connect(args.ssid, args.password or "")

        elif args.command == 'wifi-forget':
            await client.wifi_forget()

        elif args.command == 'ntp-sync':
            await client.wifi_ntp_sync()

        # LED 命令
        elif args.command == 'led-on':
            await client.led_on()

        elif args.command == 'led-off':
            await client.led_off()

        elif args.command == 'led-color':
            await client.led_set_color(args.color)

        elif args.command == 'led-status':
            await client.led_status()

        elif args.command == 'led-effect':
            effect_map = {'none': 0, 'breath': 1, 'blink': 2, 'rainbow': 3, 'strobe': 4}
            effect_id = effect_map[args.effect]
            await client.led_set_effect(effect_id, args.speed)

    except KeyboardInterrupt:
        print("\n\n用户中止操作")
        if connected and args.command == 'ota-bt':
            print("正在中止OTA...")
            await client.ota_abort()

    finally:
        try:
            if connected:
                await client.disconnect()
        except Exception:
            pass


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n程序已中止")
