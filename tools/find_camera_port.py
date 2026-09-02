#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_camera_port.py — 扫描所有 COM 口，找出 RA8P1 摄像头板（检测 USB 帧头 0x55 0xAA）"""
import serial
import time
import sys
import glob


def list_ports():
    ports = []
    try:
        import serial.tools.list_ports as lp
        ports = [p.device for p in lp.comports()]
    except Exception:
        pass
    if not ports:
        # 回退：显式枚举 COM1~COM32
        if sys.platform.startswith("win"):
            ports = ["COM%d" % i for i in range(1, 33)]
        else:
            ports = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    return ports


def main():
    ports = list_ports()
    print("[i] 扫描串口: %s" % ", ".join(ports))

    for dev in ports:
        try:
            ser = serial.Serial(dev, 115200, timeout=0.3)
        except Exception:
            continue
        print("[*] 测试 %s ..." % dev, end=" ")
        found = False
        buf = b""
        t0 = time.time()
        while time.time() - t0 < 2.0:
            data = ser.read(2048)
            if data:
                buf += data
                # 找帧头 0x55 0xAA
                idx = buf.find(b"\x55\xaa")
                if idx >= 0 and len(buf) >= idx + 8:
                    w = (buf[idx+2] << 8) | buf[idx+3]
                    h = (buf[idx+4] << 8) | buf[idx+5]
                    print("=> 找到摄像头板! %s, 帧 %dx%d, 收到 %d 字节" % (dev, w, h, len(buf)))
                    found = True
                    break
                if len(buf) > 100000:
                    break
        ser.close()
        if not found:
            print("无信号")
        if found:
            print("\n[i] 使用命令预览:")
            print("    py tools\\camera_viewer.py --port %s" % dev)
            return 0
    print("\n[!] 未找到摄像头板。检查: 板子是否上电? USB 线是否接 USBHS 口?")
    return 1


if __name__ == "__main__":
    sys.exit(main())
