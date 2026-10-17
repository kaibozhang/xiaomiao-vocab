# -*- coding: utf-8 -*-
"""
用真实生成的字模数据 + 真实词库，1:1 渲染固件界面预览图（验证排版是否放得进 160x128）。
输出: preview/preview.png
用法: python tools/preview.py
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import pack_words  # noqa: E402

W, H = 160, 128
HEAD_H, BODY_TOP, BODY_END, FOOT_Y = 15, 16, 112, 113
LIST_TOP, ITEM_H = 17, 14

C_BG, C_PANEL, C_BAR, C_FG, C_DIM = 0x10C4, 0x2126, 0x3186, 0xFFFF, 0x9515
C_ACCENT, C_OK, C_ERR, C_WARN, C_SEL = 0x0655, 0x3E4A, 0xE228, 0xFE47, 0x2AD9


def rgb(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


# ------------------------------------------------------------------ 解析字模
def parse_fonts(path):
    src = open(path, encoding="utf-8").read()
    fonts = {}
    for name in ("FONT_12", "FONT_16", "FONT_24"):
        m = re.search(r"static const Glyph %s_GLYPHS\[\d+\] = \{(.*?)\n\};" % name, src, re.S)
        glyphs = {}
        for row in re.findall(r"\{(0x[0-9A-Fa-f]+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*\d+,\s*(\d+)\}", m.group(1)):
            cp = int(row[0], 16)
            glyphs[cp] = dict(w=int(row[1]), h=int(row[2]), adv=int(row[3]), left=int(row[4]), top=int(row[5]), off=int(row[6]))
        bm = re.search(r"const uint8_t %s_BMP\[\d+\] = \{(.*?)\n\};" % name, src, re.S)
        data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm.group(1)))
        meta = re.search(r"\{%s_GLYPHS,\s*%s_BMP,\s*\d+,\s*(\d+),\s*(\d+)\}" % (name, name), src)
        fonts[name] = dict(g=glyphs, bmp=data, ascent=int(meta.group(1)), line=int(meta.group(2)))
    return fonts


FONTS = parse_fonts(os.path.join(ROOT, "src", "font_data.cpp"))


class Canvas:
    def __init__(self):
        self.img = Image.new("RGB", (W, H), rgb(C_BG))
        self.px = self.img.load()

    def fill(self, x, y, w, h, c):
        col = rgb(c)
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(W, x + w), min(H, y + h)
        for yy in range(y0, y1):
            for xx in range(x0, x1):
                self.px[xx, yy] = col

    def rect(self, x, y, w, h, c):
        self.fill(x, y, w, 1, c)
        self.fill(x, y + h - 1, w, 1, c)
        self.fill(x, y, 1, h, c)
        self.fill(x + w - 1, y, 1, h, c)

    def tw(self, s, font):
        f = FONTS[font]
        return sum(f["g"].get(ord(ch), {}).get("adv", f["ascent"] // 2) for ch in s)

    def text(self, s, x, y, c, font, maxw=None):
        f = FONTS[font]
        col = rgb(c)
        pen = x
        for ch in s:
            g = f["g"].get(ord(ch))
            if not g:
                pen += f["ascent"] // 2
                continue
            stride = (g["w"] + 7) // 8
            for ry in range(g["h"]):
                py = y + f["ascent"] + g["top"] + ry
                if not (0 <= py < H):
                    continue
                for rx in range(g["w"]):
                    byte = f["bmp"][g["off"] + ry * stride + (rx >> 3)]
                    if byte & (1 << (7 - (rx & 7))):
                        pxp = pen + g["left"] + rx
                        if 0 <= pxp < W:
                            self.px[pxp, py] = col
            pen += g["adv"]
        return pen - x

    def center(self, s, y, c, font):
        w = self.tw(s, font)
        return self.text(s, max(0, (W - w) // 2), y, c, font)

    def right(self, s, xe, y, c, font):
        return self.text(s, xe - self.tw(s, font), y, c, font)

    def trunc(self, s, x, y, maxw, c, font):
        if self.tw(s, font) <= maxw:
            return self.text(s, x, y, c, font)
        ell = self.tw("…", font)
        out = ""
        for ch in s:
            if self.tw(out + ch, font) + ell > maxw:
                break
            out += ch
        used = self.text(out, x, y, c, font)
        self.text("…", x + used, y, c, font)
        return used + ell


def header(cv, title, right=""):
    cv.fill(0, 0, W, HEAD_H, C_BAR)
    cv.text(title, 3, 1, C_FG, "FONT_12")
    if right:
        cv.right(right, W - 3, 1, C_DIM, "FONT_12")
    cv.fill(0, HEAD_H, W, 1, C_ACCENT)


def footer(cv, hint):
    cv.fill(0, FOOT_Y, W, H - FOOT_Y, C_PANEL)
    cv.fill(0, FOOT_Y, W, 1, C_BAR)
    cv.text(hint, 3, FOOT_Y + 1, C_DIM, "FONT_12")


def row(cv, y, left, right, sel):
    if sel:
        cv.fill(0, y - 1, W, ITEM_H, C_SEL)
    cv.trunc(left, 4, y, W - 60, C_FG, "FONT_12")
    if right:
        cv.right(right, W - 4, y, C_ACCENT if not sel else C_FG, "FONT_12")


# ------------------------------------------------------------------ 各屏
def scr_home(cv, words):
    header(cv, "小喵背单词")
    names = ["今日学习", "快速复习", "四选一测验", "拼写练习", "单词闯关", "错词本", "统计", "设置"]
    rights = ["20新 8复", "8", "", "", "星6 关2", "12", "", ""]
    for i in range(8):
        row(cv, LIST_TOP + i * ITEM_H, names[i], rights[i], i == 0)
    footer(cv, "第1场  %d词" % len(words))


def scr_glevels(cv):
    header(cv, "单词闯关", "6/90")
    unlocked = 2
    for i in range(30):
        col, rowi = i % 5, i // 5
        x, y = 4 + col * 31, 18 + rowi * 16
        lv = i + 1
        locked = lv > unlocked + 1
        cur = (lv == 3)
        cv.fill(x, y, 29, 14, C_SEL if cur else C_PANEL)
        fg = C_DIM if locked else (C_FG if cur else C_ACCENT)
        cv.center(str(lv), y + (2 if locked else 1), fg, "FONT_12")
        if not locked:
            st = {1: 3, 2: 3, 3: 0}.get(lv, 0)
            for s in range(3):
                cv.fill(x + 6 + s * 6, y + 10, 4, 2, C_WARN if s < st else C_DIM)
    footer(cv, "←→↑↓ 选关  A 开始  B 返回")


def scr_gplay(cv, w, opts):
    header(cv, "第3关 4/10")
    for i in range(3):
        cv.fill(W - 10 - i * 8, 4, 6, 6, C_ERR if i < 2 else C_DIM)
    f = "FONT_24" if cv.tw(w["en"], "FONT_24") <= W - 12 else "FONT_16"
    cv.center(w["en"], 16, C_FG, f)
    if w["ph"]:
        cv.center(w["ph"], 44 if f == "FONT_24" else 34, C_DIM, "FONT_12")
    cv.right("x3", W - 30, 1, C_WARN, "FONT_12")
    cv.fill(4, 52, W - 8, 3, C_PANEL)
    cv.fill(4, 52, int((W - 8) * 0.6), 3, C_OK)
    for i, o in enumerate(opts):
        y = 58 + i * 18
        cv.fill(2, y - 1, W - 4, 17, C_SEL if i == 1 else C_PANEL)
        cv.trunc("ABCD"[i] + " " + o, 4, y + 1, W - 8, C_FG, "FONT_16")
    footer(cv, "↑↓ 选择   A 确认   B 退出")


def scr_gresult(cv):
    header(cv, "通关")
    cv.center("好棒，闯关成功", 22, C_OK, "FONT_12")
    for s in range(3):
        cv.fill(52 + s * 20, 36, 14, 14, C_WARN if s < 3 else C_DIM)
    cv.center("答对 10/10   最高连击 7", 58, C_FG, "FONT_12")
    cv.center("第3关  星级 3", 74, C_DIM, "FONT_12")
    footer(cv, "A 再来一次   B 选关")


def scr_card(cv, w):
    header(cv, "背单词", "3/20")
    f = "FONT_24" if cv.tw(w["en"], "FONT_24") <= W - 12 else "FONT_16"
    wy = 26 if f == "FONT_24" else 32
    cv.center(w["en"], wy, C_FG, f)
    if w["ph"]:
        cv.center(w["ph"], wy + (27 if f == "FONT_24" else 19), C_ACCENT, "FONT_12")
    cv.fill(12, 74, W - 24, 1, C_BAR)
    line, yy = "", 82
    for ch in w["zh"]:
        if cv.tw(line + ch, "FONT_16") > W - 8:
            cv.text(line, 4, yy, C_FG, "FONT_16")
            yy += 20
            line = ch
        else:
            line += ch
    cv.text(line, 4, yy, C_FG, "FONT_16")
    footer(cv, "← 忘了       记得 →")


def scr_quiz(cv, w, opts):
    header(cv, "四选一 5/20", "对7 错2")
    f = "FONT_24" if cv.tw(w["en"], "FONT_24") <= W - 12 else "FONT_16"
    cv.center(w["en"], 14, C_FG, f)
    if w["ph"]:
        cv.center(w["ph"], 41 if f == "FONT_24" else 33, C_ACCENT, "FONT_12")
    for i, o in enumerate(opts):
        y = 56 + i * 19
        cv.fill(2, y - 1, W - 4, 18, C_SEL if i == 1 else C_PANEL)
        cv.trunc("ABCD"[i] + " " + o, 4, y + 1, W - 8, C_FG, "FONT_16")
    footer(cv, "↑↓ 选择   A 确认   B 结束")


def scr_spell(cv, w, typed):
    header(cv, "拼写练习", "2/15")
    # 中文释义 16px
    cv.text(w["zh"][:12], 3, BODY_TOP + 1, C_WARN, "FONT_16")
    ln = len(w["en"])
    if ln > 12: ln = 12
    step = min(13, (W - 8) // max(ln, 1))
    sx = (W - ln * step) // 2
    for i in range(ln):
        cv.text(typed[i] if i < len(typed) else "_", sx + i * step, 46, C_FG if i < len(typed) else C_DIM, "FONT_24")
    KB = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    for i in range(28):
        c_, r_ = i % 7, i // 7
        x, y = 3 + c_ * 22, 68 + r_ * 11
        cur = i == 4
        cv.fill(x, y, 21, 11, C_ACCENT if cur else C_PANEL)
        ch = KB[i] if i < 26 else ("←" if i == 26 else "OK")
        cv.text("OK" if i == 27 else ch, x + (3 if i == 27 else 7), y - 1, C_BG if cur else (C_WARN if i == 26 else C_OK if i == 27 else C_FG), "FONT_12")
    footer(cv, "方向选  A输入  B退格")


def scr_stats(cv, n):
    header(cv, "学习统计")
    y = BODY_TOP + 2
    for line, col in [("已学 842 / %d" % n, C_FG), ("掌握 311   待复习 8", C_ACCENT),
                      ("错词 12   连续 5 场", C_WARN), ("今日 新20 复8 88%", C_FG),
                      ("累计 1930 题  正确 81%", C_DIM)]:
        cv.text(line, 4, y, col, "FONT_12")
        y += 15
    cv.rect(4, y + 2, W - 8, 10, C_BAR)
    cv.fill(5, y + 3, int((W - 10) * 0.16), 8, C_ACCENT)
    cv.center("16%", y + 2, C_FG, "FONT_12")
    footer(cv, "B 返回")


def scr_settings(cv):
    header(cv, "设置")
    names = ["选择词册", "每日新词", "音效", "显示方向", "颜色模式", "清空进度", "关于"]
    rights = ["20/20", "20", "开", "正常", "RGB", "", ""]
    for i in range(7):
        row(cv, LIST_TOP + i * ITEM_H, names[i], rights[i], i == 1)
    footer(cv, "←→ 调整   A 进入   B 返回")


# ------------------------------------------------------------------ 屏保
def status_bar(cv, t="08:42", pct=86):
    """右上角 电量 + 时间（与固件 header 一致）。"""
    # 电池图标
    bx, by = W - 13 - 2 - cv.tw("00:00", "FONT_12") - 2, 4
    cv.rect(bx, by, 13, 8, C_DIM)
    cv.fill(bx + 13, by + 2, 2, 4, C_DIM)
    fill = (13 - 4) * pct // 100
    cv.fill(bx + 2, by + 2, max(fill, 1), 4, C_OK if pct > 35 else C_WARN)
    cv.right(t, W - 3, 2, C_FG, "FONT_12")


def saver_clock(cv):
    cv.center("10月17日 周五", 16, C_DIM, "FONT_12")
    cv.center("08:42", 30, C_FG, "FONT_24") if "FONT_24" in FONTS else None
    status_bar(cv)
    cv.center("复习 8  新词 20", 100, C_DIM, "FONT_12")
    cv.center("xiaomiao.local", 114, C_ACCENT, "FONT_12")


def saver_word(cv, w):
    cv.center(w["en"], 26, C_FG, "FONT_16")
    cv.center(w["ph"], 54, C_ACCENT, "FONT_12")
    cv.text(w["zh"][:14], 4, 74, C_FG, "FONT_12")
    status_bar(cv)
    cv.right("5s", W - 3, 114, C_DIM, "FONT_12")


def saver_cat(cv):
    import math
    cx, cy = W // 2, 62
    fur, dark = 0xFE47, 0xB345
    # 简单猫脸（与固件 saver.cpp 对应）
    for dx in range(-28, 29):
        for dy in range(-28, 29):
            if dx * dx + dy * dy <= 28 * 28:
                px, py = cx + dx, cy + dy
                if 0 <= px < W and 0 <= py < H:
                    cv.px[px, py] = rgb(fur)
    for s in (-1, 1):
        ex, ey = cx + s * 11, cy - 4
        for dx in range(-4, 5):
            for dy in range(-4, 5):
                if dx * dx + dy * dy <= 16:
                    cv.px[ex + dx, ey + dy] = (0, 0, 0)
    cv.center("08:42", 108, C_ACCENT, "FONT_12")
    cv.text("喵~", 4, 2, C_DIM, "FONT_12")


def main():
    books, words = pack_words.load_words()
    bmap = {i: b["title"] for i, b in enumerate(books)}
    sample = [w for w in words if w["en"] == "important"] or words[3000:3001]
    w0 = dict(sample[0])
    w0["book"] = bmap[w0["book"]]
    w1 = dict(words[4200])
    w1["book"] = bmap[w1["book"]]
    w2 = dict(([x for x in words if x["en"] == "dangerous"] or words[4600:4601])[0])
    w2["book"] = bmap[w2["book"]]

    screens = []
    for fn, args in [(scr_home, (words,)), (scr_card, (w0,)),
                     (scr_quiz, (w1, [w1["zh"], "危险的", "勇敢的", "著名的"])),
                     (scr_spell, (w2, "dange")),
                     (scr_stats, (len(words),)), (scr_settings, ())]:
        cv = Canvas()
        fn(cv, *args)
        screens.append(cv.img)

    # 闯关预览
    for fn, args in [(scr_glevels, ()), (scr_gplay, (w0, [w0["zh"], "危险的", "勇敢的", "著名的"])),
                     (scr_gresult, ())]:
        cv = Canvas()
        fn(cv, *args)
        screens.append(cv.img)

    # 屏保预览
    for fn, args in [(saver_clock, ()), (saver_word, (w0,)), (saver_cat, ())]:
        cv = Canvas()
        fn(cv, *args)
        screens.append(cv.img)

    scale = 3
    cols, rows = 3, 4
    sheet = Image.new("RGB", (W * scale * cols + (cols + 1) * 8, H * scale * rows + (rows + 1) * 8), (28, 30, 36))
    for i, im in enumerate(screens):
        cx, cy = i % cols, i // cols
        sheet.paste(im.resize((W * scale, H * scale), Image.NEAREST),
                    (8 + cx * (W * scale + 8), 8 + cy * (H * scale + 8)))
    out = os.path.join(ROOT, "preview", "preview.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print("预览图已生成:", out, sheet.size)


if __name__ == "__main__":
    main()
