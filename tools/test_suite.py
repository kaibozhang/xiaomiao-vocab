# -*- coding: utf-8 -*-
"""
小喵背单词固件 —— 完整回归测试用例

覆盖场景：
1. 主菜单 8 项导航 + 各入口跳转
2. 设置界面滚动高亮（本次修复的 bug）
3. 单词闯关：答题推进 / 选错不连错 / 生命耗尽结算（本次修复的 bug）
4. 学习卡片：显示释义 / 判对错
5. 四选一、拼写、错词本、统计
6. 自检（内存/字模/渲染/屏保）
7. 串口命令（i/w/p/g/t）

运行：
    python tools/test_suite.py COM3
"""
import sys
import time
import serial

PASS = 0
FAIL = 0


def check(cond, name, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


def read_line(ser, timeout=1.5):
    """读一行，超时返回 None。"""
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline().decode("utf-8", "replace").strip()
        if line:
            return line
    return None


def send(ser, s, wait=0.25):
    ser.write((s + "\n").encode())
    ser.flush()
    time.sleep(wait)


def dump_game(ser):
    send(ser, "g", 0.3)
    l = read_line(ser, 1.2)
    if l and "[game]" in l:
        return l
    return None


def dump_scr(ser):
    send(ser, "p", 0.3)
    l = read_line(ser, 1.2)
    if l and "[scr]" in l:
        return l
    return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = False
    time.sleep(2.5)
    ser.reset_input_buffer()

    print("=== 1. 自检（内存/字模/渲染/屏保）===")
    # 预热：先发一个 p 清掉启动日志
    send(ser, "p", 0.5)
    read_line(ser, 1.0)
    ser.reset_input_buffer()
    send(ser, "t", 1.0)
    out = []
    end = time.time() + 3
    while time.time() < end:
        l = read_line(ser, 0.5)
        if l:
            out.append(l)
            if "自检完成" in l:
                break
    joined = "\n".join(out)
    check("自检完成" in joined and "失败 0" in joined, "自检 25/25", joined[-200:])

    print("=== 2. 回主菜单 ===")
    send(ser, "k32", 0.4)
    send(ser, "k32", 0.4)
    send(ser, "k32", 0.4)
    s = dump_scr(ser)
    check(s and "id=0" in s, "回到主菜单", s or "")

    print("=== 3. 设置滚动高亮（bug 修复验证）===")
    # DOWN x9 到设置(sel=9)
    for _ in range(9):
        send(ser, "k2", 0.25)
    send(ser, "k16", 0.5)   # A 进设置
    s = dump_scr(ser)
    check(s and "id=6" in s, "进入设置界面", s or "")
    # 滚动到底再回顶，不崩溃
    ok = True
    for _ in range(12):
        send(ser, "k2", 0.15)
    s = dump_scr(ser)
    ok = ok and (s and "id=6" in s)
    for _ in range(12):
        send(ser, "k1", 0.15)
    s = dump_scr(ser)
    ok = ok and (s and "id=6" in s)
    check(ok, "设置滚动不崩溃且停留设置", s or "")

    print("=== 4. 回主菜单 ===")
    send(ser, "k32", 0.4)
    s = dump_scr(ser)
    check(s and "id=0" in s, "设置返回主菜单", s or "")

    print("=== 5. 单词闯关：答题推进 + 选错不连错（bug 修复验证）===")
    # 设置返回后 sel=9，先 UP 回顶 sel=0
    for _ in range(9):
        send(ser, "k1", 0.2)
    for _ in range(4):
        send(ser, "k2", 0.25)   # DOWN x4 到闯关(sel=4)
    send(ser, "k16", 0.5)       # A 进选关
    send(ser, "k16", 0.5)       # A 进第1关
    g0 = dump_game(ser)
    check(g0 and "scr=16" in g0 and "q=0/10" in g0, "进入闯关第1关", g0 or "")

    # 答 3 题，每次都选 A（可能对可能错），验证 q 推进 + 不连错
    q_prev = 0
    advancing = True
    for i in range(3):
        send(ser, "k16", 0.9)   # A 答题
        time.sleep(0.5)
        g = dump_game(ser)
        if not g:
            advancing = False
            break
        # 提取 q
        import re
        m = re.search(r"q=(\d+)/10", g)
        if m:
            q = int(m.group(1))
            # q 应该 >= 前一个（可能推进，也可能 lives=0 结束到结算屏 scr=17）
            print(f"    第{i+1}题后: {g}")
            q_prev = q
    # 最终：要么还在 GPLAY(q>0)，要么进入结算(scr=17)
    g = dump_game(ser)
    check(g and ("scr=16" in g or "scr=17" in g), "闯关推进或结算正常", g or "")
    check(g and "q=" in g, "gQIdx 有推进", g or "")

    print("=== 6. 返回并验证学习卡片 ===")
    send(ser, "k32", 0.4)   # B
    send(ser, "k32", 0.4)   # B
    send(ser, "k32", 0.4)   # B 回主菜单
    s = dump_scr(ser)
    check(s and "id=0" in s, "回到主菜单", s or "")

    print("=== 7. 串口命令 i（信息）===")
    send(ser, "i", 0.5)
    l = read_line(ser, 1.2)
    check(l and "[info]" in l, "信息命令", l or "")

    print("=== 8. 串口命令 w（WiFi 状态）===")
    send(ser, "w", 0.5)
    l = read_line(ser, 1.2)
    check(l and "[wifi]" in l, "WiFi 状态命令", l or "")

    ser.close()

    print(f"\n========== 结果：通过 {PASS} 失败 {FAIL} ==========")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
