#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Icon region burner: write build/ospi_icons.bin into the OSPI icon region
# (offset 0x00950000) via the RTT down channel.
# Frame: [0..2]'TTF' [3]cmd(0x00 reset,0x02 done,0x10 icon-data)
#        [4..7]seq BE32 [8..11]offset(rel. icon region) BE32 [12..15]len BE32 [16..]data
# Usage: py tools/ospi_burn_icons.py <elf> <icons.bin>
import struct
import subprocess
import sys
import time

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"

CHUNK = 1024
DOWN_OFFSET = 16 + 4 + 4 + 24
CMD_ICON_DATA = 0x10  # must match TTF_LOADER_CMD_ICON_DATA in ospi_ttf_loader.c


def _read32_retry(jlink, addr, retries=10, delay=0.5):
    """J-Link 直读 RAM 偶发失败（Unspecified error），自动重试。"""
    last = None
    for _ in range(retries):
        try:
            return jlink.memory_read32(addr, 1)[0]
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(delay)
    raise last


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
        self.p_buffer = _read32_retry(jlink, self.base + 4)
        self.size = _read32_retry(jlink, self.base + 8)

    def write_blocking(self, data, timeout=10.0):
        deadline = time.time() + timeout
        while True:
            wo = _read32_retry(self.j, self.base + 12)
            ro = _read32_retry(self.j, self.base + 16)
            free = (ro - wo - 1) % self.size
            if free >= len(data):
                break
            if time.time() > deadline:
                raise RuntimeError("down buffer still full after %.1fs" % timeout)
            time.sleep(0.01)
        first = min(len(data), self.size - wo)
        self.j.memory_write(self.p_buffer + wo, data[:first])
        if len(data) > first:
            self.j.memory_write(self.p_buffer, data[first:])
        self.j.memory_write32(self.base + 12, [(wo + len(data)) % self.size])


def main():
    if len(sys.argv) < 3:
        print("usage: py tools/ospi_burn_icons.py <elf> <icons.bin>")
        return 1
    elf = sys.argv[1]
    bin_path = sys.argv[2]

    with open(bin_path, "rb") as f:
        payload = f.read()
    print("icons: %d bytes (%s)" % (len(payload), bin_path))
    if len(payload) % 8 != 0:
        print("ERROR: payload size not multiple of 8")
        return 1

    syms = nm_symbols(elf, ["s_ttf_written", "s_ttf_busy", "s_ttf_chip_erased", "_SEGGER_RTT",
                            "s_ospi_ready", "s_ttf_burn_mode", "s_ttf_last_err"])
    print("symbols:", {k: hex(v) for k, v in syms.items()})
    for need in ("s_ttf_written", "s_ttf_chip_erased", "_SEGGER_RTT", "s_ttf_burn_mode"):
        if need not in syms:
            print("symbol %s not found" % need)
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

        jlink.reset()
        time.sleep(1.0)   # 等 CPU 复位稳定，避免立即读失败
        jlink.memory_write32(syms["s_ttf_burn_mode"], [1])
        try:
            jlink.memory_write32(0xE000EF70, [syms["s_ttf_burn_mode"]])
        except Exception:
            pass
        print("burn mode forced on")
        jlink._dll.JLINKARM_Go()
        deadline = time.time() + 15.0
        while time.time() < deadline:
            if "s_ospi_ready" in syms and _read32_retry(jlink, syms["s_ospi_ready"]):
                break
            time.sleep(0.2)
        time.sleep(1.0)

        down = ManualRTTDown(jlink, syms["_SEGGER_RTT"])
        print("down[0] p_buffer=0x%X size=%d" % (down.p_buffer, down.size))

        def read_u32(addr):
            return _read32_retry(jlink, addr)

        def frame(cmd, seq, offset, data):
            return (b"TTF" + bytes([cmd]) + struct.pack(">III", seq, offset, len(data)) + data)

        down.write_blocking(frame(0x00, 0, 0, b""))
        deadline = time.time() + 3.0
        while time.time() < deadline:
            wo = jlink.memory_read32(down.base + 12, 1)[0]
            ro = jlink.memory_read32(down.base + 16, 1)[0]
            if wo == ro:
                break
            time.sleep(0.05)
        print("loader state reset")

        print("waiting for flash idle (WIP clear)...")
        down.write_blocking(frame(0x04, 0, 0, b""))
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if read_u32(syms["s_ttf_busy"]) == 1:
                break
            time.sleep(0.05)
        deadline = time.time() + 320.0
        while time.time() < deadline:
            if read_u32(syms["s_ttf_busy"]) == 0:
                break
            time.sleep(0.2)
        last_err = read_u32(syms["s_ttf_last_err"]) if "s_ttf_last_err" in syms else 0
        if last_err != 0:
            print("ERROR: wait-idle failed (s_ttf_last_err=0x%08X)" % last_err)
            return 1
        print("flash idle OK")

        seq = 1
        n_chunks = (len(payload) + CHUNK - 1) // CHUNK
        t0 = time.time()
        for idx in range(n_chunks):
            off = idx * CHUNK
            data = payload[off:off + CHUNK]
            down.write_blocking(frame(CMD_ICON_DATA, seq, off, data))
            seq += 1
            expect = off + len(data)
            deadline = time.time() + 10.0
            while time.time() < deadline:
                if read_u32(syms["s_ttf_written"]) >= expect:
                    break
                time.sleep(0.01)
            got = read_u32(syms["s_ttf_written"])
            if got < expect:
                last_err = read_u32(syms["s_ttf_last_err"]) if "s_ttf_last_err" in syms else 0
                print("ERROR: chunk %d stalled (written=%d expect=%d, s_ttf_last_err=0x%08X)"
                      % (idx, got, expect, last_err))
                return 1
            if idx % 200 == 0 or idx == n_chunks - 1:
                el = time.time() - t0
                print("  chunk %d/%d offset=0x%X written=%d (%.1fs)" % (idx + 1, n_chunks, off, got, el))

        down.write_blocking(frame(0x02, seq, 0, b""))
        time.sleep(0.5)
        final = read_u32(syms["s_ttf_written"])
        print("final s_ttf_written: %d (expected %d)" % (final, len(payload)))
        if final != len(payload):
            print("RESULT: FAIL - size mismatch")
            return 1
        print("RESULT: PASS - icons burned to OSPI icon region (%.1fs)" % (time.time() - t0))
        try:
            jlink._dll.JLINKARM_Go()
        except Exception:
            pass
        print("firmware running")
    finally:
        try:
            jlink.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
