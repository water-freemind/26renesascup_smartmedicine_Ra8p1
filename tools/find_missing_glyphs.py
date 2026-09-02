#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""找 tiny_ttf 演示测试字：
1. 解析编译期字库（lv_font_SourceHanSerifSC_*.c）覆盖的码点集合
2. 生成 GB2312 全部 6763 汉字
3. 差集 = GB2312 有但编译字库没有的字
4. 在仿宋_GB2312.ttf 的 cmap format4 里验证字形存在
输出候选字。
"""
import glob
import re
import struct

FONT_DIR = r"gui\RA8P1\generated\assets\fonts"
TTF = r"gui\RA8P1\resources\font\仿宋_GB2312.ttf"


def parse_font_cov(file):
    """从生成的 lv_font C 文件收集覆盖的 unicode 码点（只解析 unicode_list 数组）。"""
    src = open(file, "r", encoding="utf-8", errors="replace").read()
    cps = set()
    for m in re.finditer(r"static\s+const\s+uint(?:16|32)_t\s+unicode_list_\d+\s*\[\]\s*=\s*\{([\s\S]*?)\n\};", src):
        body = m.group(1)
        for v in re.findall(r"0x([0-9a-fA-F]+)", body):
            cps.add(int(v, 16))
        # 数组内也可能是十进制
        for v in re.findall(r"(\d+),", body):
            n = int(v)
            if 32 < n < 0x10FFFF:
                cps.add(n)
    return cps


def gb2312_chars():
    """GB2312 全部汉字（区 16-87，位 1-94）。"""
    chars = []
    for area in range(16, 88):
        for pos in range(1, 95):
            try:
                b = bytes([area + 0xA0, pos + 0xA0])
                ch = b.decode("gb2312")
                if len(ch) == 1 and '\u4e00' <= ch <= '\u9fff':
                    chars.append(ch)
            except Exception:
                pass
    return chars


def glyph_in_ttf(cp, data, sub_off):
    segX2 = struct.unpack(">H", data[sub_off + 6:sub_off + 8])[0]
    seg = segX2 // 2
    endCode = [struct.unpack(">H", data[sub_off + 14 + 2 * i:sub_off + 16 + 2 * i])[0] for i in range(seg)]
    startCode = [struct.unpack(">H", data[sub_off + 16 + segX2 + 2 * i:sub_off + 18 + segX2 + 2 * i])[0] for i in range(seg)]
    idDelta = [struct.unpack(">h", data[sub_off + 16 + 2 * segX2 + 2 * i:sub_off + 18 + 2 * segX2 + 2 * i])[0] for i in range(seg)]
    idRangeOff = [struct.unpack(">H", data[sub_off + 16 + 3 * segX2 + 2 * i:sub_off + 18 + 3 * segX2 + 2 * i])[0] for i in range(seg)]
    for i in range(seg):
        if startCode[i] <= cp <= endCode[i]:
            if idRangeOff[i] == 0:
                g = (cp + idDelta[i]) & 0xFFFF
                return g
            pos = sub_off + 16 + 4 * segX2 + idRangeOff[i] + 2 * (cp - startCode[i])
            if pos + 2 > len(data):
                return 0
            g = struct.unpack(">H", data[pos:pos + 2])[0]
            if g != 0:
                g = (g + idDelta[i]) & 0xFFFF
            return g
    return 0


def main():
    cov = set()
    for f in glob.glob(FONT_DIR + r"\lv_font_*.c"):
        cov |= parse_font_cov(f)
    print("编译字库覆盖码点:", len(cov))

    gb = gb2312_chars()
    print("GB2312 汉字:", len(gb))

    data = open(TTF, "rb").read()
    cmap_off = 0x00189608
    version, num = struct.unpack(">HH", data[cmap_off:cmap_off + 4])
    sub_off = None
    for i in range(num):
        rec = cmap_off + 4 + i * 8
        pid, eid, off = struct.unpack(">HHI", data[rec:rec + 8])
        if pid == 3 and eid == 1:
            sub_off = cmap_off + off
            break
    print("cmap format4 @ 0x%X" % sub_off)

    # 在 GB2312 但不在编译字库，且仿宋有字形
    candidates = []
    for ch in gb:
        cp = ord(ch)
        if cp in cov:
            continue
        if glyph_in_ttf(cp, data, sub_off):
            candidates.append(ch)
    print("GB2312 有 + 字库没有 + 仿宋有字形:", len(candidates))
    print("".join(candidates[:60]))
    # 挑选不常用的（GB2312 二级区 56-87）
    rare = [c for c in candidates if 0xB8A1 <= c.encode("gb2312")[0] <= 0xF7FE]
    print("其中二级生僻字:", "".join(rare[:60]))


if __name__ == "__main__":
    main()
