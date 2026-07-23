#!/usr/bin/env python3
"""
ESP32 BLE Device Manager CLI - 命令行工具 v2.1.1

使用方法:
  python tools/client.py scan [--timeout 5]
  python tools/client.py info [-d 设备名]
  python tools/client.py memory [-d 设备名]
  python tools/client.py cpu [-d 设备名]
  python tools/client.py flash [-d 设备名]
  python tools/client.py partition [-d 设备名]
  python tools/client.py ota-bt -f firmware.bin [-d 设备名]
  python tools/client.py ota-url --url <URL> [-d 设备名]
  python tools/client.py wifi-status [-d 设备名]
  python tools/client.py wifi-connect --ssid <SSID> --password <密码> [-d 设备名]
  python tools/client.py wifi-disconnect [-d 设备名]
  python tools/client.py wifi-forget [-d 设备名]
  python tools/client.py ntp-sync [-d 设备名]
  python tools/client.py led-on [-d 设备名]
  python tools/client.py led-off [-d 设备名]
  python tools/client.py led-color --color FF0000 [-d 设备名]
  python tools/client.py led-status [-d 设备名]
  python tools/client.py led-effect --effect breath --speed 50 [-d 设备名]
  python tools/client.py restart [-d 设备名]

使用注意事项:
  1. 设备名使用前缀匹配，不指定 -d 参数时会扫描并列出设备供选择
  2. 蓝牙OTA时建议关闭其他蓝牙设备以避免干扰
  3. 每次OTA后建议重启设备，OTA失败需要重启设备后再次OTA
  4. URL OTA需要设备先连接WiFi，否则会失败
  5. LED颜色使用十六进制RGB格式（如FF0000=红色, 00FF00=绿色, 0000FF=蓝色）
  6. LED特效速度范围1-255，数值越小速度越快
  7. 按 Ctrl+C 可中止正在进行的OTA传输

命令说明:
  scan           扫描周围BLE设备
  info           读取设备综合信息（包含温度）
  memory         读取内存详细信息
  cpu            读取CPU详细信息
  flash          读取Flash详细信息
  partition      读取所有分区信息
  restart        重启设备
  ota-bt         蓝牙OTA固件升级（需要指定固件文件）
  ota-url        URL OTA固件升级（需要指定固件URL）
  wifi-status    查看WiFi连接状态
  wifi-connect   连接WiFi（需要SSID，密码可选）
  wifi-disconnect 断开WiFi连接
  wifi-forget    清除保存的WiFi凭据
  ntp-sync       NTP时间同步
  led-on         打开LED
  led-off        关闭LED
  led-color      设置LED颜色（十六进制RGB）
  led-status     查看LED状态
  led-effect     设置LED特效

示例:
  # 扫描设备
  python tools/client.py scan

  # 读取设备信息（自动选择设备）
  python tools/client.py info

  # 指定设备名读取信息
  python tools/client.py -d BLE-SRV info

  # 蓝牙OTA升级
  python tools/client.py -d BLE-SRV ota-bt -f build/ble_srv_example.bin

  # URL OTA升级
  python tools/client.py -d BLE-SRV ota-url --url http://example.com/firmware.bin

  # 连接WiFi
  python tools/client.py -d BLE-SRV wifi-connect --ssid MyWiFi --password 12345678

  # 设置LED为红色呼吸灯
  python tools/client.py -d BLE-SRV led-color --color FF0000
  python tools/client.py -d BLE-SRV led-effect --effect breath --speed 80
"""

__version__ = "2.1.1"

import asyncio
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from client import BLEDeviceClient


async def main():
    parser = argparse.ArgumentParser(
        description=f'ESP32 BLE Device Manager CLI v{__version__}',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  python tools/client.py scan                       扫描BLE设备
  python tools/client.py -d BLE-SRV info            读取设备信息
  python tools/client.py -d BLE-SRV ota-bt -f fw.bin  蓝牙OTA升级
  python tools/client.py -d BLE-SRV wifi-connect --ssid MyWiFi --password 123  连接WiFi

注意事项:
  - 设备名使用前缀匹配，不指定 -d 时会扫描选择
  - 每次OTA后建议重启设备
  - OTA失败需要重启设备后再次OTA
  - URL OTA需要设备先连接WiFi
  - LED颜色格式为十六进制RGB（如FF0000=红）
  - 按 Ctrl+C 可中止OTA传输
        """
    )
    parser.add_argument('-v', '--version', action='version', version=f'%(prog)s {__version__}')
    parser.add_argument('-d', '--device', help='设备名称（前缀匹配），不指定则扫描选择')
    parser.add_argument('--pin', help='设备连接密码（GATT层PIN认证），设备开启认证时需要')

    cmd_choices = [
        'scan', 'info', 'memory', 'cpu', 'flash', 'partition', 'restart',
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
                        help='LED特效: none(无), breath(呼吸), blink(闪烁), rainbow(彩虹), strobe(频闪)')
    parser.add_argument('--speed', type=int, default=50, help='LED特效速度（1-255，默认50，越小越快）')
    parser.add_argument('--timeout', type=int, default=5, help='扫描超时时间（秒，默认5）')

    args = parser.parse_args()

    client = BLEDeviceClient(device_name=args.device, pin=args.pin)
    connected = False

    if args.command == 'wifi-connect' and not args.ssid:
        print("错误: wifi-connect 需要 --ssid <SSID>")
        return 1
    if args.command == 'ota-bt' and not args.firmware:
        print("错误: ota-bt 需要 -f <firmware.bin>")
        return 1
    if args.command == 'ota-url' and not args.url:
        print("错误: ota-url 需要 --url <URL>")
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

        elif args.command == 'ota-bt':
            ok = await client.ota_update(args.firmware)
            if ok:
                print("\nOTA升级成功！建议重启设备以应用新固件。")
                return 0
            else:
                print("\nOTA升级失败。请重启设备后再次尝试OTA。", file=sys.stderr)
                return 1

        elif args.command == 'ota-url':
            ok = await client.ota_url_start_url(args.url)
            if ok:
                print("\nURL OTA完成！建议重启设备以应用新固件。")
                return 0
            else:
                print("\nURL OTA失败。请检查WiFi连接和URL地址，重启设备后再次尝试。", file=sys.stderr)
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
