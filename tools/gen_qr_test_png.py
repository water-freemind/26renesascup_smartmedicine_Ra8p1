#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成实机扫码测试二维码 PNG（手机屏幕显示 → OV7725 摄像头识别）。

用法: py tools\\gen_qr_test_png.py
输出: .tmp\\qr_test\\QR_MED001.png（药品码，Scan 页测试）
      .tmp\\qr_test\\QR_RXORDER.png（取药单 JSON，Pickup 页测试）
"""
import os
import sys
import qrcode

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   ".tmp", "qr_test")
BOX = 14          # 每模块像素（手机屏足够大，摄像头易识别）
BORDER = 4        # 静区模块数

CASES = [
    ("QR_MED001", "MED-001", "药品码：Scan 页测试"),
    ("QR_RXORDER", '{"oid":"RX-20260818-001","i":[{"id":"MED-001","n":2},{"id":"MED-007","n":1}]}',
     "取药单（完整 JSON 45x45 码，需放得很近）"),
    ("QR_RXSHORT", "RX-20260818-001", "取药单（简短单号 25x25 码，易识别）"),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, payload, note in CASES:
        qr = qrcode.QRCode(version=None, error_correction=qrcode.constants.ERROR_CORRECT_M,
                           box_size=BOX, border=BORDER)
        qr.add_data(payload)
        qr.make(fit=True)
        img = qr.make_image(fill_color="black", back_color="white")
        fn = os.path.join(OUT, name + ".png")
        img.save(fn)
        print("%s: %d B payload, %dx%d px  (%s)" %
              (fn, len(payload), img.width, img.height, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
