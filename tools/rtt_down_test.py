#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# RTT down channel end-to-end test: write one TTF data frame via manual
# down[0] ring-buffer access, verify s_ttf_written advances.
# Usage: py tools/rtt_down_test.py <elf>
import struct
import subprocess
import sys
import time

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"

DOWN_OFFSET = 16 + 4 + 4 + 24


def nm_symbols(elf, names):
    out = subprocess.check_output([NM, elf], text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] in names:
            syms[parts[2]] = int(parts[0], 16)
    return syms


class ManualRTTDown:
    def __init__(self, jlink, ctrl_addr):
        self.j = jlink
        self.base = ctrl_addr + DOWN_OFFSET
        self.p_buffer = jlink.memory_read32(self.base + 4, 1)[0]
        self.size = jlink.memory_read32(self.base + 8, 1)[0]

    def write(self, data):
        if len(data) >= self.size:
            raise ValueError("frame too large")
        wo = self.j.memory_read32(self.base + 12, 1)[0]
        ro = self.j.memory_read32(self.base + 16, 1)[0]
        free = (ro - wo - 1) % self.size
        if len(data) > free:
            raise RuntimeError("down buffer full")
        first = min(len(data), self.size - wo)
        self.j.memory_write(self.p_buffer + wo, data[:first])
        if len(data) > first:
            self.j.memory_write(self.p_buffer, data[first:])
        self.j.memory_write32(self.base + 12, [(wo + len(data)) % self.size])


def main():
    if len(sys.argv) < 2:
        print("usage: py tools/rtt_down_test.py <elf>")
        return 1
    elf = sys.argv[1]
    syms = nm_symbols(elf, ["s_ttf_written", "_SEGGER_RTT"])
    print("symbols:", {k: hex(v) for k, v in syms.items()})
    if "s_ttf_written" not in syms or "_SEGGER_RTT" not in syms:
        print("symbols not found")
        return 1

    lib = pylink.Library(dllpath=DLL_X64)
    jlink = pylink.JLink(lib=lib)
    try:
        jlink.open()
        print("J-Link opened, serial:", jlink.serial_number)
        jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        jlink.set_speed(4000)
        jlink.exec_command("Device = " + DEVICE)
        if not jlink.target_connected():
            jlink._dll.JLINKARM_Connect()
        print("connected:", jlink.target_connected())
        if jlink.halted():
            jlink._dll.JLINKARM_Go()  # resume without resetting the firmware
        time.sleep(2.0)

        down = ManualRTTDown(jlink, syms["_SEGGER_RTT"])
        print("down[0] p_buffer=0x%X size=%d" % (down.p_buffer, down.size))

        def read_written():
            return jlink.memory_read32(syms["s_ttf_written"], 1)[0]

        w0 = read_written()
        print("s_ttf_written before:", w0)

        data = bytes(range(64))
        frame = b"TTF" + bytes([0x01]) + struct.pack(">III", 1, 0, len(data)) + data
        down.write(frame)
        print("frame written to down[0]")

        deadline = time.time() + 5.0
        w = w0
        while time.time() < deadline:
            time.sleep(0.05)
            w = read_written()
            if w >= w0 + len(data):
                break
        print("s_ttf_written after:", w, "expected >= ", w0 + len(data))
        if w >= w0 + len(data):
            print("RESULT: PASS - RTT down channel works")
        else:
            print("RESULT: FAIL - no progress")
            return 1
    finally:
        try:
            jlink.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
