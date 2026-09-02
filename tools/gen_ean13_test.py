#!/usr/bin/env python3
"""生成 EAN-13 商品条码测试 PNG（含正向/镜像/倾斜，供实机与 host 验证）。

用法: python tools/gen_ean13_test.py
输出: .tmp/barcode_test/*.png
依赖: python-barcode (pip install python-barcode) 或自行用 PIL 绘制。
本脚本优先用 python-barcode；不可用时退化为纯 PIL 绘制（标准 EAN-13 编码）。
"""
import os
import sys

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".tmp", "barcode_test")

def ean13_digits(code: str) -> str:
    """补齐校验位，返回 13 位数字串。"""
    if len(code) == 12 and code.isdigit():
        s = sum((3 if i % 2 else 1) * int(c) for i, c in enumerate(code))
        return code + str((10 - s % 10) % 10)
    if len(code) == 13 and code.isdigit():
        return code
    raise ValueError("需要 12 位（自动补校验）或 13 位数字")

# EAN-13 码表（1=条）
L = ["0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011"]
G = ["0100111","0110011","0011011","0100001","0011101","0111001","0000101","0010001","0001001","0010111"]
R = ["1110010","1100110","1101100","1000010","1011100","1001110","1010000","1000100","1001000","1110100"]
FIRST = ["000000","001011","001101","001110","010011","011001","011100","010101","010110","011010"]

def build_pattern(code13: str) -> str:
    """返回条码位串（1=条），含 guard。"""
    first = int(code13[0])
    fpat = FIRST[first]
    bits = "101"  # start
    for i in range(6):
        d = int(code13[1 + i])
        bits += (G[d] if fpat[i] == '1' else L[d])
    bits += "01010"  # middle
    for i in range(6):
        bits += R[int(code13[7 + i])]
    bits += "101"  # end
    return bits

def render(bits: str, module_px: int, height_px: int) -> "PIL.Image":
    from PIL import Image
    w = len(bits) * module_px
    img = Image.new("L", (w, height_px), 255)  # 白底
    px = img.load()
    for x, bit in enumerate(bits):
        if bit == '1':
            x0 = x * module_px
            for xx in range(x0, x0 + module_px):
                for yy in range(height_px):
                    px[xx, yy] = 0
    return img


def main():
    from PIL import Image
    os.makedirs(OUT_DIR, exist_ok=True)
    codes = ["690123456789", "697123456789"]  # 12 位，自动补校验
    module = 4
    height = 200
    for c12 in codes:
        c13 = ean13_digits(c12)
        bits = build_pattern(c13)
        img = render(bits, module, height)
        base = os.path.join(OUT_DIR, f"ean13_{c13}")
        img.save(base + ".png")
        # 镜像（模拟 OV7725 HFLIP）
        img.transpose(0).save(base + "_mirror.png")  # transpose(0)=左右翻转
        # 倾斜 5°
        w, h = img.size
        rot = img.rotate(5, expand=True, fillcolor=255)
        rot.save(base + "_tilt5.png")
        print(f"OK {c13}: {img.size[0]}x{img.size[1]} -> {base}.png (+mirror/tilt5)")

if __name__ == "__main__":
    main()
