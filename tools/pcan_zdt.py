#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PCAN-USB <-> ZDT 电机驱动 调试工具
=====================================
通过 PEAK PCANBasic.dll 直接操作 PCAN-USB，向 ZDT 电机驱动收发 CAN 帧。

用法:
  python pcan_zdt.py listen [秒]            # 只监听总线（默认 5 秒）
  python pcan_zdt.py send <id> <hex...>     # 发一帧(扩展帧)，然后监听 3 秒
      例: python pcan_zdt.py send 0x100 F3 AB 01 00 6B
  python pcan_zdt.py enable <x|y|z|catch|all> [on|off]   # 使能/脱机
      例: python pcan_zdt.py enable x on     # X 轴使能 -> 0x0100 F3 AB 01 00 6B
  python pcan_zdt.py disable <axis>          # 等同 enable <axis> off
  python pcan_zdt.py status                  # 查询通道状态

波特率默认 500 kbit/s（与 RA8P1 CANFD0 标称一致），可用 --baud 覆盖:
  python pcan_zdt.py --baud 250k listen 5

ZDT 协议速记:
  ID: X=0x0100 Y=0x0200 Z=0x0300 CATCH=0x0400 ALL=0x0000 (29位扩展帧)
  使能: 0xF3 0xAB [01使能/00脱机] 0x00 0x6B   (DLC=5)
  回零: 0x9A 0x02 [sync] 0x6B                  (DLC=4)
  急停: 0xFE 0x98 0x00 0x6B                    (DLC=4)
  同步: 0xFF 0x66 0x6B / 0xFF 0x02 0x6B        (ID=0x0000, DLC=3)
  位置: 0xFD + dir + spd(2) + acc + pos(4) + abs + sync + 0x6B (12字节, 拆两包)
"""

import ctypes
import sys
import time

# ---------------- PCANBasic 常量 ----------------
PCAN_USBBUS1 = 0x51
PCAN_USBBUS2 = 0x52
PCAN_USBBUS3 = 0x53
PCAN_USBBUS4 = 0x54
PCAN_USBBUS5 = 0x55
PCAN_USBBUS6 = 0x56
PCAN_USBBUS7 = 0x57
PCAN_USBBUS8 = 0x58

BAUDS = {
    "1m": 0x00000100, "800k": 0x00000200, "500k": 0x00000400,
    "250k": 0x00000800, "125k": 0x00001000, "100k": 0x00002000,
    "50k": 0x00004000, "20k": 0x00008000, "10k": 0x00010000,
    "5k": 0x00020000,
}

PCAN_MESSAGE_STANDARD = 0x00
PCAN_MESSAGE_EXTENDED = 0x80
PCAN_MESSAGE_RTR = 0x40

PCAN_ERROR_OK = 0x0000
PCAN_ERROR_XMTFULL = 0x0001
PCAN_ERROR_OVERRUN = 0x0002
PCAN_ERROR_BUSLIGHT = 0x0004
PCAN_ERROR_BUSHEAVY = 0x0008
PCAN_ERROR_BUSOFF = 0x0010
PCAN_ERROR_QRCVEMPTY = 0x0020   # 无数据可读（正常）
PCAN_ERROR_ILLPARAMVAL = 0x0080
PCAN_ERROR_ILLHANDLE = 0x0100
PCAN_ERROR_ILLDEVICE = 0x0200
PCAN_ERROR_ILLNETMODE = 0x0400
PCAN_ERROR_ILLNETTYPE = 0x0800
PCAN_ERROR_ILLNETINT = 0x1000
PCAN_ERROR_ILLHWTYPE = 0x2000
PCAN_ERROR_ILLHWVERSION = 0x4000
PCAN_ERROR_ILLHANDLEID = 0x8000
PCAN_ERROR_ILLINTERRUPT = 0x10000
PCAN_ERROR_ILLPARAMTYPE = 0x20000
PCAN_ERROR_ILLPARAMVALUE = 0x40000
PCAN_ERROR_ILLRXQUEUE = 0x80000
PCAN_ERROR_ILLTXQUEUE = 0x100000
PCAN_ERROR_ILLHWMODE = 0x200000
PCAN_ERROR_RXQUEUEEMPTY = 0x400000
PCAN_ERROR_ILLBAUD = 0x800000
PCAN_ERROR_INITIALIZE = 0x1000000
PCAN_ERROR_ILLOPERATION = 0x2000000
PCAN_ERROR_INITIALIZEAUX = 0x4000000

ERROR_NAMES = {v: k for k, v in list(globals().items()) if k.startswith("PCAN_ERROR_")}


class TPCANMsg(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint32),
        ("MSGTYPE", ctypes.c_uint16),   # TPCANMessageType = WORD
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


class TPCANTimestamp(ctypes.Structure):
    _fields_ = [
        ("millis", ctypes.c_uint32),
        ("millis_overflow", ctypes.c_uint16),
        ("micros", ctypes.c_uint16),
    ]


def load_dll():
    candidates = [
        r"C:\WINDOWS\System32\PCANBasic.dll",
        "PCANBasic.dll",
    ]
    last_err = None
    for c in candidates:
        try:
            dll = ctypes.WinDLL(c)
            return dll
        except OSError as e:
            last_err = e
    raise RuntimeError(f"无法加载 PCANBasic.dll: {last_err}")


def err_text(dll, status):
    buf = ctypes.create_string_buffer(128)
    dll.CAN_GetErrorText(ctypes.c_uint32(status), ctypes.c_uint16(0x0000), buf)  # 0 = English
    return buf.value.decode("ascii", "replace")


class PCAN:
    def __init__(self, channel=PCAN_USBBUS1, baud="500k"):
        self.dll = load_dll()
        self.channel = channel
        self.baud = baud
        # argtypes / restype
        self.dll.CAN_Initialize.restype = ctypes.c_uint32
        self.dll.CAN_Initialize.argtypes = [
            ctypes.c_uint16, ctypes.c_uint16, ctypes.c_ubyte,
            ctypes.c_uint32, ctypes.c_uint16,
        ]
        self.dll.CAN_Uninitialize.restype = ctypes.c_uint32
        self.dll.CAN_Uninitialize.argtypes = [ctypes.c_uint16]
        self.dll.CAN_Write.restype = ctypes.c_uint32
        self.dll.CAN_Write.argtypes = [ctypes.c_uint16, ctypes.POINTER(TPCANMsg)]
        self.dll.CAN_Read.restype = ctypes.c_uint32
        self.dll.CAN_Read.argtypes = [ctypes.c_uint16, ctypes.POINTER(TPCANMsg),
                                      ctypes.POINTER(TPCANTimestamp)]
        self.dll.CAN_GetStatus.restype = ctypes.c_uint32
        self.dll.CAN_GetStatus.argtypes = [ctypes.c_uint16]

    def init(self):
        st = self.dll.CAN_Initialize(
            ctypes.c_uint16(self.channel),
            ctypes.c_uint16(BAUDS[self.baud]),
            ctypes.c_ubyte(0),  # HwType 0=auto
            ctypes.c_uint32(0), ctypes.c_uint16(0),
        )
        if st != PCAN_ERROR_OK:
            raise RuntimeError(
                f"CAN_Initialize 失败 status=0x{st:08X} ({ERROR_NAMES.get(st, '?')}) "
                f": {err_text(self.dll, st)}")
        print(f"[OK] PCAN 通道 0x{self.channel:02X} 初始化成功, 波特率 {self.baud}")
        return st

    def uninit(self):
        self.dll.CAN_Uninitialize(ctypes.c_uint16(self.channel))

    def get_value(self, param):
        """读取 PCAN 参数 (PCAN_BUSLOAD=0x05, PCAN_CHANNEL_CONDITION=0x04)"""
        if not hasattr(self.dll, "CAN_GetValue"):
            return None
        buf = ctypes.c_uint32(0)
        st = self.dll.CAN_GetValue(
            ctypes.c_uint16(self.channel), ctypes.c_uint32(param),
            ctypes.byref(buf), ctypes.c_uint32(4))
        if st != PCAN_ERROR_OK:
            return None
        return buf.value

    def busload(self):
        return self.get_value(0x05)

    def channel_condition(self):
        return self.get_value(0x04)

    def send(self, msg_id, data, extended=True):
        m = TPCANMsg()
        m.ID = msg_id & 0x1FFFFFFF
        m.MSGTYPE = PCAN_MESSAGE_EXTENDED if extended else PCAN_MESSAGE_STANDARD
        m.LEN = len(data)
        for i, b in enumerate(data[:8]):
            m.DATA[i] = b
        st = self.dll.CAN_Write(ctypes.c_uint16(self.channel), ctypes.byref(m))
        if st != PCAN_ERROR_OK:
            raise RuntimeError(
                f"CAN_Write 失败 status=0x{st:08X} ({ERROR_NAMES.get(st, '?')}) "
                f": {err_text(self.dll, st)}")
        tag = "EXT" if extended else "STD"
        hexs = " ".join(f"{b:02X}" for b in data)
        print(f"[TX] {tag} ID=0x{m.ID:08X} DLC={m.LEN}  {hexs}")
        return st

    def read_one(self):
        m = TPCANMsg()
        ts = TPCANTimestamp()
        st = self.dll.CAN_Read(ctypes.c_uint16(self.channel), ctypes.byref(m),
                               ctypes.byref(ts))
        if st == PCAN_ERROR_QRCVEMPTY or st == PCAN_ERROR_RXQUEUEEMPTY:
            return None
        if st != PCAN_ERROR_OK:
            return ("ERR", st)
        tag = "EXT" if (m.MSGTYPE & PCAN_MESSAGE_EXTENDED) else "STD"
        hexs = " ".join(f"{m.DATA[i]:02X}" for i in range(m.LEN))
        t_ms = ts.millis + ts.millis_overflow * 4294967296 + ts.micros / 1000.0
        return (m.ID, tag, m.LEN, hexs, t_ms)

    def listen(self, seconds, stop_on=None):
        """stop_on: 收到该 ID 后继续收 0.5s 即停（用于等使能应答）"""
        print(f"[..] 监听总线 {seconds}s (Ctrl+C 提前结束) ...")
        deadline = time.time() + seconds
        hit_stop = time.time() + 10**9
        count = 0
        while time.time() < deadline:
            r = self.read_one()
            if r is None:
                time.sleep(0.005)
                continue
            if r[0] == "ERR":
                print(f"[RX-ERR] status=0x{r[1]:08X}")
                continue
            mid, tag, ln, hexs, t = r
            print(f"[RX] {tag} ID=0x{mid:08X} DLC={ln}  {hexs}   @{t/1000:.3f}s")
            count += 1
            if stop_on is not None and mid == stop_on:
                hit_stop = time.time()
            if stop_on is not None and time.time() - hit_stop > 0.5:
                break
        print(f"[..] 共收到 {count} 帧")


AXES = {"x": 0x0100, "y": 0x0200, "z": 0x0300, "catch": 0x0400, "all": 0x0000}


def cmd_move(p, axis, pos, speed, acc, sync, post):
    """位置模式移动 (0xFD)，严格按工程 ZDT_MovePosition 拆包:
       帧1 ID=id  DLC=8: FD dir spdH spdL acc pos3 pos2 pos1
       帧2 ID=id+1 DLC=5: FD pos0 abs sync 6B
    """
    if axis not in AXES or axis == "all":
        print(f"轴 {axis} 不支持移动, 可选: x/y/z/catch")
        sys.exit(2)
    mid = AXES[axis]
    dir_byte = 0x00 if pos >= 0 else 0x01
    apos = abs(pos)
    payload = [0xFD, dir_byte,
               (speed >> 8) & 0xFF, speed & 0xFF,
               acc & 0xFF,
               (apos >> 24) & 0xFF, (apos >> 16) & 0xFF,
               (apos >> 8) & 0xFF, apos & 0xFF,
               0x01, 0x01 if sync else 0x00, 0x6B]
    if post > 0:
        print(f"[..] 发送前先监听 {post}s ...")
        p.listen(post)
    p.send(mid, payload[0:8])
    time.sleep(0.002)
    p.send(mid + 1, [payload[0]] + payload[8:12])
    print(f"[..] 已发送 {axis.upper()} 轴位置命令: pos={pos:+d} speed={speed} acc={acc} (绝对)")
    p.listen(post)


def cmd_enable(p, axis, on, pre, post):
    if axis not in AXES:
        print(f"未知轴 {axis}, 可选: {list(AXES)}")
        sys.exit(2)
    mid = AXES[axis]
    data = [0xF3, 0xAB, 0x01 if on else 0x00, 0x00, 0x6B]
    if pre > 0:
        print(f"[..] 发送前先监听 {pre}s ...")
        p.listen(pre)
    p.send(mid, data)
    p.listen(post)


def main():
    args = sys.argv[1:]
    baud = "500k"
    while args and args[0].startswith("--baud"):
        baud = args.pop(1) if len(args) > 1 else "500k"
    if baud not in BAUDS:
        print(f"未知波特率 {baud}, 可选: {list(BAUDS)}")
        sys.exit(2)
    # 通用参数: --pre <s> --post <s>
    pre = 0.0
    post = 8.0
    rest = []
    i = 0
    while i < len(args):
        if args[i] == "--pre" and i + 1 < len(args):
            pre = float(args[i + 1]); i += 2
        elif args[i] == "--post" and i + 1 < len(args):
            post = float(args[i + 1]); i += 2
        else:
            rest.append(args[i]); i += 1
    args = rest
    if not args:
        print(__doc__)
        sys.exit(1)
    cmd = args[0]

    p = PCAN(PCAN_USBBUS1, baud)
    try:
        p.init()
        bl = p.busload()
        if bl is not None:
            print(f"[..] 当前总线负载 BUSLOAD = {bl}%")
        if cmd == "status":
            st = p.dll.CAN_GetStatus(ctypes.c_uint16(p.channel))
            print(f"CAN_GetStatus = 0x{st:08X} ({ERROR_NAMES.get(st, '?')})")
            cc = p.channel_condition()
            if cc is not None:
                print(f"通道状态 CHANNEL_CONDITION = {cc} (0=空闲 1=占用 2=PCAN-View)")
        elif cmd == "listen":
            secs = float(args[1]) if len(args) > 1 else 5.0
            p.listen(secs)
        elif cmd == "send":
            if len(args) < 3:
                print("用法: send <id> <hex...>")
                sys.exit(2)
            mid = int(args[1], 0)
            data = [int(x, 16) for x in args[2:]]
            if pre > 0:
                print(f"[..] 发送前先监听 {pre}s ...")
                p.listen(pre)
            p.send(mid, data)
            p.listen(post)
        elif cmd == "enable":
            axis = args[1] if len(args) > 1 else "x"
            on = (args[2] if len(args) > 2 else "on").lower() in ("on", "1", "true")
            cmd_enable(p, axis, on, pre, post)
        elif cmd == "disable":
            axis = args[1] if len(args) > 1 else "x"
            cmd_enable(p, axis, False, pre, post)
        elif cmd == "move":
            # move <x|y|z|catch> <pos> [speed] [acc]
            if len(args) < 3:
                print("用法: move <x|y|z|catch> <pos脉冲> [speed] [acc]")
                sys.exit(2)
            axis = args[1]
            pos = int(args[2], 0)
            speed = int(args[3], 0) if len(args) > 3 else 50
            acc = int(args[4], 0) if len(args) > 4 else 5
            cmd_move(p, axis, pos, speed, acc, False, post)
        elif cmd == "stop":
            # 急停: 0xFE 0x98 0x00 0x6B (对指定轴或广播)
            axis = args[1] if len(args) > 1 else "all"
            mid = AXES.get(axis, AXES["all"])
            p.send(mid, [0xFE, 0x98, 0x00, 0x6B])
            p.listen(1)
        else:
            print(f"未知命令 {cmd}")
            print(__doc__)
            sys.exit(2)
    finally:
        p.uninit()
        print("[..] 通道已释放")


if __name__ == "__main__":
    main()
