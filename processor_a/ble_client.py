"""
BLE 透传客户端 — 与 ESP32S3_Host_QS 通信
Windows 电脑蓝牙，基于 bleak 库

用法:
    pip install bleak
    python ble_client.py
"""

import asyncio
import sys
from bleak import BleakScanner, BleakClient

# 跟 ESP32 代码里的 UUID 保持一致
DEVICE_NAME = "ESP32S3_Host_QS"
SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_TX_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"  # 手机/电脑写 -> ESP32 收
CHAR_RX_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb"  # ESP32 通知 -> 手机/电脑收


def notification_handler(sender, data):
    """收到 ESP32 的通知数据"""
    print(f"\n[ESP32 >>] {data.decode('utf-8', errors='replace')}")


async def main():
    # 1. 扫描设备
    print(f"正在扫描 BLE 设备 (超时 5s)...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=5.0)

    if device is None:
        print(f"没找到设备 '{DEVICE_NAME}'，检查 ESP32 是否已上电广播")
        print("尝试列出所有扫描到的设备:")
        devices = await BleakScanner.discover(timeout=3.0)
        for d in devices:
            if d.name:
                print(f"  - {d.name} ({d.address})")
        return

    print(f"找到设备: {device.name} ({device.address})")

    # 2. 连接
    async with BleakClient(device.address) as client:
        if not client.is_connected:
            print("连接失败")
            return
        print("BLE 已连接")

        # 3. 订阅通知 (FFE2)
        await client.start_notify(CHAR_RX_UUID, notification_handler)
        print(f"已订阅 RX 通知 ({CHAR_RX_UUID})")

        print("\n输入文字后回车发送，Ctrl+C 退出\n")
        print("-" * 50)

        # 4. 交互循环
        loop = asyncio.get_event_loop()

        while True:
            try:
                line = await loop.run_in_executor(None, sys.stdin.readline)
            except KeyboardInterrupt:
                break

            if not line:
                break

            line = line.strip()
            if not line:
                continue

            if line == "quit" or line == "exit":
                break

            try:
                await client.write_gatt_char(CHAR_TX_UUID, line.encode("utf-8"), response=False)
                print(f"[>> ESP32] {line}")
            except Exception as e:
                print(f"发送失败: {e}")

        # 5. 清理
        await client.stop_notify(CHAR_RX_UUID)
        print("已断开")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n退出")