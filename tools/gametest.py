# -*- coding: utf-8 -*-
"""
闯关连错 bug 复现测试。
直接控制串口：注入按键 → 读取 gameDump 输出，观察答题推进与"连错"现象。

用法:
    python tools/gametest.py COM3
"""
import sys
import time
import serial


def read_until(ser, timeout, markers):
    """读取直到出现某个 marker 或超时。返回 (all_text, found_marker)。"""
    end = time.time() + timeout
    buf = b""
    found = None
    while time.time() < end:
        b = ser.read(1)
        if b:
            buf += b
            for m in markers:
                if m in buf.decode("utf-8", "replace"):
                    found = m
                    break
            if found:
                break
    return buf.decode("utf-8", "replace"), found


def send(ser, s, wait=0.25):
    ser.write((s + "\n").encode())
    ser.flush()
    time.sleep(wait)


def dump(ser):
    send(ser, "g", 0.3)
    end = time.time() + 1.5
    while time.time() < end:
        line = ser.readline().decode("utf-8", "replace").strip()
        if "[game]" in line:
            return line
    return None


def key(ser, mask, wait=0.3):
    send(ser, "k%d" % mask, wait)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = False
    time.sleep(2.5)
    ser.reset_input_buffer()

    # 回主菜单
    send(ser, "k32", 0.5)          # B
    send(ser, "k32", 0.5)          # B (确保回主菜单)

    # 主菜单 sel=0，DOWN x4 到闯关(sel=4)
    for _ in range(4):
        key(ser, 2)
    print("进选关前 dump:", dump(ser))

    key(ser, 16, 0.6)              # A 进入选关
    print("选关 dump:", dump(ser))

    key(ser, 16, 0.6)              # A 进入第1关
    d0 = dump(ser)
    print("第1关初始:", d0)

    # 连续按 A 答 10 题，每次都记录状态
    for i in range(10):
        d_before = dump(ser)
        key(ser, 16, 0.8)          # A 选择（默认第一个选项）
        # 等待反馈推进
        time.sleep(0.8)
        d_after = dump(ser)
        print(f"题{i+1}: 前[{d_before}] 后[{d_after}]")

    ser.close()


if __name__ == "__main__":
    main()
