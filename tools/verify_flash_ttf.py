#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Verify OSPI flash region [0x80000000 .. 0x80000000+size) matches the TTF file.
# Usage: py tools/verify_flash_ttf.py <ttf> [size]
# If size omitted, use file size. Reads flash via J-Link AHB (OSPI mmap).
import hashlib
import struct
import sys
import time

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
DEVICE = "R7KA8P1KF"
FLASH_BASE = 0x80000000
BLOCK = 4096  # read block size


def main():
    if len(sys.argv) < 2:
        print("usage: py tools/verify_flash_ttf.py <ttf> [size]")
        return 1
    ttf_path = sys.argv[1]
    with open(ttf_path, "rb") as f:
        data = f.read()
    size = len(data) if len(sys.argv) < 3 else int(sys.argv[2], 0)
    if size > len(data):
        size = len(data)
    print("TTF: %s, %d bytes; flash base 0x%X size 0x%X" % (ttf_path, len(data), FLASH_BASE, size))
    md5_file = hashlib.md5(data[:size]).hexdigest()
    print("file md5: %s" % md5_file)

    lib = pylink.Library(dllpath=DLL_X64)
    jlink = pylink.JLink(lib=lib)
    try:
        jlink.open()
        jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        jlink.set_speed(4000)
        jlink.exec_command("Device = " + DEVICE)
        if not jlink.target_connected():
            jlink._dll.JLINKARM_Connect()
        print("connected:", jlink.target_connected())

        # read flash region in blocks, hash incrementally
        md5_flash = hashlib.md5()
        n_bad = 0
        t0 = time.time()
        for off in range(0, size, BLOCK):
            n = min(BLOCK, size - off)
            raw = bytes(jlink.memory_read(FLASH_BASE + off, n))
            md5_flash.update(raw)
            # also spot-compare block head
            if raw != data[off:off + n]:
                n_bad += 1
        el = time.time() - t0
        md5_flash_hex = md5_flash.hexdigest()
        print("flash md5: %s (%.1fs, %d mismatched blocks)" % (md5_flash_hex, el, n_bad))
        if md5_flash_hex == md5_file:
            print("RESULT: PASS - flash content matches TTF file")
            return 0
        else:
            # find first mismatch offset
            bad = None
            for off in range(0, size, BLOCK):
                n = min(BLOCK, size - off)
                raw = bytes(jlink.memory_read(FLASH_BASE + off, n))
                if raw != data[off:off + n]:
                    for i in range(n):
                        if raw[i] != data[off + i]:
                            bad = off + i
                            break
                    break
            print("RESULT: FAIL - first mismatch at 0x%X (flash=0x%02X file=0x%02X)"
                  % (bad, raw[bad - off] if bad is not None else 0,
                     data[bad] if bad is not None else 0))
            return 1
    finally:
        try:
            jlink.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
