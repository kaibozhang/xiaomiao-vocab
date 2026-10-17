# -*- coding: utf-8 -*-
"""
串口自检：打开设备串口，持续排空并打印输出，按顺序发送命令。

用法:
    python tools/serialtest.py                       # COM3，跑一轮标准自检
    python tools/serialtest.py COM5
    python tools/serialtest.py COM3 --script=t,20,e,5,i
    python tools/serialtest.py COM3 --script=t:20,w:3    # cmd:持续秒数

注意：打开串口后会把 DTR/RTS 置低，避免 ESP32 被反复复位，保证命令/回显一一对应。
"""
import sys
import time

import serial


def drain(ser, seconds, stop_evt=None):
    """在 seconds 秒内持续读取并打印。"""
    end = time.time() + seconds
    while time.time() < end:
        line = ser.readline()
        if line:
            text = line.decode("utf-8", "replace").rstrip()
            if text:
                print("  <", text, flush=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opt = {}
    for a in sys.argv[1:]:
        if a.startswith("--script="):
            opt["script"] = a.split("=", 1)[1]
        elif a.startswith("--settle="):
            opt["settle"] = float(a.split("=", 1)[1])

    port = args[0] if args else "COM3"
    settle = opt.get("settle", 2.5)
    script = opt.get("script", "t:20,e:6,i:3,w:2")

    steps = []
    for s in script.split(","):
        s = s.strip()
        if not s:
            continue
        if ":" in s:
            cmd, dur = s.split(":", 1)
            steps.append((cmd, float(dur)))
        elif s.replace(".", "", 1).isdigit():
            steps.append((None, float(s)))
        else:
            steps.append((s, 2.5))

    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = False
    try:
        ser.reset_input_buffer()
        print("== %s 已打开，先静默监听 %.1fs（打印稳定后的输出）==" % (port, settle), flush=True)
        drain(ser, settle)
        for cmd, dur in steps:
            if cmd is None:
                print("== 等待 %.1fs ==" % dur, flush=True)
                drain(ser, dur)
                continue
            print("== 发送 '%s'，等待 %.1fs ==" % (cmd, dur), flush=True)
            ser.write((cmd + "\n").encode())
            ser.flush()
            drain(ser, dur)
        drain(ser, 0.6)
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
