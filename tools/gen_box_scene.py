#!/usr/bin/env python3
"""生成模拟"纸盒条码"场景的 raw 灰度帧，验证解码器对小条码/低对比度/
背景干扰的鲁棒性。

用法: python tools/gen_box_scene.py
输出: .tmp/barcode_test/box_*.raw (640x480 灰度)
场景:
  - box_small_*: 模块 2px / 1.5px, 暗背景包围
  - box_lowcontrast_*: 低对比度
  - box_blur_*: 模糊
  - box_side_*: 条码带两侧深色图案（模拟药盒印刷）
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ean13_test import ean13_digits, build_pattern

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".tmp", "barcode_test")
W, H = 640, 480

def blur1d(row, radius=1):
    n = len(row)
    out = row[:]
    for _ in range(radius):
        nxt = out[:]
        for i in range(n):
            s = 0
            c = 0
            for d in (-1, 0, 1):
                j = i + d
                if 0 <= j < n:
                    s += out[j]
                    c += 1
            nxt[i] = s // c
        out = nxt
    return out

def make_frame(bits, module_px, bar_hi, bar_lo, bg, y0, x0, blur=0, side_pat=False, qz=40):
    """条码 bar_lo..bar_hi 灰度；背景 bg。side_pat 在静区外侧加深色图案块。
    qz：条码两侧白静区宽度（px）——EAN-13 印刷标准要求静区 ≥11 模块，
    真实纸盒条码两侧一定是白的，深色物体只在静区外。"""
    frame = bytearray([bg] * (W * H))
    bw = len(bits) * module_px
    bh = 60
    for yy in range(bh):
        row = []
        for x, bit in enumerate(bits):
            v = bar_lo if bit == '1' else bar_hi
            row.extend([v] * module_px)
        # 白静区（EAN-13 标准：条码两侧 ≥11 模块白）
        row = [bar_hi] * qz + row + [bar_hi] * qz
        if blur:
            row = blur1d(row, blur)
        for xx, v in enumerate(row):
            fx = x0 + xx
            fy = y0 + yy
            if 0 <= fx < W and 0 <= fy < H:
                frame[fy * W + fx] = v
    if side_pat:
        # 静区外侧各加一个 40px 宽的深色"图案"块（印刷文字模拟）
        for yy in range(bh):
            for xx in range(40):
                for fx in (x0 - 40 + xx, x0 + bw + qz * 2 + xx):
                    if 0 <= fx < W and 0 <= y0 + yy < H:
                        frame[(y0 + yy) * W + fx] = 90 if (xx // 8) % 2 else 140
    return frame

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    c13 = ean13_digits("690123456789")
    bits = build_pattern(c13)
    cases = [
        # (name, module, bar_hi, bar_lo, bg, blur, side_pat)
        ("small2px",      2, 200, 40, 90, 0, False),   # 模块2px 暗背景
        ("small1p5",      1, 200, 40, 90, 0, False),   # 模块1.5px（95模块≈143px）
        ("small2px_bright", 2, 230, 60, 180, 0, False),# 亮背景
        ("lowcontrast",   3, 170, 100, 90, 0, False),  # 对比度仅 70
        ("blur2px",       2, 200, 40, 90, 1, False),   # 模块2px+模糊
        ("side2px",       2, 200, 40, 90, 0, True),    # 两侧图案干扰
        ("side1p5",       1, 200, 40, 90, 0, True),    # 1.5px+两侧图案
    ]
    for name, mod, hi, lo, bg, blur, side in cases:
        bw = len(bits) * mod
        x0 = (W - bw) // 2
        y0 = (H - 60) // 2
        frame = make_frame(bits, mod, hi, lo, bg, y0, x0, blur, side)
        path = os.path.join(OUT_DIR, f"box_{name}_{c13}.raw")
        with open(path, "wb") as f:
            f.write(bytes(frame))
        print(f"OK {name}: module={mod}px bw={bw}px contrast={hi-lo} bg={bg} blur={blur} side={side}")

if __name__ == "__main__":
    main()
