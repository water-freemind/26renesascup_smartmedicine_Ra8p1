#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 J-Link 读取解码线程最近一帧灰度快照（s_qr_frame_snapshot，SDRAM）并转 PNG。
用法: py tools\\dump_qr_frame.py [elf]   # 输出 .tmp/qr_test/frame_latest.png + 诊断
前置：用户在 Scan/Store 页把条码/二维码停在摄像头前（快照随解码每 200ms 更新）。
注意：VGA 640x480 快照在 SDRAM，读 614KB；D-Cache 写回需固件侧 clean（已做）。"""
import os
import struct
import subprocess
import sys
import time

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"
W, H = 640, 480

STATUS = {0: "OK", 1: "NO_CODE", 2: "DECODE_FAILED", 3: "NOT_READY",
          4: "INVALID_ARGUMENT", 5: "IMAGE_SIZE_MISMATCH", 6: "OUTPUT_TOO_SMALL",
          7: "NO_MEMORY"}


def nm_syms(elf, names):
    out = subprocess.check_output([NM, elf], text=True)
    found = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3 and p[2] in names:
            found[p[2]] = int(p[0], 16)
    return found


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "build/Debug/26renesascup_smartmedicine_Ra8p1.elf"
    syms = nm_syms(elf, ["s_qr_frame_snapshot", "s_dbg_qr_last_status",
                         "s_dbg_qr_frame_mean", "s_dbg_qr_decode_count",
                         "s_dbg_qr_decode_ms", "s_dbg_ean13_try", "s_dbg_ean13_ok",
                         "s_dbg_ean13_fail"])
    print("syms:", {k: hex(v) for k, v in syms.items()})
    if "s_qr_frame_snapshot" not in syms:
        print("ERROR: snapshot symbol not found")
        return 1

    lib = pylink.Library(dllpath=DLL_X64)
    j = pylink.JLink(lib=lib)
    try:
        j.open()
        j.set_tif(pylink.enums.JLinkInterfaces.SWD)
        j.set_speed(4000)
        j.exec_command("Device = " + DEVICE)
        if not j.target_connected():
            j._dll.JLINKARM_Connect()
        if j.halted():
            j._dll.JLINKARM_Go()
        time.sleep(0.3)

        raw = j.memory_read(syms["s_qr_frame_snapshot"], W * H)
        cnt = j.memory_read32(syms["s_dbg_qr_decode_count"], 1)[0]
        ms = j.memory_read32(syms["s_dbg_qr_decode_ms"], 1)[0]
        st = j.memory_read32(syms["s_dbg_qr_last_status"], 1)[0]
        mean = j.memory_read32(syms["s_dbg_qr_frame_mean"], 1)[0]
        e13t = j.memory_read32(syms.get("s_dbg_ean13_try", 0), 1)[0] if "s_dbg_ean13_try" in syms else -1
        e13o = j.memory_read32(syms.get("s_dbg_ean13_ok", 0), 1)[0] if "s_dbg_ean13_ok" in syms else -1
        e13f = j.memory_read32(syms.get("s_dbg_ean13_fail", 0), 1)[0] if "s_dbg_ean13_fail" in syms else -1
        print("decode_count=%d decode_ms=%d last_status=%s frame_mean=%d" %
              (cnt, ms, STATUS.get(st, st), mean))
        print("ean13: try=%d ok=%d fail=%d" % (e13t, e13o, e13f))

        out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               ".tmp", "qr_test")
        os.makedirs(out_dir, exist_ok=True)
        png = os.path.join(out_dir, "frame_latest.png")
        from PIL import Image
        img = Image.frombytes("L", (W, H), bytes(raw))
        img.save(png)  # 原尺寸保存
        print("saved:", png)

        # 亮度直方图概览（每 32 级）
        hist = [0] * 8
        for b in raw:
            hist[b >> 5] += 1
        tot = W * H
        print("hist: " + " ".join("%d:%5.1f%%" % (i * 32, 100.0 * h / tot) for i, h in enumerate(hist)))

        # 保存 raw（供 host 解码器复现）
        rawf = os.path.join(out_dir, "frame_latest.raw")
        with open(rawf, "wb") as f:
            f.write(bytes(raw))
        print("saved raw:", rawf)
    finally:
        try:
            j.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
