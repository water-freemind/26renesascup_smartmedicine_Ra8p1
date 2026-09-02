#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# gen_ospi_icons.py: pack GUI Guider icon pixel arrays into one OSPI region
# binary and emit src/middleware/src/ospi_icons.c (lv_image_dsc_t with .data
# pointing into the OSPI memory-mapped region, so the 160KB of icon pixels
# stop occupying internal flash).
#
# Region base (must match ICON_FLASH_BASE_OFFSET in ospi_ttf_loader.c):
#   OSPI offset 0x00950000  ->  mmap address 0x80095000
#
# Usage:  py tools/gen_ospi_icons.py
#   writes build/ospi_icons.bin            (payload for ospi_burn_icons.py)
#   writes src/middleware/src/ospi_icons.c (replaces the generated images/*.c)
import os
import re
import sys

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG_DIR = os.path.join(ROOT, "gui", "RA8P1", "generated", "assets", "images")
GG_IMAGE_H = os.path.join(IMG_DIR, "gg_image.h")
OUT_C = os.path.join(ROOT, "src", "middleware", "src", "ospi_icons.c")
OUT_BIN = os.path.join(ROOT, "build", "ospi_icons.bin")

ICON_REGION_OFFSET = 0x00950000  # must match ICON_FLASH_BASE_OFFSET
MMAP_BASE = 0x80000000

# Boot 页背景照片：resources/image/cover_640x332.png（由 封面_compressed.png 预处理缩放，
# 供 GUI Guider 画布引用；打包为 640x332 ARGB8888 铺满白色区域）
BOOT_PHOTO_SRC = os.path.join(ROOT, "gui", "RA8P1", "resources", "image", "cover_640x332.png")
BOOT_PHOTO_W = 640
BOOT_PHOTO_H = 332


def parse_icon_c(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        txt = f.read()
    name = os.path.basename(path).replace(".c", "")
    # pixel bytes
    m = re.search(r"_map\[\]\s*=\s*\{(.*?)\};", txt, re.S)
    if not m:
        return None
    body = m.group(1)
    pixels = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    # descriptor fields
    dm = re.search(r"const lv_image_dsc_t\s+\w+\s*=\s*\{(.*?)\};", txt, re.S)
    fields = {}
    if dm:
        dsc = dm.group(1)
        for key in ("magic", "cf", "stride", "flags", "w", "h", "data_size"):
            km = re.search(r"\.header\.%s\s*=\s*([^,}]+)" % key, dsc)
            if km:
                fields[key] = km.group(1).strip()
            else:
                km2 = re.search(r"\.%s\s*=\s*([^,}]+)" % key, dsc)
                if km2:
                    fields[key] = km2.group(1).strip()
    return {"name": name, "pixels": pixels, "fields": fields}


def main():
    if not os.path.isdir(IMG_DIR):
        print("images dir not found: %s" % IMG_DIR)
        return 1

    # order follows gg_image.h declarations (stable), then any remaining
    # icon_*.c in the images dir (GUI Guider generates C arrays for every
    # referenced resource, incl. ones not listed in gg_image.h), so a
    # regeneration never leaves a referenced dsc symbol undefined.
    order = []
    if os.path.isfile(GG_IMAGE_H):
        with open(GG_IMAGE_H, "r", encoding="utf-8", errors="replace") as f:
            order = re.findall(r"LV_IMAGE_DECLARE\((\w+)\)", f.read())
    for fname in sorted(os.listdir(IMG_DIR)):
        if fname.endswith(".c"):
            name = fname[:-2]
            if name not in order:
                order.append(name)

    icons = []
    for name in order:
        path = os.path.join(IMG_DIR, name + ".c")
        if not os.path.isfile(path):
            print("WARN: %s.c missing" % name)
            continue
        ic = parse_icon_c(path)
        if ic is None or not ic["pixels"]:
            print("WARN: %s.c parse failed, skipped" % name)
            continue
        icons.append(ic)

    # Boot 页背景照片：PIL 缩放 -> ARGB8888（内存序 B,G,R,A）
    if HAS_PIL and os.path.isfile(BOOT_PHOTO_SRC):
        im = Image.open(BOOT_PHOTO_SRC).convert("RGBA")
        im = im.resize((BOOT_PHOTO_W, BOOT_PHOTO_H), Image.LANCZOS)
        px = im.load()
        pixels = bytearray()
        for y in range(BOOT_PHOTO_H):
            for x in range(BOOT_PHOTO_W):
                r, g, b, a = px[x, y]
                pixels += bytes((b, g, r, a))
        icons.append({
            "name": "boot_photo",
            "pixels": bytes(pixels),
            "fields": {"stride": str(BOOT_PHOTO_W * 4), "w": str(BOOT_PHOTO_W),
                       "h": str(BOOT_PHOTO_H), "cf": "LV_COLOR_FORMAT_ARGB8888"},
        })
        print("boot_photo: %dx%d -> %d bytes" % (BOOT_PHOTO_W, BOOT_PHOTO_H, len(pixels)))
    else:
        print("WARN: PIL or boot photo missing, boot_photo skipped")

    if not icons:
        print("ERROR: no icons parsed")
        return 1

    # pack
    blob = bytearray()
    offsets = []
    for ic in icons:
        offsets.append(len(blob))
        blob += ic["pixels"]
    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    with open(OUT_BIN, "wb") as f:
        f.write(bytes(blob))
    print("packed %d icons -> %s (%d bytes, %d B at 0x%X in OSPI)"
          % (len(icons), OUT_BIN, len(blob), ICON_REGION_OFFSET, MMAP_BASE + ICON_REGION_OFFSET))

    # emit ospi_icons.c
    lines = []
    lines.append("/* AUTO-GENERATED by tools/gen_ospi_icons.py - DO NOT EDIT BY HAND. */")
    lines.append("/*")
    lines.append(" * Icon pixel data lives in the OSPI icon region")
    lines.append(" * (offset 0x%X, mmap 0x%X, see ICON_FLASH_BASE_OFFSET in ospi_ttf_loader.c)." % (ICON_REGION_OFFSET, MMAP_BASE + ICON_REGION_OFFSET))
    lines.append(" * These descriptors replace gui/RA8P1/generated/assets/images/*.c")
    lines.append(" * (excluded from the build in CMakeLists.txt) and free ~%d KB of internal flash." % (len(blob) // 1024))
    lines.append(" */")
    lines.append('#include "lvgl.h"')
    lines.append("")
    lines.append("#define OSPI_ICON_REGION_BASE ((const uint8_t *) 0x%08XUL)" % (MMAP_BASE + ICON_REGION_OFFSET))
    lines.append("")
    for ic, off in zip(icons, offsets):
        f = ic["fields"]
        stride = f.get("stride", str(len(ic["pixels"]) // max(1, int(f.get("h", "1") or 1))))
        w = f.get("w", "0")
        h = f.get("h", "0")
        cf = f.get("cf", "LV_COLOR_FORMAT_ARGB8888")
        lines.append("const lv_image_dsc_t %s = {" % ic["name"])
        lines.append("  .header.magic = LV_IMAGE_HEADER_MAGIC,")
        lines.append("  .header.cf = %s," % cf)
        lines.append("  .header.stride = %s," % stride)
        lines.append("  .header.flags = 0,")
        lines.append("  .header.w = %s," % w)
        lines.append("  .header.h = %s," % h)
        lines.append("  .data_size = %d," % len(ic["pixels"]))
        lines.append("  .data = OSPI_ICON_REGION_BASE + 0x%08XUL," % off)
        lines.append("};")
        lines.append("")
        print("  %-40s off=0x%07X size=%d" % (ic["name"], off, len(ic["pixels"])))
    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("wrote %s" % OUT_C)
    return 0


if __name__ == "__main__":
    sys.exit(main())
