# -*- coding: utf-8 -*-
"""裸分区存储验证探针：
1. 设备在线与基本信息
2. 真实答题 5 词（k16 显示释义 → k4 忘了），使 5 个词变脏
3. 发 s 后立即轮询 p，测真实保存耗时（期望单扇区 ~60-150ms，旧实现整文件 ~1080ms）
4. 重启后确认进度仍在（new 应下降 5）
"""
import sys
import time

sys.path.insert(0, "tools")
from test_full import Dev, BTN_A, SCR  # noqa: E402


def timed_save(dev, budget=10.0):
    """发 s 后立刻发 p，测设备重新可响应前的阻塞时长（≈保存耗时）。"""
    dev.ser.write(b"\ns\n")
    dev.ser.flush()
    time.sleep(0.03)
    t0 = time.time()
    dev.ser.write(b"\np\n")
    dev.ser.flush()
    end = t0 + budget
    while time.time() < end:
        line = dev.ser.readline().decode("utf-8", "replace")
        if "id=" in line:
            return (time.time() - t0) * 1000
    return None


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    dev = Dev(port)

    print("== 设备信息 ==")
    info = dev.info()
    print(" ", info)
    if not info:
        print("!! 设备无响应")
        return 1
    print(" ", dev.cfg())

    for _ in range(3):
        if dev.cmd("m0"):
            break
        time.sleep(0.2)

    print("== 进入四选一测验并答 4 题 ==")
    sid = dev.menu(2)
    print("  menu(2) -> screen id=%s (期望 %d)" % (sid, SCR["QUIZ"]))
    if sid != SCR["QUIZ"]:
        print("!! 没能进入测验界面")
        return 1

    for i in range(4):
        dev.key(BTN_A, 0.2)     # 提交当前选项（对错皆可，只要落脏标记）
        time.sleep(0.6)         # 等反馈动画推进
        s = dev.screen_id()
        print("  第 %d 题答完 -> screen=%s" % (i + 1, s))
        if s not in (SCR["QUIZ"], SCR["SUMMARY"]):
            print("  !! 状态异常，中止")
            break
        if s == SCR["SUMMARY"]:
            break

    info2 = dev.info()
    print("  答完 5 词 info: %s" % info2)

    print("== 增量保存计时（3 次） ==")
    for i in range(3):
        dt = timed_save(dev)
        print("  第 %d 次保存阻塞: %s ms" % (i + 1, ("%.0f" % dt) if dt else "超时"))
        time.sleep(0.3)

    print("== 重启验证持久性 ==")
    dev.cmd("R")
    time.sleep(2.5)
    dev.close()

    dev2 = Dev(port)
    info3 = dev2.info()
    print("  重启后 info: %s" % info3)
    print("== 探针完成 ==")
    dev2.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
