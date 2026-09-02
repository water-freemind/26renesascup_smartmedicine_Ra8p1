#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
camera_viewer.py — OV7725 摄像头 PC 实时预览上位机
===================================================

配合 RA8P1 板载 USB (PCDC 虚拟串口) 使用。板端 usb_cdc.c 按以下协议发送：

    帧 = 8 字节帧头 + RGB565 像素数据
    帧头: [0x55][0xAA][w_hi][w_lo][h_hi][h_lo][seq_hi][seq_lo]

用法:
    pip install pyserial opencv-python
    python camera_viewer.py --port COM3
    python camera_viewer.py --port COM3 --fps 30 --save photos

按键:
    s 保存当前帧为 PNG     q/ESC 退出

依赖 (二选一):
    A. pyserial + opencv-python + numpy   → 实时显示窗口
    B. pyserial + numpy + Pillow          → 实时显示窗口 (无 OpenCV)
    C. pyserial 仅保存 raw 帧 (无显示)    → --save-raw
"""

import argparse
import struct
import sys
import time
import os

import serial

FRAME_HEADER_SIZE = 8
MAGIC_0 = 0x55
MAGIC_1 = 0xAA


def find_serial_port():
    """自动查找可能的串口 (Windows/COM*, Linux/ttyACM*, macOS/cu.usbmodem*)。"""
    import glob
    patterns = []
    if os.name == "nt":
        patterns = ["COM[0-9]*"]
    elif sys.platform.startswith("linux"):
        patterns = ["/dev/ttyACM*", "/dev/ttyUSB*"]
    else:  # darwin
        patterns = ["/dev/cu.usbmodem*", "/dev/cu.usbserial*"]
    ports = []
    for pat in patterns:
        ports.extend(glob.glob(pat))
    return sorted(ports)


def parse_args():
    ap = argparse.ArgumentParser(description="RA8P1 OV7725 USB PCDC 摄像头预览")
    ap.add_argument("--port", help="串口号 (如 COM3)。不填则自动查找")
    ap.add_argument("--baud", type=int, default=115200, help="波特率 (PCDC 虚拟串口一般任意)")
    ap.add_argument("--fps", type=int, default=30, help="显示目标帧率上限")
    ap.add_argument("--save", default=None, help="按 s 存图目录，如 photos")
    ap.add_argument("--save-raw", action="store_true", help="仅把收到的帧存为 .raw 文件，不显示")
    return ap.parse_args()


def main():
    args = parse_args()

    # --- 选择显示后端 ---
    backend = None
    try:
        import numpy as np
        try:
            import cv2
            backend = "cv2"
        except ImportError:
            try:
                from PIL import Image
                backend = "pil"
            except ImportError:
                backend = "numpy-only"
    except ImportError:
        backend = "raw-only"

    if args.save_raw:
        backend = "raw-only"
        print("[i] --save-raw: 仅保存 .raw 帧，不显示")

    port = args.port
    if not port:
        cand = find_serial_port()
        if not cand:
            print("[!] 未指定 --port 且未找到串口。请用 --port COMx 指定。")
            sys.exit(1)
        port = cand[0]
        print(f"[i] 自动选择串口: {port}")

    print(f"[i] 打开 {port} @ {args.baud}")
    ser = serial.Serial(port, args.baud, timeout=0.2)
    ser.reset_input_buffer()

    if args.save:
        os.makedirs(args.save, exist_ok=True)
    if args.save_raw:
        os.makedirs("raw_frames", exist_ok=True)

    # --- 帧同步缓冲 ---
    buf = bytearray()
    frame_no = 0
    last_show = 0.0
    stat_frames = 0
    stat_time = time.time()
    win_name = "OV7725 Preview (s=save, q=quit)"

    try:
        while True:
            data = ser.read(4096)
            if data:
                buf.extend(data)

            # 搜索帧头
            while True:
                idx = buf.find(bytes([MAGIC_0, MAGIC_1]))
                if idx < 0:
                    # 保留末尾最多 1 字节，防止跨包帧头被截断
                    buf = buf[-1:]
                    break
                if idx > 0:
                    del buf[:idx]

                # 需要 8 字节帧头
                if len(buf) < FRAME_HEADER_SIZE:
                    break

                w, h, seq = struct.unpack(">HHH", bytes(buf[2:8]))
                frame_size = FRAME_HEADER_SIZE + w * h * 2
                if w == 0 or h == 0 or w > 2048 or h > 2048:
                    print(f"[!] 非法帧尺寸 {w}x{h}，重新同步")
                    del buf[:1]
                    continue

                if len(buf) < frame_size:
                    # 等整帧到达
                    break

                payload = bytes(buf[FRAME_HEADER_SIZE:frame_size])
                del buf[:frame_size]
                frame_no += 1

                # --- 帧已就绪，处理 ---
                if backend == "cv2":
                    img = np.frombuffer(payload, dtype="<u2").reshape(h, w)
                    img = np.left_shift(img, 3) & 0xF8
                    # RGB565 → BGR888
                    r = (img >> 11) & 0x1F
                    g = (img >> 5) & 0x3F
                    b = img & 0x1F
                    bgr = np.stack([b << 3, g << 2, r << 3], axis=-1).astype(np.uint8)
                    now = time.time()
                    if now - last_show >= 1.0 / max(args.fps, 1):
                        last_show = now
                        cv2.imshow(win_name, bgr)
                        key = cv2.waitKey(1) & 0xFF
                        if key in (ord("q"), 27):
                            break
                        if key == ord("s") and args.save:
                            fn = os.path.join(args.save, f"frame_{frame_no:05d}.png")
                            cv2.imwrite(fn, bgr)
                            print(f"[s] 保存 {fn}")
                elif backend == "pil":
                    import numpy as np
                    from PIL import Image
                    img = np.frombuffer(payload, dtype="<u2").reshape(h, w)
                    r = ((img >> 11) & 0x1F) << 3
                    g = ((img >> 5) & 0x3F) << 2
                    b = (img & 0x1F) << 3
                    rgb = np.stack([r, g, b], axis=-1).astype(np.uint8)
                    now = time.time()
                    if now - last_show >= 1.0 / max(args.fps, 1):
                        last_show = now
                        im = Image.fromarray(rgb, "RGB")
                        im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
                        im.show()
                        if args.save:
                            fn = os.path.join(args.save, f"frame_{frame_no:05d}.png")
                            im.save(fn)
                            print(f"[s] 保存 {fn}")
                else:  # raw-only / numpy-only
                    if args.save_raw:
                        fn = os.path.join("raw_frames", f"frame_{frame_no:05d}_{w}x{h}.raw")
                        with open(fn, "wb") as f:
                            f.write(payload)
                        print(f"[raw] {fn}")

                stat_frames += 1
                if time.time() - stat_time >= 5.0:
                    fps = stat_frames / (time.time() - stat_time)
                    print(f"[i] {fps:.1f} FPS, 帧#{frame_no} {w}x{h}, seq={seq}")
                    stat_frames = 0
                    stat_time = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if backend == "cv2":
            cv2.destroyAllWindows()
        print("[i] 退出")


if __name__ == "__main__":
    main()
