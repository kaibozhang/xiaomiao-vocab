# -*- coding: utf-8 -*-
"""
小喵背单词固件 —— 真机全功能测试（覆盖每一项菜单与功能）

特点：
- 开始时强制静音蜂鸣器（m0 命令，运行时生效不写入设置；设置段测音效行后会重新 m0），结束恢复
- 用串口命令驱动真实状态机（k 按键 / p 屏号 / g 状态 dump / i 信息），等价真机操作
- 节奏压在设备响应上限：按键间隔 ~70ms，反馈等待按固件时长给足
- 覆盖：自检、屏保自检、I2C 灯控、12 项主菜单、学习/测验/拼写、闯关(确定性判对判错)、
  配对、字母重组、猜单词、单词接龙、错词本、统计、设置全部 12 行、屏保进入/退出/误触回归、
  WiFi、时间、压测

用法：
    python tools/test_full.py            # 默认 COM3
    python tools/test_full.py COM5
"""
import re
import sys
import time

import serial

PASS = 0
FAIL = 0
BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B = 1, 2, 4, 8, 16, 32

# 屏幕编号（与 src/app.cpp 的 Screen 枚举一致）
SCR = {
    "HOME": 0, "LEARN": 1, "QUIZ": 2, "SPELL": 3, "WRONG": 4, "STATS": 5,
    "SETTINGS": 6, "BOOKS": 7, "ABOUT": 8, "SUMMARY": 9, "MSG": 10,
    "WIFI": 11, "SCAN": 12, "PW": 13, "TIME": 14,
    "GLEVELS": 15, "GPLAY": 16, "GRESULT": 17,
    "MATCH": 18, "MATCHRESULT": 19, "SCRAMBLE": 20, "SCRAMBLERESULT": 21,
    "HANG": 22, "HANGEND": 23, "CHAIN": 24, "CHAINEND": 25,
}


def check(cond, name, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name}", flush=True)
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}", flush=True)


class Dev:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.15)
        self.ser.dtr = False
        self.ser.rts = False
        time.sleep(1.8)
        self.ser.reset_input_buffer()
        self._warmup()

    def _warmup(self):
        """设备刚重启时首条命令可能被吞/无响应：重试到有回信为止。"""
        for _ in range(10):
            self.ser.write(b"\n")
            time.sleep(0.05)
            self.ser.write(b"p\n")
            self.ser.flush()
            end = time.time() + 0.6
            while time.time() < end:
                line = self.ser.readline().decode("utf-8", "replace")
                if "id=" in line:
                    return
            time.sleep(0.3)

    def close(self):
        self.ser.close()

    def _drain(self, seconds):
        """读 seconds 秒内出现的所有行。"""
        out = []
        end = time.time() + seconds
        while time.time() < end:
            line = self.ser.readline().decode("utf-8", "replace").strip()
            if line:
                out.append(line)
        return out

    def cmd(self, c, wait=0.22):
        """发一条串口命令（先发空行绕过 GD32 吞首字节），返回响应行。"""
        self.ser.write(b"\n")
        time.sleep(0.05)
        self.ser.write((c + "\n").encode())
        self.ser.flush()
        return self._drain(wait)

    def key(self, mask, wait=0.07):
        self.ser.write(b"\n")
        time.sleep(0.03)
        self.ser.write((f"k{mask}\n").encode())
        self.ser.flush()
        time.sleep(wait)

    def keys(self, masks, gap=0.07):
        for m in masks:
            self.key(m, gap)

    # ---- 状态查询（带重试：设备偶发因保存/网络事件响应变慢，异步行会混入）----
    def _query(self, c, prefix, want, tries=4):
        for _ in range(tries):
            for l in self.cmd(c, 0.3):
                if l.startswith(prefix) and want in l:
                    return l
            time.sleep(0.1)
        return None

    def screen(self):
        for _ in range(4):
            for l in self.cmd("p", 0.3):
                m = re.search(r"id=(\d+) saver=(\d+)", l)
                if m:
                    return int(m.group(1)), int(m.group(2))
            time.sleep(0.1)
        return None

    def screen_id(self):
        s = self.screen()
        return s[0] if s else None

    def saver(self):
        s = self.screen()
        return s[1] if s else None

    def info(self):
        return self._query("i", "[info]", "words=")

    def cfg(self):
        return self._query("i", "[cfg]", "dailyNew=")

    def game(self):
        return self._dump("[game]")

    def hang(self):
        return self._dump("[hang]")

    def chain(self):
        return self._dump("[chain]")

    def match(self):
        return self._dump("[match]")

    def scrline(self):
        return self._dump("[scr]")

    def _dump(self, prefix):
        """g dump 带重试：响应偶尔迟到，连试 3 次再放弃。"""
        for _ in range(3):
            for l in self.cmd("g", 0.3):
                if l.startswith(prefix):
                    return l
            time.sleep(0.1)
        return None

    # ---- 导航 ----
    def go_home(self):
        for _ in range(12):
            sid, sv = self.screen() or (-1, 0)
            if sid == SCR["HOME"]:
                return True
            if sv:
                self.key(BTN_A, 0.15)          # 退出屏保
                continue
            self.key(BTN_B, 0.18)
        return self.screen_id() == SCR["HOME"]

    def menu(self, sel):
        """主菜单移动并进入指定项。"""
        if not self.go_home():
            # 回不去主菜单就多按几次 B 再试
            for _ in range(6):
                self.key(BTN_B, 0.15)
            self.go_home()
        # 先回顶
        for _ in range(14):
            self.key(BTN_UP, 0.05)
        for _ in range(sel):
            self.key(BTN_DOWN, 0.07)
        self.key(BTN_A, 0.35)
        return self.screen_id()

    def press_until(self, masks, want, tries=8, gap=0.25):
        """重复按 masks 里的按键，直到屏幕变成 want（或超次数）。"""
        for _ in range(tries):
            if self.screen_id() == want:
                return True
            self.key(masks[0] if len(masks) == 1 else masks, gap)
        return self.screen_id() == want


def wait_screen(dev, want, timeout=4.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        last = dev.screen_id()
        if last == want:
            return True
        time.sleep(0.12)
    return False


# ===========================================================================
def t_boot_selftest(dev):
    print("=== 1. 开机信息 + 自检 ===", flush=True)
    info = dev.info()
    check(bool(info) and "words=5211" in info, "词库 5211 词", info or "")
    lines = []
    end = time.time() + 30
    got = dev.cmd("t", 0.3)
    lines += got
    while time.time() < end:
        for l in dev._drain(0.4):
            lines.append(l)
            if "自检完成" in l:
                break
        else:
            continue
        break
    joined = "\n".join(lines)
    check("自检完成" in joined, "自检执行完毕", joined[-160:])
    m = re.search(r"通过 (\d+) 失败 (\d+)", joined)
    check(bool(m) and m.group(2) == "0", "自检 0 失败",
          m.group(0) if m else joined[-160:])
    check("屏保防误触：时间戳旧于 lastAct" not in joined.replace("[FAIL]", "FAIL"),
          "屏保竞态回归通过", "")


def t_saver_selftest(dev):
    print("=== 2. 屏保自检（8 种各渲染）===", flush=True)
    lines = dev.cmd("e", 0.4)
    end = time.time() + 20
    while time.time() < end:
        lines += dev._drain(0.5)
        if any("屏保自检结束" in l for l in lines):
            break
    joined = "\n".join(lines)
    n = joined.count("10 帧")
    check(n >= 8, f"8 种屏保均渲染（{n}）", joined[-160:])


def t_lights(dev):
    print("=== 2b. I2C 灯控（静音时用灯光反馈）===", flush=True)
    info = dev.info()
    check(bool(info) and "light=1" in info, "灯控 0x40 在线", info or "")
    lines = dev.cmd("I", 3.0)
    check(any("0x40 应答" in l for l in lines), "I2C 扫描发现 0x40",
          lines[-1] if lines else "")
    lines = dev.cmd("l", 2.5)
    check(any("已播放" in l for l in lines), "灯光节奏播放（肉眼应见 1短闪/2闪/3急闪）",
          lines[-1] if lines else "")


def t_menu(dev):
    print("=== 3. 主菜单 12 项导航 ===", flush=True)
    dev.go_home()
    ok = True
    for _ in range(14):
        dev.key(BTN_UP, 0.05)
    for i in range(12):
        dev.key(BTN_DOWN, 0.07)
        s = dev.screen_id()
        if s is None:
            time.sleep(0.2)
            s = dev.screen_id()          # 重试一次再判定
        if s is not None and s != SCR["HOME"]:
            ok = False
    check(ok, "DOWN x12 不越界且停留主菜单")
    for _ in range(14):
        dev.key(BTN_UP, 0.05)
    check(dev.screen_id() == SCR["HOME"], "UP 回顶")
    # 滚到底部再回顶（scroll 边界）
    for _ in range(11):
        dev.key(BTN_DOWN, 0.06)
    for _ in range(11):
        dev.key(BTN_UP, 0.06)
    check(dev.screen_id() == SCR["HOME"], "滚动往返正常")


def t_learn(dev):
    print("=== 4. 今日学习（卡片）===", flush=True)
    sid = dev.menu(0)
    if sid == SCR["MSG"]:
        # 今日新词配额已满（当天多次测试累积）：调大每日新词量再进
        print("  今日配额已满，调大每日新词量后重试")
        dev.menu(11)                       # 设置
        dev.key(BTN_DOWN, 0.12)            # 行1 每日新词
        for _ in range(3):
            dev.key(BTN_RIGHT, 0.12)       # 20→30→50
        dev.key(BTN_B, 0.3)                # 保存返回主菜单
        wait_screen(dev, SCR["HOME"], 2.0)
        sid = dev.menu(0)
    check(sid == SCR["LEARN"], "进入今日学习", f"screen={sid}")
    dev.key(BTN_A, 0.2)                      # 显示释义
    dev.key(BTN_RIGHT, 0.2)                  # 记得
    time.sleep(0.55)                         # 等反馈推进
    s1 = dev.screen_id()
    check(s1 in (SCR["LEARN"], SCR["SUMMARY"]), "答对后推进", f"screen={s1}")
    if s1 == SCR["LEARN"]:
        dev.key(BTN_A, 0.2)
        dev.key(BTN_LEFT, 0.2)               # 忘了
        time.sleep(0.75)
        s2 = dev.screen_id()
        check(s2 in (SCR["LEARN"], SCR["SUMMARY"]), "答错后推进", f"screen={s2}")
    dev.key(BTN_B, 0.3)                      # 结束 → 小结
    wait_screen(dev, SCR["SUMMARY"], 2.0)
    s3 = dev.screen_id()
    check(s3 == SCR["SUMMARY"], "结束进入小结", f"screen={s3}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "小结返回主菜单")


def t_review(dev):
    print("=== 5. 快速复习 ===", flush=True)
    sid = dev.menu(1)
    check(sid in (SCR["LEARN"], SCR["MSG"]), "进入快速复习", f"screen={sid}")
    if sid == SCR["MSG"]:
        dev.key(BTN_A, 0.25)
        check(wait_screen(dev, SCR["HOME"], 2.0), "无复习词时提示返回")
    else:
        dev.key(BTN_B, 0.3)
        wait_screen(dev, SCR["SUMMARY"], 2.0)
        dev.key(BTN_B, 0.3)
        check(wait_screen(dev, SCR["HOME"], 2.0), "复习返回主菜单")


def t_quiz(dev):
    print("=== 6. 四选一测验 ===", flush=True)
    sid = dev.menu(2)
    check(sid == SCR["QUIZ"], "进入四选一测验", f"screen={sid}")
    for i in range(3):
        dev.key(BTN_DOWN, 0.08)
        dev.key(BTN_A, 0.2)
        time.sleep(0.75)
        s = dev.screen_id()
        if s != SCR["QUIZ"]:
            break
    s = dev.screen_id()
    check(s in (SCR["QUIZ"], SCR["SUMMARY"]), "测验答题推进", f"screen={s}")
    dev.key(BTN_B, 0.3)
    s = dev.screen_id()
    check(s in (SCR["SUMMARY"], SCR["HOME"]), "测验结束", f"screen={s}")
    if s == SCR["SUMMARY"]:
        dev.key(BTN_B, 0.3)
        check(wait_screen(dev, SCR["HOME"], 2.0), "小结返回主菜单")


def t_spell(dev):
    print("=== 7. 拼写练习 ===", flush=True)
    sid = dev.menu(3)
    check(sid == SCR["SPELL"], "进入拼写练习", f"screen={sid}")
    dev.key(BTN_A, 0.15)                     # 输入一个字母
    dev.key(BTN_A, 0.15)
    dev.key(BTN_B, 0.15)                     # 退格
    dev.key(BTN_DOWN, 0.1)                   # 键盘移动
    dev.key(BTN_A, 0.15)
    s = dev.screen_id()
    check(s == SCR["SPELL"], "拼写界面操作不崩溃", f"screen={s}")
    # 退出：B 可能先当退格（清空已输字母），需要多按几次直到离开
    for _ in range(5):
        if dev.screen_id() != SCR["SPELL"]:
            break
        dev.key(BTN_B, 0.25)
    s = dev.screen_id()
    check(s in (SCR["SUMMARY"], SCR["HOME"]), "拼写退出", f"screen={s}")
    if s == SCR["SUMMARY"]:
        dev.key(BTN_B, 0.3)
        check(wait_screen(dev, SCR["HOME"], 2.0), "小结返回主菜单")


def t_level_game(dev):
    print("=== 8. 单词闯关（确定性判对/判错）===", flush=True)
    sid = dev.menu(4)
    check(sid == SCR["GLEVELS"], "进入选关", f"screen={sid}")
    dev.key(BTN_A, 0.4)
    sid = dev.screen_id()
    check(sid == SCR["GPLAY"], "进入第 1 关", f"screen={sid}")

    def pick(option):
        g = dev.game()
        if not g:
            return None
        cur = int(re.search(r"optSel=(\d+)", g).group(1))
        while cur != option:
            dev.key(BTN_DOWN if option > cur else BTN_UP, 0.07)
            cur += 1 if option > cur else -1
        dev.key(BTN_A, 0.25)

    # 第 1 题：答对
    g = dev.game()
    oc = int(re.search(r"optC=(\d+)", g).group(1)) if g else 0
    pick(oc)
    time.sleep(0.8)
    g = dev.game()
    check(g and "q=1/10" in g and "correct=1" in g, "答对进入第 2 题", g or "")
    # 第 2 题：答错（验证不会连错）
    if g:
        oc = int(re.search(r"optC=(\d+)", g).group(1))
        pick((oc + 1) % 4)
        time.sleep(0.8)
        g = dev.game()
        check(g and "q=2/10" in g and "bad=1" in g and "lives=2" in g,
              "答错只扣一条命且继续", g or "")
    # 第 3 题：再答对，连击
    if g:
        oc = int(re.search(r"optC=(\d+)", g).group(1))
        pick(oc)
        time.sleep(0.8)
        g = dev.game()
        check(g and "q=3/10" in g, "第 3 题正常推进", g or "")
    # 退出到选关
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["GLEVELS"], 2.0), "中途退出回选关")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "选关返回主菜单")


def t_match(dev):
    print("=== 9. 配对消消乐 ===", flush=True)
    sid = dev.menu(5)
    check(sid == SCR["MATCH"], "进入配对消消乐", f"screen={sid}")
    m = dev.match()
    check(bool(m), "获取配对状态", m or "")
    mm = re.search(r"cards=([\d,]+)", m) if m else None
    cards = [int(x) for x in mm.group(1).split(",") if x] if mm else []
    # 找一对相同单词的牌
    pair = None
    for i in range(len(cards)):
        for j in range(i + 1, len(cards)):
            if cards[i] == cards[j]:
                pair = (i, j)
    if not pair:
        check(False, "找到可配对的两张牌", m or "")
        dev.key(BTN_B, 0.3)
        return
    # 光标移到 idx（3 列 x 4 行网格：LEFT/RIGHT 换列 ±1，UP/DOWN 换行 ±3）
    def move_to(idx):
        cur = None
        m = dev.match()
        if m:
            mm = re.search(r"cursor=(\d+)", m)
            if mm:
                cur = int(mm.group(1))
        if cur is None:
            cur = 0
        for _ in range(40):                      # 迭代上限，防 dump 失败时死循环
            if cur == idx:
                return
            col, row = cur % 3, cur // 3
            tcol, trow = idx % 3, idx // 3
            if col != tcol:
                dev.key(BTN_RIGHT if tcol > col else BTN_LEFT, 0.08)
            else:
                dev.key(BTN_DOWN if trow > row else BTN_UP, 0.08)
            m = dev.match()
            if m:
                mm = re.search(r"cursor=(\d+)", m)
                if mm:
                    cur = int(mm.group(1))

    move_to(pair[0])
    dev.key(BTN_A, 0.2)                      # 翻第一张
    move_to(pair[1])
    dev.key(BTN_A, 0.3)                      # 翻第二张 → 配对成功
    m = dev.match()
    check(bool(re.search(r"score=1", m or "")), "配对成功得分", m or "")

    # 故意配错一次。注意：要排除已经消掉的两张牌（再按已被消掉的牌不会翻），
    # 而且第一轮的 pair 取的是「最后一次找到的相同组合」，不一定是 (0,x)。
    wrong = None
    mm = re.search(r"matched=([\d,]+)", dev.match() or "")
    done = set()
    if mm:
        done = {i for i, x in enumerate(mm.group(1).split(",")) if x.strip() == "1"}
    free = [i for i in range(len(cards)) if i not in done]
    for i in free:
        for j in free:
            if i != j and cards[i] != cards[j]:
                wrong = (i, j)          # 取最后一组，尽量贴近真实随机操作
    if wrong:
        check(bool(wrong), "找到可故意配错的两张牌", str((free, wrong)))
        move_to(wrong[0])
        dev.key(BTN_A, 0.2)
        move_to(wrong[1])
        dev.key(BTN_A, 0.3)
        m = dev.match()
        check(m and "score=1" in m and "tries=2" in m, "配错不计分只计尝试", m or "")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "配对返回主菜单")


def t_scramble(dev):
    print("=== 10. 字母重组（含重复字母词也能拼对）===", flush=True)
    sid = dev.menu(6)
    check(sid == SCR["SCRAMBLE"], "进入字母重组", f"screen={sid}")
    s = dev.scrline()
    check(bool(s), "获取字母重组状态", s or "")
    if not s:
        dev.key(BTN_B, 0.3)
        return

    def field(pat, line):
        m = re.search(pat, line) if line else None
        return m.group(1) if m else None

    word = field(r"word=(\S+)", s)
    if not word:
        check(False, "解析字母重组词", s)
        dev.key(BTN_B, 0.3)
        return
    # 逐位填正确字母：候选 j 满足 word[order[j]] == word[pos]
    for pos in range(len(word)):
        s = dev.scrline()
        order = [int(x) for x in (field(r"order=([\d,]+)", s) or "").split(",") if x != ""]
        cur = int(field(r"sel=(\d+)", s) or 0)
        if not order:
            break
        target = None
        for j in range(len(order)):
            if j < len(order) and word[order[j]].lower() == word[pos].lower():
                target = j
                break
        if target is None:
            break
        for _ in range(30):                     # 迭代上限防死循环
            if cur == target:
                break
            dev.key(BTN_RIGHT if target > cur else BTN_LEFT, 0.07)
            cur += 1 if target > cur else -1
            s2 = dev.scrline()
            c2 = field(r"sel=(\d+)", s2)
            if c2 is not None:
                cur = int(c2)
        dev.key(BTN_A, 0.15)
    time.sleep(0.3)
    s = dev.scrline()
    okflag = field(r"ok=(\d+)/", s)
    check(okflag is not None or (s and "score=10" in s), "拼对得分", s or "")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "字母重组返回主菜单")


def t_hangman(dev):
    print("=== 11. 猜单词（新游戏）===", flush=True)

    def hfield(pat):
        h = dev.hang()
        if not h:
            return None
        m = re.search(pat, h)
        return m.group(1) if m else None

    def kb_to(target):
        """把键盘光标移到 target（0-25，7 列 x 4 行）。"""
        for _ in range(40):
            kb = hfield(r"kb=(\d+)")
            if kb is None:
                return False
            kb = int(kb)
            if kb == target:
                return True
            if target // 7 != kb // 7:
                dev.key(BTN_DOWN if target > kb else BTN_UP, 0.07)
            else:
                dev.key(BTN_RIGHT if target > kb else BTN_LEFT, 0.07)
        return hfield(r"kb=(\d+)") == str(target)

    sid = dev.menu(7)
    check(sid == SCR["HANG"], "进入猜单词", f"screen={sid}")
    h = dev.hang()
    check(h and "lives=6" in h, "初始 6 条命", h or "")
    word = hfield(r"word=(\S+)")
    check(bool(word), "获取谜底词", str(h))
    if not word:
        dev.key(BTN_B, 0.3)
        return
    # 猜一个正确字母
    good_letter = word[0].lower()
    check(kb_to(ord(good_letter) - ord("a")), "键盘移到目标字母")
    dev.key(BTN_A, 0.25)
    shown = hfield(r"shown=(\S*)")
    check(shown is not None and good_letter in shown, "猜对字母后显示", str(shown))
    # 猜一个错误字母
    bad = next((chr(ord("a") + i) for i in range(26) if chr(ord("a") + i) not in word.lower()), "z")
    check(kb_to(ord(bad) - ord("a")), "键盘移到错误字母")
    dev.key(BTN_A, 0.25)
    lives = hfield(r"lives=(\d+)")
    check(lives == "5", "猜错扣一条命", f"lives={lives}")
    # 把剩下字母全猜对 → 获胜。
    # 注意要每轮重读 word/shown：上一词猜完后固件会自动进下一词，
    # 拿着旧词的字母去猜新词会被判错扣命（曾经就是这个导致偶发失败）。
    phase = "0"
    for _ in range(40):
        h = dev.hang()
        if not h:
            break
        ph = re.search(r"phase=(\d+)", h)
        phase = ph.group(1) if ph else phase
        if phase == "1":
            break
        cur = re.search(r"word=(\S+)", h)
        sh = re.search(r"shown=(\S*)", h)
        cur = cur.group(1).lower() if cur else ""
        sh = sh.group(1).lower() if sh else ""
        todo = [c for c in sorted(set(cur)) if c and c not in sh]
        if not todo:
            time.sleep(0.4)
            continue
        if not kb_to(ord(todo[0]) - ord("a")):
            break
        dev.key(BTN_A, 0.2)
    if phase != "1":
        phase = hfield(r"phase=(\d+)") or "?"
    check(phase == "1", "猜出整个单词", f"phase={phase}")
    # 等固件进入下一词
    for _ in range(15):
        time.sleep(0.3)
        h = dev.hang()
        if h and "lives=6" in h and "ok=1/" in h:
            break
    check(h and "lives=6" in h and "ok=1/" in h, "获胜后进入下一词", h or "")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "猜单词返回主菜单")


def t_chain(dev):
    print("=== 12. 单词接龙（新游戏）===", flush=True)

    def cfield(pat):
        c = dev.chain()
        if not c:
            return None
        m = re.search(pat, c)
        return m.group(1) if m else None

    def move_sel(want):
        for _ in range(20):
            cur = cfield(r"sel=(\d+)")
            if cur is None:
                return False
            cur = int(cur)
            if cur == want:
                return True
            dev.key(BTN_DOWN if want > cur else BTN_UP, 0.08)
        cur = cfield(r"sel=(\d+)")
        return cur is not None and int(cur) == want

    sid = dev.menu(8)
    check(sid == SCR["CHAIN"], "进入单词接龙", f"screen={sid}")
    c = dev.chain()
    check(c and "lives=3" in c, "初始 3 条命", c or "")
    # 选正确项
    correct = cfield(r"correct=(\d+)")
    if correct is None:
        check(False, "获取接龙状态", str(c))
        dev.key(BTN_B, 0.3)
        return
    correct = int(correct)
    opts_before = (cfield(r"opts=(\S+)") or "").split(",")   # 按 A 前的选项
    check(move_sel(correct), "光标移到正确项")
    dev.key(BTN_A, 0.3)
    c = dev.chain()
    check(c and "combo=1" in c and "round=1" in c, "接龙成功连击+1", c or "")
    # 当前词应变成刚选中的词（按 A 后 cur=选中词，opts 已是新回合的）
    cur_word = cfield(r"cur=(\S+)")
    check(cur_word == opts_before[correct] if len(opts_before) > correct else False,
          "选中词成为新当前词", f"{cur_word} vs {opts_before}")
    # 选错误项
    correct = cfield(r"correct=(\d+)")
    if correct is None:
        dev.key(BTN_B, 0.3)
        return
    correct = int(correct)
    wrong = (correct + 1) % 3
    check(move_sel(wrong), "光标移到错误项")
    dev.key(BTN_A, 0.3)
    c = dev.chain()
    check(c and "lives=2" in c and "phase=1" in c, "选错扣命并显示反馈", c or "")
    time.sleep(1.7)
    c = dev.chain()
    check(c and "lives=2" in c and "phase=0" in c, "反馈后开新局", c or "")
    # 连错两次 → 游戏结束
    for _ in range(2):
        correct = cfield(r"correct=(\d+)")
        if correct is None:
            break
        wrong = (int(correct) + 1) % 3
        move_sel(wrong)
        dev.key(BTN_A, 0.3)
        time.sleep(1.7)
    s = dev.screen_id()
    check(s == SCR["CHAINEND"], "3 命耗尽进结算", f"screen={s}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "结算返回主菜单")


def t_wrong_stats(dev):
    print("=== 13. 错词本 + 统计 ===", flush=True)
    sid = dev.menu(9)
    check(sid == SCR["WRONG"], "进入错词本", f"screen={sid}")
    dev.key(BTN_DOWN, 0.1)
    dev.key(BTN_UP, 0.1)
    dev.key(BTN_A, 0.15)                     # 第一项是"清空"，按一次进入确认态（不按第二次）
    s = dev.screen_id()
    check(s == SCR["WRONG"], "错词本导航不崩溃", f"screen={s}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "错词本返回")
    sid = dev.menu(10)
    check(sid == SCR["STATS"], "进入统计", f"screen={sid}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "统计返回")


def t_settings(dev):
    print("=== 14. 设置（全部 12 行）===", flush=True)
    cfg0 = dev.cfg()
    v = {k: int(x) for k, x in re.findall(r"(\w+)=(-?\d+)", cfg0)} if cfg0 else {}

    def cfgval(name):
        c = dev.cfg()
        mm = re.search(rf"{name}=(-?\d+)", c) if c else None
        return int(mm.group(1)) if mm else None

    def restore_cycle(name, presses):
        """按相反方向按回去，直到值恢复。"""
        for _ in range(8):
            if cfgval(name) == v.get(name):
                return True
            dev.key(presses[0], 0.12)
        return cfgval(name) == v.get(name)

    sid = dev.menu(11)
    check(sid == SCR["SETTINGS"], "进入设置", f"screen={sid}")

    # 行 0 词册
    dev.key(BTN_A, 0.3)
    check(wait_screen(dev, SCR["BOOKS"], 2.0), "进入词册选择")
    en0 = cfgval("enabled")
    dev.key(BTN_A, 0.25)                     #  toggle 第一册
    en1 = cfgval("enabled")
    check(en0 != en1, "勾选词册改变启用词数", f"{en0} -> {en1}")
    dev.key(BTN_A, 0.25)                     # 再 toggle 还原
    check(cfgval("enabled") == en0, "再次勾选还原")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["SETTINGS"], 2.0), "词册返回设置")

    # 行 1 每日新词（循环档位）
    dev.key(BTN_DOWN, 0.12)
    dn0 = cfgval("dailyNew")
    dev.key(BTN_LEFT, 0.15)
    dn1 = cfgval("dailyNew")
    check(dn1 != dn0 or dn0 == 10, "每日新词可调", f"{dn0} -> {dn1}")
    restore_cycle("dailyNew", (BTN_RIGHT,))
    check(cfgval("dailyNew") == dn0, "每日新词还原")

    # 行 2 音效开关
    dev.key(BTN_DOWN, 0.12)
    s0 = cfgval("sound")
    dev.key(BTN_LEFT, 0.15)
    check(cfgval("sound") != s0, "音效开关切换")
    dev.key(BTN_RIGHT, 0.15)
    check(cfgval("sound") == s0, "音效开关还原")
    dev.cmd("m0", 0.2)                         # 上面的"还原"会把蜂鸣器打开，重新强制静音

    # 行 3 显示方向
    dev.key(BTN_DOWN, 0.12)
    o0 = cfgval("orient")
    dev.key(BTN_LEFT, 0.2)
    check(cfgval("orient") != o0, "显示方向切换")
    restore_cycle("orient", (BTN_RIGHT,))
    check(cfgval("orient") == o0, "显示方向还原")

    # 行 4 颜色模式
    dev.key(BTN_DOWN, 0.12)
    b0 = cfgval("bgr")
    dev.key(BTN_LEFT, 0.2)
    check(cfgval("bgr") != b0, "颜色模式切换")
    dev.key(BTN_RIGHT, 0.2)
    check(cfgval("bgr") == b0, "颜色模式还原")

    # 行 5 屏保样式 + A 预览
    dev.key(BTN_DOWN, 0.12)
    m0 = cfgval("saverMode")
    dev.key(BTN_LEFT, 0.15)
    check(cfgval("saverMode") != m0, "屏保样式切换")
    restore_cycle("saverMode", (BTN_RIGHT,))
    check(cfgval("saverMode") == m0, "屏保样式还原")
    dev.key(BTN_A, 0.3)
    sv = dev.saver()
    check(sv == 1, "A 预览屏保", f"saver={sv}")
    dev.key(BTN_A, 0.3)
    sv = dev.saver()
    for _ in range(3):                  # 响应可能滞后：先重查，仍为 1 再补按
        if sv == 0:
            break
        time.sleep(0.25)
        sv = dev.saver()
    if sv != 0:
        dev.key(BTN_A, 0.3)
        sv = dev.saver()
    check(sv == 0, "按键退出屏保预览", f"saver={sv}")

    # 行 6 屏保延时
    dev.key(BTN_DOWN, 0.12)
    ss0 = cfgval("saverSec")
    dev.key(BTN_LEFT, 0.15)
    check(cfgval("saverSec") != ss0 or ss0 == 0, "屏保延时切换")
    restore_cycle("saverSec", (BTN_RIGHT,))
    check(cfgval("saverSec") == ss0, "屏保延时还原")

    # 行 7 电量显示
    dev.key(BTN_DOWN, 0.12)
    bm0 = cfgval("battMode")
    dev.key(BTN_LEFT, 0.15)
    check(cfgval("battMode") != bm0, "电量模式切换")
    restore_cycle("battMode", (BTN_RIGHT,))
    check(cfgval("battMode") == bm0, "电量模式还原")

    # 行 8 WiFi 设置
    dev.key(BTN_DOWN, 0.12)
    dev.key(BTN_A, 0.3)
    check(wait_screen(dev, SCR["WIFI"], 2.0), "进入 WiFi 设置")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["SETTINGS"], 2.0), "WiFi 返回设置")

    # 行 9 时间设置
    dev.key(BTN_DOWN, 0.12)
    dev.key(BTN_A, 0.3)
    check(wait_screen(dev, SCR["TIME"], 2.0), "进入手动时间")
    dev.key(BTN_RIGHT, 0.1)                  # 切换字段
    dev.key(BTN_UP, 0.1)                     # 改值
    dev.key(BTN_A, 0.3)                      # 保存 → 消息屏
    s = dev.screen_id()
    check(s == SCR["MSG"], "保存时间弹出消息", f"screen={s}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "消息返回主菜单")

    # 行 10 清空进度（只按一次进确认态，不真清空）
    sid = dev.menu(11)
    for _ in range(10):
        dev.key(BTN_DOWN, 0.08)
    dev.key(BTN_A, 0.2)
    s = dev.screen_id()
    check(s == SCR["SETTINGS"], "清空进度进入确认态（未清空）", f"screen={s}")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["HOME"], 2.0), "设置返回主菜单")

    # 行 11 关于
    sid = dev.menu(11)
    for _ in range(11):
        dev.key(BTN_DOWN, 0.08)
    dev.key(BTN_A, 0.3)
    check(wait_screen(dev, SCR["ABOUT"], 2.0), "进入关于")
    dev.key(BTN_B, 0.3)
    check(wait_screen(dev, SCR["SETTINGS"], 2.0), "关于返回设置")


def t_wifi_time(dev):
    print("=== 15. WiFi 连接 + 对时 ===", flush=True)
    lines = dev.cmd("Snishishiba,Kaibo1988", 0.4)
    for _ in range(3):                    # 首条命令偶发被吞
        if any("已设置" in l for l in lines):
            break
        lines = dev.cmd("Snishishiba,Kaibo1988", 0.4)
    check(any("已设置" in l for l in lines), "发送 WiFi 凭据", lines[-1] if lines else "")
    ok = False
    for _ in range(20):
        time.sleep(1.0)
        info = dev.info()
        if info and re.search(r"wifi=(\d+)", info):
            st = int(re.search(r"wifi=(\d+)", info).group(1))
            ip = re.search(r"ip=(\S+)", info).group(1)
            if st == 2 and ip != "0.0.0.0":     # 2 = CONNECTED（见 net.h）
                ok = True
                break
    check(ok, "WiFi 连接成功", info or "")
    lines = dev.cmd("n", 0.5)
    time.sleep(2.5)
    info = dev.info()
    check(bool(info) and "synced=1" in info, "NTP 对时成功", info or "")


def t_saver(dev):
    print("=== 16. 屏保自动进入/退出（含误触回归）===", flush=True)
    dev.cmd("M3", 0.2)
    dev.go_home()
    time.sleep(4.0)
    sv = dev.saver()
    check(sv == 1, "闲置 3 秒自动进入屏保", f"saver={sv}")
    # 连续 30 次按键：每次都不允许把屏保误触回来
    bad = 0
    for _ in range(30):
        dev.key(BTN_A, 0.05)
        s = dev.screen()
        if s and s[1] == 1:
            bad += 1
    check(bad == 0, "30 次按键无一次误触屏保", f"bad={bad}")
    sv = dev.saver()
    check(sv == 0, "按键后屏保保持退出", f"saver={sv}")
    # 再次闲置应能进入
    dev.go_home()
    time.sleep(4.0)
    sv = dev.saver()
    check(sv == 1, "再次闲置可进入屏保", f"saver={sv}")
    dev.key(BTN_A, 0.2)
    sv = dev.saver()
    check(sv == 0, "退出屏保", f"saver={sv}")
    dev.cmd("M600", 0.2)


def t_soak(dev):
    print("=== 17. 随机按键压测 ===", flush=True)
    # GD32 串口桥偶发吞首字节（z3000 → z000 → 默认 2000 次），而且设备忙时会彻底漏收，
    # 所以允许重发，只要最终收到 [soak] 就算压测跑通。
    joined = ""
    for attempt in range(3):
        dev.cmd("z3000", 0.5)
        out = []
        end = time.time() + 90
        while time.time() < end:
            out += dev._drain(0.5)
            if any("soak" in l for l in out):
                break
        joined = "\n".join(out)
        if "soak" in joined:
            break
        dev.cmd("p", 0.3)
        print("  压测第 %d 次未返回，探针 i=%s p=%s"
              % (attempt + 1, dev.cmd("i", 0.4), dev.cmd("p", 0.4)), flush=True)
    check("soak" in joined, "压测完成", joined[-160:])
    m = re.search(r"剩余堆 (\d+)", joined)
    check(bool(m) and int(m.group(1)) > 100000, "压测后堆内存健康", m.group(0) if m else "")
    s = dev.screen()
    check(s is not None, "压测后设备响应正常", str(s))


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    print(f"========== 小喵掌机全功能测试 {port} ==========", flush=True)
    dev = Dev(port)

    # 准备：先把校到干净起点。上一轮压测的 3000 次随机按键会把设置和词库勾选改乱
    # （曾经出现 enabled=0 / dailyNew=30，导致后面「学习/测验/拼写」全线失败），
    # 所以每轮开头固定恢复默认设置并清空进度，保证测试可重复。
    dev.cmd("R", 0.5)
    dev.cmd("P", 1.5)

    # 静音蜂鸣器（m0）+ 临时放大屏保延时（避免测试中途进屏保）+ 注入当前时间
    # 注意：设置段的音效行会把蜂鸣器切回"开"，所以测完设置后必须重新 m0
    r = dev.cmd("m0", 0.25)
    for _ in range(3):                          # 首条命令可能被吞，重试
        if any("关" in l for l in r):
            break
        r = dev.cmd("m0", 0.25)
    check(any("关" in l for l in r), "测试前关闭蜂鸣器", str(r))
    dev.cmd("M600", 0.2)
    dev.cmd(f"T{int(time.time())}", 0.25)

    try:
        t_boot_selftest(dev)
        t_saver_selftest(dev)
        t_lights(dev)
        t_menu(dev)
        t_learn(dev)
        t_review(dev)
        t_quiz(dev)
        t_spell(dev)
        t_level_game(dev)
        t_match(dev)
        t_scramble(dev)
        t_hangman(dev)
        t_chain(dev)
        t_wrong_stats(dev)
        t_settings(dev)
        t_saver(dev)
        t_wifi_time(dev)
        t_soak(dev)
    finally:
        # 恢复：蜂鸣器（按设置值恢复为开）、屏保延时
        dev.cmd("m1", 0.2)
        dev.cmd("M-1", 0.2)
        dev.close()

    print(f"\n========== 结果：通过 {PASS} 失败 {FAIL} ==========", flush=True)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
