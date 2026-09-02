#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""J-Link 触发并读取板端二维码自检结果（诊断用）。

流程（全部走 J-Link Commander，pylink 的 reset+go 会把系统卡在调度器启动）：
复位（冷缓存）→ w4 直写 s_dbg_qr_selftest_trigger=1/2（.noinit，不被启动清零
掩蔽）→ go → 等待 → halt → 读回 trigger（0=已消费）、decode_count/decode_ms、
s_selftest_result 结构体与最近系统日志（sys_log 实例经 .map 解析，避开
pickup_log 同名 static）。

用法: py tools\\probe_qr_selftest.py [1|2] [elf]
     1=MED-001（默认） 2=取药单 JSON
"""
import os
import re
import struct
import subprocess
import sys

JLINK = r"C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe"
NM = r"C:\Users\Zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-nm.exe"
DEVICE = "R7KA8P1KF"

STATUS = {0: "OK", 1: "NO_CODE", 2: "DECODE_FAILED", 3: "NOT_READY",
          4: "INVALID_ARGUMENT", 5: "IMAGE_SIZE_MISMATCH", 6: "OUTPUT_TOO_SMALL",
          7: "NO_MEMORY"}


def nm_symbols(elf, names):
    out = subprocess.check_output([NM, elf], text=True)
    syms = {n: [] for n in names}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] in syms:
            syms[parts[2]].append(int(parts[0], 16))
    return syms


def map_syslog_symbols(elf_map, nm_syms):
    """Resolve the sys_log.c.obj symbol addresses.

    The map attributes .bss.s_total to its object file (sys_log.c.obj vs
    pickup_log.c.obj), but lists s_next_index/s_entries as bare names without
    addresses.  Anchor on the map's sys_log s_total, then pick the nm candidate
    closest to it (the three statics are contiguous within the TU).
    """
    anchor = None
    with open(elf_map, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\.bss\.s_total\s+0x([0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(\S+)$",
                         line.strip())
            if m and "sys_log.c.obj" in m.group(2):
                anchor = int(m.group(1), 16)
                break
    if anchor is None:
        return {}
    syms = {"s_total": anchor}
    for name in ("s_next_index", "s_entries"):
        cands = [a for a in nm_syms.get(name, [])]
        if not cands:
            continue
        syms[name] = min(cands, key=lambda a: abs(a - anchor))
    return syms


def run_jlink(script_lines):
    script = "\n".join(script_lines) + "\n"
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".tmp",
                        "probe_qr_selftest.jlink")
    path = os.path.normpath(path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii") as f:
        f.write(script)
    out = subprocess.run([JLINK, "-CommandFile", path, "-ExitOnError", "1"],
                         capture_output=True, text=True, timeout=120)
    return out.stdout + out.stderr


def parse_mem32_block(text, addr, count):
    """Parse `mem32 <addr>, <count>` output: the block between the echoed
    command and the next J-Link> prompt, as a list of count uint32 values.
    Only the RHS of each '=' is used (the leading address is 8 hex digits too)."""
    m = re.search(r"mem32 0x%08X, %d\s*\n(.*?)\nJ-Link>" % (addr, count), text, re.S)
    if not m:
        return None
    vals = []
    for line in m.group(1).splitlines():
        line = line.strip()
        if "=" not in line:
            continue
        rhs = line.split("=", 1)[1]
        vals.extend(int(t, 16) for t in re.findall(r"\b[0-9A-Fa-f]{8}\b", rhs))
    return vals[:count] if len(vals) >= count else None


def main():
    which = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    elf = sys.argv[2] if len(sys.argv) > 2 else "build/Debug/26renesascup_smartmedicine_Ra8p1.elf"
    elf_map = elf.rsplit(".", 1)[0] + ".map"

    syms = nm_symbols(elf, ["s_dbg_qr_selftest_trigger", "s_dbg_qr_decode_ms",
                            "s_dbg_qr_decode_count", "s_dbg_qr_selftest_stage",
                            "s_selftest_result",
                            "s_next_index", "s_entries"])
    slog = map_syslog_symbols(elf_map, syms)
    uni = {k: v[0] for k, v in syms.items() if len(v) == 1}
    print("syms:", {k: hex(v) for k, v in uni.items()})
    print("syslog:", {k: hex(v) for k, v in slog.items()})
    if "s_dbg_qr_selftest_trigger" not in uni:
        print("ERROR: symbol not found; is the self-test build flashed?")
        return 1

    result_addr = uni["s_selftest_result"]
    script = [
        f"device {DEVICE}",
        "si SWD",
        "speed 1000",
        "connect",
        "r",
        f"w4 0x{uni['s_dbg_qr_selftest_trigger']:08X}, {which}",
        "g",
        "sleep 6000",
        "halt",
        f"mem32 0x{uni['s_dbg_qr_selftest_trigger']:08X}, 1",
        f"mem32 0x{uni['s_dbg_qr_decode_count']:08X}, 1",
        f"mem32 0x{uni['s_dbg_qr_decode_ms']:08X}, 1",
        f"mem32 0x{uni['s_dbg_qr_selftest_stage']:08X}, 1",
        f"mem32 0x{result_addr:08X}, 35",
        f"mem32 0x{slog['s_total']:08X}, 1",
        f"mem32 0x{slog['s_next_index']:08X}, 1",
        f"mem8 0x{slog['s_entries']:08X}, {32 * 88}",
        "q",
    ]
    text = run_jlink(script)
    trig_b = parse_mem32_block(text, uni["s_dbg_qr_selftest_trigger"], 1)
    count_b = parse_mem32_block(text, uni["s_dbg_qr_decode_count"], 1)
    ms_b = parse_mem32_block(text, uni["s_dbg_qr_decode_ms"], 1)
    stage_b = parse_mem32_block(text, uni["s_dbg_qr_selftest_stage"], 1)
    words = parse_mem32_block(text, result_addr, 35)

    print("trigger now = %s (0 = consumed)" % (trig_b[0] if trig_b else None))
    print("decode_count = %s" % (count_b[0] if count_b else None))
    print("s_dbg_qr_decode_ms = %s ms" % (ms_b[0] if ms_b else None))
    print("selftest stage = %s (0=idle 1=init 2=render 3=decode 4=published)" %
          (stage_b[0] if stage_b else None))
    if words:
        w0 = words[0]
        done, w, status = w0 & 0xFF, (w0 >> 8) & 0xFF, (w0 >> 16) & 0xFF
        decode_ms = words[1]
        payload = b"".join(struct.pack("<I", x) for x in words[2:34]).split(b"\x00")[0]
        plen = words[34] & 0xFFFF
        print("selftest: done=%d which=%d status=%s decode_ms=%d plen=%d" %
              (done, w, STATUS.get(status, status), decode_ms, plen))
        try:
            print("payload: %s" % payload.decode("utf-8"))
        except Exception:
            print("payload: %s" % payload.decode("gbk", errors="replace"))
    else:
        print("selftest result: not parsed")

    # syslog tail
    total_b = parse_mem32_block(text, slog["s_total"], 1)
    nxt_b = parse_mem32_block(text, slog["s_next_index"], 1)
    total = total_b[0] if total_b else None
    nxt = nxt_b[0] if nxt_b else None
    slraw = None
    m = re.search(r"mem8 0x%08X, %d\s*\n(.*?)\nJ-Link>" % (slog["s_entries"], 32 * 88),
                  text, re.S)
    if m:
        rhs_hex = []
        for line in m.group(1).splitlines():
            line = line.strip()
            if "=" in line:
                rhs_hex.append(line.split("=", 1)[1])
        hexstr = "".join("".join(rhs_hex).split())
        if len(hexstr) >= 32 * 88 * 2:
            slraw = bytes.fromhex(hexstr)
    print("--- syslog total=%s next=%s ---" % (total, nxt))
    if slraw and total is not None and nxt is not None:
        for idx in range(min(14, total)):
            slot = (nxt + 32 - 1 - idx) % 32
            base = slot * 88
            tick, level = struct.unpack("<II", bytes(slraw[base:base + 8]))
            txt = bytes(slraw[base + 8:base + 8 + 80]).split(b"\x00")[0]
            lv = {0: "INFO", 1: "OK", 2: "WARN", 3: "ERR"}.get(level, "?")
            try:
                s = txt.decode("utf-8")
            except Exception:
                s = txt.decode("gbk", errors="replace")
            print("[%s] t=%6d %s" % (lv, tick, s))
    return 0


if __name__ == "__main__":
    sys.exit(main())
