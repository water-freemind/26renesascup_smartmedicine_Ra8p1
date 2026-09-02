#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 J-Link 读固件 sys_log 环形缓冲（诊断用）。"""
import struct
import subprocess
import sys

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"
ENTRY = 88  # tick(4) + level(4) + text[80]


def nm_symbols(elf, names):
    out = subprocess.check_output([NM, elf], text=True)
    syms = {n: [] for n in names}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] in syms:
            syms[parts[2]].append(int(parts[0], 16))
    return syms


def syslog_symbols(elf):
    """sys_log 实例地址：.map 锚定 s_total（区分 pickup_log 同名 static），
    s_next_index/s_entries 取 nm 最近候选（同一 TU 内连续）。"""
    import re
    elf_map = elf.rsplit(".", 1)[0] + ".map"
    anchor = None
    with open(elf_map, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\.bss\.s_total\s+0x([0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(\S+)$",
                         line.strip())
            if m and "sys_log.c.obj" in m.group(2):
                anchor = int(m.group(1), 16)
                break
    nm = nm_symbols(elf, ["s_total", "s_next_index", "s_entries"])
    out = {"s_total": anchor}
    for name in ("s_next_index", "s_entries"):
        cands = nm.get(name, [])
        if cands:
            out[name] = min(cands, key=lambda a: abs(a - anchor))
    return out


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "build/Debug/26renesascup_smartmedicine_Ra8p1.elf"
    syms = syslog_symbols(elf)
    print("syms:", {k: hex(v) for k, v in syms.items()})

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
            j.exec_command("go")
        import time
        time.sleep(0.3)
        total = j.memory_read32(syms["s_total"], 1)[0]
        nxt = j.memory_read32(syms["s_next_index"], 1)[0]
        raw = j.memory_read(syms["s_entries"], 32 * ENTRY)
        print("total=%d next=%d" % (total, nxt))
        # 打印最近 20 条（index 0 = 最新）
        count = min(total, 32)
        for idx in range(min(20, count)):
            slot = (nxt + 32 - 1 - idx) % 32
            base = slot * ENTRY
            tick, level = struct.unpack("<II", bytes(raw[base:base + 8]))
            text = bytes(raw[base + 8:base + 8 + 80]).split(b"\x00")[0]
            lv = {0: "INFO", 1: "OK", 2: "WARN", 3: "ERR"}.get(level, "?")
            try:
                s = text.decode("utf-8")
            except Exception:
                try:
                    s = text.decode("gbk")
                except Exception:
                    s = repr(text)
            print("[%s] t=%6d %s" % (lv, tick, s))
        # 最新一条的原始 hex
        slot = (nxt + 32 - 1) % 32
        base = slot * ENTRY
        print("latest raw:", bytes(raw[base:base + 96]).hex(" "))
    finally:
        try:
            j.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
