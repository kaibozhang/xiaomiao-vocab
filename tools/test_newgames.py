# -*- coding: utf-8 -*-
"""新游戏（配对消消乐 + 字母重组）实机功能测试。单次串口连接完成全部流程。"""
import sys
import time
import re
import serial


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = False
    time.sleep(2.5)
    ser.reset_input_buffer()

    def send(cmd, wait=0.4):
        ser.write((cmd + "\n").encode())
        ser.flush()
        time.sleep(wait)
        out = b""
        end = time.time() + wait
        while time.time() < end:
            l = ser.readline()
            if l:
                out += l
        return out.decode("utf-8", "replace")

    def scr():
        r = send("p", 0.4)
        m = re.search(r"id=(\d+)", r)
        return int(m.group(1)) if m else -1

    results = []

    def chk(cond, name, detail=""):
        results.append((cond, name, detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name} {detail if not cond else ''}")

    print("=== 1. 进入配对消消乐（主菜单 sel=5）===")
    for _ in range(10):
        send("k1", 0.2)      # UP 回顶 sel=0
    for _ in range(5):
        send("k2", 0.3)      # DOWN x5
    send("k16", 0.6)         # A 进入
    s = scr()
    chk(s == 18, "进入配对消消乐 scr=18", f"scr={s}")

    print("=== 2. 配对操作（翻牌/移动/配对/取消）===")
    # 记录操作不崩溃即可
    send("k16", 0.4)         # A 选第1张
    send("k8", 0.4)          # 右移
    send("k16", 0.5)         # A 选第2张（配对判定）
    s = scr()
    chk(s in (18, 19), "配对一轮后停留正确屏", f"scr={s}")
    # 取消选择：A 选一张，再 A 同一张取消
    send("k16", 0.3)
    send("k16", 0.4)
    s = scr()
    chk(s == 18, "取消选择不崩溃", f"scr={s}")
    # 四方向移动
    for key in ("k1", "k2", "k4", "k8"):
        send(key, 0.25)
    s = scr()
    chk(s == 18, "四方向移动不崩溃", f"scr={s}")

    print("=== 3. 配对退出 ===")
    send("k32", 0.5)         # B
    s = scr()
    chk(s == 0, "B 退回主菜单", f"scr={s}")

    print("=== 4. 进入字母重组（主菜单 sel=6）===")
    send("k2", 0.3)
    send("k16", 0.6)
    s = scr()
    chk(s == 20, "进入字母重组 scr=20", f"scr={s}")

    print("=== 5. 字母重组操作 ===")
    # 填一个字母：A（当前光标 selIdx=0）
    send("k16", 0.4)
    # 退格没有（重组没有退格，填错只能填完判错），继续填满触发判定
    # 简化：填满 len 个字母（假设 len<=8）
    for _ in range(8):
        send("k8", 0.15)     # 右移找未用字母
        send("k16", 0.25)    # A 填入
    time.sleep(0.5)
    s = scr()
    chk(s in (20, 21), "字母重组填满判定不崩溃", f"scr={s}")
    # 再来一轮
    for _ in range(9):
        send("k8", 0.12)
        send("k16", 0.2)
    time.sleep(0.5)
    s = scr()
    chk(s in (20, 21), "重组多轮不崩溃", f"scr={s}")

    print("=== 6. 重组退出 ===")
    send("k32", 0.5)
    s = scr()
    chk(s == 0, "B 退回主菜单", f"scr={s}")

    print("=== 7. 主菜单滚动到新项显示 ===")
    for _ in range(9):
        send("k2", 0.15)
    s = scr()
    chk(s == 0, "主菜单滚动到底不崩溃", f"scr={s}")

    ser.close()

    npass = sum(1 for c, _, _ in results if c)
    print(f"\n========== 新游戏测试：通过 {npass} 失败 {len(results) - npass} ==========")
    return 0 if npass == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
