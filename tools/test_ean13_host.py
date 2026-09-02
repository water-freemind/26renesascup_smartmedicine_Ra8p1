#!/usr/bin/env python3
"""Host 单元验证：编译 barcode_1d.c + ean13_host_main.c（PC 端可执行），
把生成的 EAN-13 PNG 转 raw 灰度喂入，验证正向/镜像/倾斜解码正确性。

用法: python tools/test_ean13_host.py
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "middleware", "src", "barcode_1d.c")
MAIN = os.path.join(ROOT, "tools", "ean13_host_main.c")
HEADER_DIR = os.path.join(ROOT, "src", "middleware", "inc")
TEST_DIR = os.path.join(ROOT, ".tmp", "barcode_test")


def build_bin():
    tmpdir = tempfile.mkdtemp(prefix="ean13_test_")
    exe = os.path.join(tmpdir, "ean13_host.exe" if os.name == "nt" else "ean13_host")
    cc = os.environ.get("CC", "gcc")
    subprocess.check_call([cc, SRC, MAIN, "-I", HEADER_DIR, "-o", exe, "-std=c99", "-O2"])
    return exe


def decode(exe, gray: np.ndarray) -> str:
    h, w = gray.shape
    rawf = os.path.join(tempfile.gettempdir(), "ean13_test_input.raw")
    with open(rawf, "wb") as f:
        f.write(np.ascontiguousarray(gray, dtype=np.uint8).tobytes())
    # host 从文件直接读，规避 Windows 子进程 stdin 在 ~144KB 截断
    proc = subprocess.run([exe, str(w), str(h), rawf],
                          capture_output=True)
    out = proc.stdout.decode("ascii", "replace").strip()
    return None if out == "FAIL" else out


def main():
    exe = build_bin()
    passed = 0
    failed = 0
    for fname in sorted(os.listdir(TEST_DIR)):
        if not fname.endswith(".png"):
            continue
        path = os.path.join(TEST_DIR, fname)
        img = Image.open(path).convert("L")
        gray = np.array(img, dtype=np.uint8)
        # 期望值从文件名提取：支持 ean13_XXXX...png / ean13_BIG_XXXX / ean13_PRINT_XXXX / ean13_thinN_XXXX
        stem = fname.replace(".png", "")
        parts = stem.split("_")
        expected = None
        for p in parts[1:]:
            if p.isdigit() and len(p) >= 12:
                expected = p
                break
        result = decode(exe, gray)
        ok = (result == expected)
        print(f"[{'PASS' if ok else 'FAIL'}] {fname}: expected={expected} got={result}")
        if ok:
            passed += 1
        else:
            failed += 1
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
