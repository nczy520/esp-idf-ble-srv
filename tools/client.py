#!/usr/bin/env python3
"""
ESP32 BLE设备管理器客户端 - 命令行工具

使用方法:
  python tools/client.py scan                       扫描BLE设备
  python tools/client.py info -d 设备名            读取设备信息
  python tools/client.py ota-bt -f firmware.bin     蓝牙OTA升级
  python tools/client.py ota-url --url <URL>        URL OTA升级
  python tools/client.py wifi-status                WiFi状态
  python tools/client.py led-color --color FF0000   设置LED颜色(红)
"""

__version__ = "2.0.0"

import asyncio
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from client import BLEDeviceClient


async def main():
    parser = argparse.ArgumentParser(
        description='ESP32 BLE Device Manager CLI',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('-d', '--device', help='设备名称（前缀匹配），不指定则扫描选择')

    cmd_choices = [
        'scan', 'info', 'memory', 'cpu', 'flash', 'partition', 'restart', 'temperature',
        'ota-bt', 'ota-url',
        'wifi-status', 'wifi-connect', 'wifi-disconnect', 'wifi-forget', 'ntp-sync',
        'led-on', 'led-off', 'led-color', 'led-status', 'led-effect'
    ]

    cmd_group = parser.add_mutually_exclusive_group(required=True)
    cmd_group.add_argument('-c', choices=cmd_choices, metavar='CMD', dest='command',
                           help='执行的命令')
    cmd_group.add_argument('--command', choices=cmd_choices, metavar='', dest='command',
                           help=argparse.SUPPRESS)

    parser.add_argument('-f', '--firmware', help='OTA固件文件路径（蓝牙OTA用）')
    parser.add_argument('--url', help='固件URL地址（URL OTA用）')
    parser.add_argument('--ssid', help='WiFi SSID')
    parser.add_argument('--password', default='', help='WiFi密码')
    parser.add_argument('--color', help='LED颜色（AABBCC十六进制，如FF0000=红）')
    parser.add_argument('--effect', choices=['none', 'breath', 'blink', 'rainbow', 'strobe'],
                        help='LED特效')
    parser.add_argument('--speed', type=int, default=50, help='LED特效速度（1-255，默认50）')
    parser.add_argument('--timeout', type=int, default=5, help='扫描超时时间（秒，默认5）')

    args = parser.parse_args()

    client = BLEDeviceClient(device_name=args.device)
    connected = False

    if args.command == 'wifi-connect' and not args.ssid:
        print("错误: wifi-connect 需要 --ssid <SSID>")
        return 1
    if args.command == 'ota-bt' and not args.firmware:
        print("错误: ota-bt 需要 -f <firmware.bin>")
        return 1
    if args.command == 'ota-url' and not args.url:
        print("错误: ota-url 需要 --url <URL>，或使用 ota-url-default 使用设备内置URL")
        return 1
    if args.command == 'led-color' and not args.color:
        print("错误: led-color 需要 --color AABBCC（如FF0000=红色）")
        return 1
    if args.command == 'led-effect' and not args.effect:
        print("错误: led-effect 需要 --effect <类型>")
        print("可用特效: none(无), breath(呼吸灯), blink(闪烁), rainbow(彩虹), strobe(频闪)")
        return 1

    try:
        if args.command == 'scan':
            await client.scan(timeout=args.timeout)
            return 0

        connected = await client.connect()
        if not connected:
            return 1

        if args.command == 'info':
            info = await client.read_device_info()
            if info:
                print(f"\n设备信息:\n{info}")
            else:
                return 1

        elif args.command == 'memory':
            info = await client.read_memory_info()
            if info:
                print(f"\n内存信息:\n{info}")
            else:
                return 1

        elif args.command == 'cpu':
            info = await client.read_cpu_info()
            if info:
                print(f"\nCPU信息:\n{info}")
            else:
                return 1

        elif args.command == 'flash':
            info = await client.read_flash_info()
            if info:
                print(f"\nFlash信息:\n{info}")
            else:
                return 1

        elif args.command == 'partition':
            partitions = await client.read_all_partitions()
            if partitions:
                print(f"\n分区列表 ({len(partitions)} 个):")
                for i, part in enumerate(partitions):
                    print(f"\n--- 分区 {i} ---")
                    print(part)
            else:
                print("读取分区信息失败")
                return 1

        elif args.command == 'restart':
            await client.restart_device()

        elif args.command == 'temperature':
            temp = await client.read_temperature()
            if temp is not None:
                if temp <= -900.0:
                    print("\n温度传感器: 不支持或未启用")
                else:
                    print(f"\n当前温度: {temp:.2f}°C")
            else:
                return 1

        elif args.command == 'ota-bt':
            ok = await client.ota_update(args.firmware)
            if ok:
                print("\nOTA升级成功")
                return 0
            else:
                print("\nOTA升级失败", file=sys.stderr)
                return 1

        elif args.command == 'ota-url':
            ok = await client.ota_url_start_url(args.url)
            if ok:
                print("\nURL OTA完成")
                return 0
            else:
                print("\nURL OTA失败", file=sys.stderr)
                return 1

        elif args.command == 'wifi-status':
            status = await client.wifi_status()
            if status:
                print(f"\nWiFi状态:\n{status}")
            else:
                return 1

        elif args.command == 'wifi-connect':
            await client.wifi_connect(args.ssid, args.password)

        elif args.command == 'wifi-disconnect':
            await client.wifi_disconnect()

        elif args.command == 'wifi-forget':
            await client.wifi_forget()

        elif args.command == 'ntp-sync':
            await client.wifi_ntp_sync()

        elif args.command == 'led-on':
            await client.led_on()

        elif args.command == 'led-off':
            await client.led_off()

        elif args.command == 'led-color':
            ok = await client.led_set_color(args.color)
            if not ok:
                return 1

        elif args.command == 'led-status':
            await client.led_status()

        elif args.command == 'led-effect':
            effect_map = {'none': 0, 'breath': 1, 'blink': 2, 'rainbow': 3, 'strobe': 4}
            await client.led_set_effect(effect_map[args.effect], args.speed)

        return 0

    except KeyboardInterrupt:
        print("\n\n用户中止")
        if connected and args.command == 'ota-bt':
            print("正在中止OTA...")
            await client.ota_abort()
        return 130
    finally:
        try:
            if connected:
                await client.disconnect()
        except Exception:
            pass


if __name__ == '__main__':
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        print("\n已中止")
        sys.exit(130)
