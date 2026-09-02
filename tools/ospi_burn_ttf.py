#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# TTF font burner: write a TTF into OSPI W25Q256 via RTT down channel.
# Frame: [0..2]'TTF' [3]cmd(0x00 reset,0x01 data,0x02 done,0x03 chip-erase)
#        [4..7]seq BE32 [8..11]offset BE32 [12..15]len BE32 [16..]data
# Usage: py tools/ospi_burn_ttf.py <elf> <ttf> [--erase]
import struct
import subprocess
import sys
import time

import pylink

DLL_X64 = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink_x64.dll"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"

# CHUNK must be multiple of 8, 16+CHUNK<=2048 (RTT down buffer), and divide
# 65536 (64KB block-erase boundary) so every block start is frame-covered.
CHUNK = 1024
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

    def write_blocking(self, data, timeout=10.0):
        deadline = time.time() + timeout
        while True:
            wo = self.j.memory_read32(self.base + 12, 1)[0]
            ro = self.j.memory_read32(self.base + 16, 1)[0]
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
        print("usage: py tools/ospi_burn_ttf.py <elf> <ttf> [--erase]")
        return 1
    elf = sys.argv[1]
    ttf_path = sys.argv[2]
    do_erase = "--erase" in sys.argv

    with open(ttf_path, "rb") as f:
        ttf = f.read()
    ttf_len = len(ttf)
    print("TTF: %d bytes (%s)" % (ttf_len, ttf_path))
    if ttf_len % 8 != 0:
        print("ERROR: TTF size not multiple of 8")
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

        # reset firmware (it may be stuck in HardFault from rendering the
        # incomplete font), then FORCE burn mode BEFORE it runs: s_ttf_burn_mode
        # is in .noinit (RAM survives reset/startup bss-clear), so writing it
        # now while halted at the reset vector makes the whole boot skip
        # tiny_ttf creation -> no stbtt assert -> loader keeps polling.
        jlink.reset()
        jlink.memory_write32(syms["s_ttf_burn_mode"], [1])
        # invalidate the D-Cache line for that address (DCIMVAC) so the CPU
        # does not read a stale cached value after it starts running
        try:
            jlink.memory_write32(0xE000EF70, [syms["s_ttf_burn_mode"]])
        except Exception:
            pass
        print("burn mode forced on (s_ttf_burn_mode=1)")
        jlink._dll.JLINKARM_Go()
        # wait until the Camera thread finishes OSPI init (s_ospi_ready==1)
        deadline = time.time() + 15.0
        while time.time() < deadline:
            if "s_ospi_ready" in syms and jlink.memory_read32(syms["s_ospi_ready"], 1)[0]:
                break
            time.sleep(0.2)
        time.sleep(1.0)

        down = ManualRTTDown(jlink, syms["_SEGGER_RTT"])
        print("down[0] p_buffer=0x%X size=%d" % (down.p_buffer, down.size))

        def read_u32(addr):
            return jlink.memory_read32(addr, 1)[0]

        def frame(cmd, seq, offset, data):
            return (b"TTF" + bytes([cmd]) + struct.pack(">III", seq, offset, len(data)) + data)

        down.write_blocking(frame(0x00, 0, 0, b""))
        # wait until the firmware consumes the reset frame (down ring empty),
        # otherwise the next data frame glues to it and the loader mis-parses
        deadline = time.time() + 3.0
        while time.time() < deadline:
            wo = jlink.memory_read32(down.base + 12, 1)[0]
            ro = jlink.memory_read32(down.base + 16, 1)[0]
            if wo == ro:
                break
            time.sleep(0.05)
        if read_u32(syms["s_ttf_written"]) != 0:
            print("WARN: s_ttf_written not reset")
        print("loader state reset")

        # wait for any residual WIP from an interrupted erase (CMD 0x04):
        # the loader sets busy=1 while waiting, busy=0 when the flash is idle
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

        if do_erase:
            print("sending chip erase (0x03)...")
            down.write_blocking(frame(0x03, 0, 0, b""))
            print("chip erase issued, waiting (up to ~360s)...")
            deadline = time.time() + 370.0
            while time.time() < deadline:
                if read_u32(syms["s_ttf_chip_erased"]) == 1:
                    break
                time.sleep(1.0)
            if read_u32(syms["s_ttf_chip_erased"]) != 1:
                print("ERROR: chip erase did not complete")
                return 1
            print("chip erase done")

        seq = 1
        n_chunks = (ttf_len + CHUNK - 1) // CHUNK
        t0 = time.time()
        for idx in range(n_chunks):
            off = idx * CHUNK
            data = ttf[off:off + CHUNK]
            down.write_blocking(frame(0x01, seq, off, data))
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
            if idx % 500 == 0 or idx == n_chunks - 1:
                el = time.time() - t0
                print("  chunk %d/%d offset=0x%X written=%d (%.1fs)" % (idx + 1, n_chunks, off, got, el))

        down.write_blocking(frame(0x02, seq, 0, b""))
        time.sleep(0.5)
        final = read_u32(syms["s_ttf_written"])
        print("final s_ttf_written: %d (expected %d)" % (final, ttf_len))
        if final != ttf_len:
            print("RESULT: FAIL - size mismatch")
            return 1
        print("RESULT: PASS - TTF burned to OSPI flash (%.1fs)" % (time.time() - t0))
        # leave the firmware running (burn mode cleared by DONE frame -> GUI
        # will create the tiny fonts from the now-complete data on next retry)
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
