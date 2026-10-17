# -*- coding: utf-8 -*-
"""
小喵掌机背单词固件 —— 词库 / 字模打包脚本

输入 : data/books/*.json（人教版 PEP 小学~高中词库）+ src/ 源码里出现的中文
输出 : src/words_data.h/.cpp   词表
       src/font_data.h/.cpp    精简点阵字模（只打包真正用到的字符）

用法 : python tools/pack_words.py
"""
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")
SRC = os.path.join(ROOT, "src")

# ---------------------------------------------------------------- 字体选择
# 汉字/全角符号用雅黑；拉丁字母和 IPA 音标雅黑缺字，改用 Arial
FONT_CANDIDATES = [
    ("C:/Windows/Fonts/msyh.ttc", 0),
    ("C:/Windows/Fonts/msyh.ttc", 1),
    ("C:/Windows/Fonts/simhei.ttf", 0),
    ("C:/Windows/Fonts/Deng.ttf", 0),
    ("C:/Windows/Fonts/simsun.ttc", 0),
]
LATIN_CANDIDATES = [
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
]

# 字号 -> (点阵名称, 是否只打包拉丁/IPA 子集)
# FONT_12 = UI/列表正文；FONT_16 = 单词中文释义/选项（全字库）；FONT_24 = 英文单词大字
SIZE_PLAN = [("FONT_12", 12, False), ("FONT_16", 16, False), ("FONT_24", 24, True)]

THRESHOLD = 110  # 灰度阈值，越小字越粗

# ---------------------------------------------------------------- 数据清洗
BRACKET = re.compile(r"\[[^\]]*\]")


def clean_zh(s, maxlen=15):
    """把词典式长释义压成适合 160x128 屏幕显示的短释义。"""
    s = (s or "").strip()
    s = BRACKET.sub("", s)
    s = re.sub(r"\s+", " ", s)
    parts = [p.strip(" ；;,，。、") for p in re.split(r"[；;]", s)]
    parts = [p for p in parts if p]
    if not parts:
        return ""
    out = parts[0]
    for p in parts[1:]:
        if len(out) + 1 + len(p) <= maxlen:
            out += "；" + p
        else:
            break
    if len(out) > maxlen:
        out = out[: maxlen - 1] + "…"
    return out


def is_spellable(en):
    return bool(re.fullmatch(r"[a-zA-Z]+", en or ""))


def short_name(b):
    """人教版小学英语 · 三年级上 -> 小学三上"""
    stage = b["stage"]
    tail = b["title"].split("·")[-1].strip()
    tail = tail.replace("选择性必修", "选必").replace("年级", "").replace("册", "").replace("第", "")
    return {"primary": "小学", "junior": "初中", "senior": "高中"}[stage] + tail


# ---------------------------------------------------------------- 读取词库
def load_words():
    idx = json.load(open(os.path.join(DATA, "index.json"), encoding="utf-8"))
    books = []
    words = []
    seen = {}
    for bi, b in enumerate(idx["books"]):
        d = json.load(open(os.path.join(DATA, "books", b["id"] + ".json"), encoding="utf-8"))
        stage = {"primary": 0, "junior": 1, "senior": 2}[b["stage"]]
        books.append({"id": b["id"], "title": short_name(b), "stage": stage})
        ui = 0
        for u in d["units"]:
            ui += 1
            for w in u["words"]:
                en = (w.get("en") or "").strip()
                if not en:
                    continue
                key = en.lower()
                if key in seen:  # 跨册重复只保留最低年级那一次
                    continue
                seen[key] = len(words)
                words.append(
                    {
                        "en": en,
                        "ph": (w.get("phonetic") or "").strip(),
                        "zh": clean_zh(w.get("zh")),
                        "book": bi,
                        "unit": ui,
                    }
                )
    return books, words


# ---------------------------------------------------------------- 字符集
def collect_charset(words, extra_text):
    cs = set()
    for w in words:
        cs.update(w["en"])
        cs.update(w["ph"])
        cs.update(w["zh"])
    cs.update(extra_text)
    cs.add("…")
    cs.add("…")
    return sorted(cs)


def scan_sources():
    txt = []
    for fn in sorted(os.listdir(SRC)):
        if fn.endswith((".cpp", ".h", ".hpp")):
            txt.append(open(os.path.join(SRC, fn), encoding="utf-8").read())
    return "".join(txt)


# ---------------------------------------------------------------- 字模
def pick_font(size):
    for path, index in FONT_CANDIDATES:
        if not os.path.exists(path):
            continue
        try:
            return ImageFont.truetype(path, size, index=index)
        except Exception:
            continue
    raise SystemExit("找不到可用的中文字体")


def pick_latin_font(size):
    for path in LATIN_CANDIDATES:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                continue
    return None


def font_order(ch, cjk, latin):
    """按码位决定用哪个字体渲染（雅黑优先级在前面的用不上就落到下一个）"""
    cp = ord(ch)
    lat = [latin] if latin else []
    if cp >= 0x2E80:
        return [cjk] + lat
    if 0x2500 <= cp <= 0x25FF:      # 几何图形 ■ □ ● ○：雅黑缺，用 Arial
        return lat + [cjk]
    if 0x2000 <= cp <= 0x2BFF:      # 箭头 / 省略号：雅黑有
        return [cjk] + lat
    return lat + [cjk]


def render_font(name, size, chars, line_height=None):
    """两遍扫描生成字模：
    1) 每个字符按统一基线绘制，用像素扫描取紧凑墨迹框（textbbox 是行盒含空白，不可用）
    2) ascent 取全部字形最高墨迹，使字形顶恰好落在行顶；底部裁剪到行盒内
    渲染端约定：text(s,x,y) 的行盒为 [y, y+lineHeight)，基线 = y + ascent，
    字形永不越出本行 —— 这是消除"行间遮挡"的根。
    """
    cjk = pick_font(size)
    latin = pick_latin_font(size)
    if line_height is None:
        line_height = size + max(2, size // 6)

    pad = size
    canvas_w = size * 3 + pad * 2
    canvas_h = size * 3 + pad * 2
    img = Image.new("L", (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(img)

    BASE = pad + size + 2          # 第一遍统一基线（比任何墨迹都低，留足降部空间）
    ox = pad

    def draw_char(ch):
        """把字符画到画布上，基线固定在 BASE，返回 (adv, 紧凑墨迹bbox或None)"""
        draw.rectangle([0, 0, canvas_w, canvas_h], fill=0)
        for f in font_order(ch, cjk, latin):
            fa, _ = f.getmetrics()
            y_top = BASE - int(round(fa))     # PIL 以行顶定位，行顶 = 基线 - ascender
            draw.text((ox, y_top), ch, fill=255, font=f)
            # 像素级扫描真实墨迹（限制在画布内、基线上下 size+3 范围）
            px = img.load()
            y_lo = max(0, BASE - size - 3)
            y_hi = min(canvas_h, BASE + size + 3)
            x0 = y0 = 10 ** 9
            x1 = y1 = -10 ** 9
            for yy in range(y_lo, y_hi):
                for xx in range(canvas_w):
                    if px[xx, yy] >= THRESHOLD:
                        if xx < x0: x0 = xx
                        if xx > x1: x1 = xx
                        if yy < y0: y0 = yy
                        if yy > y1: y1 = yy
            if x1 >= x0 and y1 >= y0:
                return int(round(f.getlength(ch))), (x0, y0, x1, y1)
        return int(round(cjk.getlength(ch))), None

    # ---- 第一遍：测墨迹，定 ascent ----
    measures = {}
    max_top = 1          # 最高的墨迹顶（相对基线，负值）
    for ch in chars:
        adv, bbox = draw_char(ch)
        if bbox:
            max_top = min(max_top, bbox[1] - BASE)
        measures[ch] = (adv, bbox)
    ascent = max(2, -max_top)            # 基线距行顶 = 最高墨迹
    bot_limit = line_height - ascent - 1  # 行盒内允许的最低墨迹（相对基线）

    # ---- 第二遍：按最终度量提取点阵（裁剪到行盒） ----
    glyphs = []
    bitmap = bytearray()
    for ch in chars:
        adv, bbox = measures[ch]
        cp = ord(ch)
        if bbox is None:
            glyphs.append((cp, 0, 0, adv, 0, 0, b""))
            continue
        draw_char(ch)                    # 重画到画布（画面内容与第一遍相同）
        x0, y0, x1, y1 = bbox
        left = x0 - ox
        top = y0 - BASE                  # ≤ -ascent 不会发生（ascent 取了最大值）
        # 底部裁剪到行盒
        y1c = min(y1, BASE + bot_limit)
        if y1c < y0:
            glyphs.append((cp, 0, 0, adv, 0, 0, b""))
            continue
        w, h = x1 - x0 + 1, y1c - y0 + 1
        stride = (w + 7) // 8
        rows = bytearray()
        for ry in range(h):
            acc = 0
            for rx in range(w):
                v = img.getpixel((x0 + rx, y0 + ry))
                if v >= THRESHOLD:
                    acc |= 1 << (7 - (rx & 7))
                if (rx & 7) == 7:
                    rows.append(acc)
                    acc = 0
            if w & 7:
                rows.append(acc)
        assert len(rows) == stride * h, (ch, len(rows), stride * h)
        glyphs.append((cp, w, h, adv, left, top, bytes(rows)))

    offs = []
    for g in glyphs:
        offs.append(len(bitmap))
        bitmap += g[6]
    return {
        "name": name,
        "size": size,
        "ascent": ascent,
        "line": line_height,
        "glyphs": glyphs,
        "offs": offs,
        "bmp": bytes(bitmap),
    }


def c_array_bytes(name, data, per_line=16):
    out = ["const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), per_line):
        out.append("    " + ",".join("0x%02X" % b for b in data[i : i + per_line]) + ",")
    out.append("};")
    return "\n".join(out)


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


# ---------------------------------------------------------------- 输出
def main():
    books, words = load_words()
    src_text = scan_sources()
    charset = collect_charset(words, src_text)

    # 拉丁 / 音标 / 标点子集（非 CJK）
    latin = [c for c in charset if ord(c) < 0x2E80]
    full = charset

    print("册数=%d  单词数=%d  全字符集=%d  拉丁子集=%d" % (len(books), len(words), len(full), len(latin)))

    fonts = []
    for name, size, latin_only in SIZE_PLAN:
        chars = latin if latin_only else full
        f = render_font(name, size, chars)
        fonts.append(f)
        print("  %s size=%d glyphs=%d bitmap=%d bytes" % (name, size, len(f["glyphs"]), len(f["bmp"])))

    # ---------------- font_data.h
    with open(os.path.join(SRC, "font_data.h"), "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("struct Glyph {\n")
        f.write("    uint16_t cp;    // unicode codepoint\n")
        f.write("    uint8_t  w, h;  // 点阵尺寸\n")
        f.write("    uint8_t  adv;   // 步进宽度\n")
        f.write("    int8_t   left;  // 相对笔位左偏移\n")
        f.write("    int8_t   top;   // 相对基线顶部偏移\n")
        f.write("    uint16_t rsv;\n")
        f.write("    uint32_t off;   // 位图偏移\n")
        f.write("};\n\n")
        f.write("struct FontDef {\n")
        f.write("    const Glyph* glyphs;\n")
        f.write("    const uint8_t* bmp;\n")
        f.write("    uint16_t count;\n")
        f.write("    uint8_t  ascent;      // 基线距行顶\n")
        f.write("    uint8_t  lineHeight;\n")
        f.write("};\n\n")
        f.write("enum { FONT_12 = 0, FONT_16 = 1, FONT_24 = 2, FONT_COUNT = 3 };\n\n")
        f.write("extern const FontDef FONTS[FONT_COUNT];\n")

    # ---------------- font_data.cpp
    with open(os.path.join(SRC, "font_data.cpp"), "w", encoding="utf-8") as f:
        f.write('// 自动生成，请勿手改。由 tools/pack_words.py 生成\n#include "font_data.h"\n\n')
        for fd in fonts:
            n = fd["name"]
            f.write("static const Glyph %s_GLYPHS[%d] = {\n" % (n, len(fd["glyphs"])))
            for (cp, w, h, adv, left, top, _), off in zip(fd["glyphs"], fd["offs"]):
                f.write("    {0x%04X, %d, %d, %d, %d, %d, 0, %d},\n" % (cp, w, h, adv, left, top, off))
            f.write("};\n\n")
            if len(fd["bmp"]):
                f.write(c_array_bytes("%s_BMP" % n, fd["bmp"], 20) + "\n\n")
            else:
                f.write("static const uint8_t %s_BMP[1] = {0};\n\n" % n)
        f.write("const FontDef FONTS[FONT_COUNT] = {\n")
        for fd in fonts:
            f.write(
                "    {%s_GLYPHS, %s_BMP, %d, %d, %d},\n"
                % (fd["name"], fd["name"], len(fd["glyphs"]), fd["ascent"], fd["line"])
            )
        f.write("};\n")

    # ---------------- words_data.h
    with open(os.path.join(SRC, "words_data.h"), "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define WORD_COUNT %d\n" % len(words))
        f.write("#define BOOK_COUNT %d\n\n" % len(books))
        f.write("extern const char* const W_EN[WORD_COUNT];\n")
        f.write("extern const char* const W_PH[WORD_COUNT];\n")
        f.write("extern const char* const W_ZH[WORD_COUNT];\n")
        f.write("extern const uint8_t W_BOOK[WORD_COUNT];\n")
        f.write("extern const uint8_t W_UNIT[WORD_COUNT];\n")
        f.write("extern const uint8_t W_STAGE[WORD_COUNT];\n")
        f.write("extern const char* const BOOK_NAME[BOOK_COUNT];\n")
        f.write("extern const uint8_t BOOK_STAGE[BOOK_COUNT];\n")
        f.write("extern const uint16_t BOOK_WORDS[BOOK_COUNT];\n")

    # ---------------- words_data.cpp
    stage_of_book = [b["stage"] for b in books]
    book_count = [0] * len(books)
    for w in words:
        book_count[w["book"]] += 1

    with open(os.path.join(SRC, "words_data.cpp"), "w", encoding="utf-8") as f:
        f.write('// 自动生成，请勿手改。由 tools/pack_words.py 生成\n#include "words_data.h"\n\n')
        f.write("const char* const W_EN[WORD_COUNT] = {\n")
        for w in words:
            f.write('    "%s",\n' % esc(w["en"]))
        f.write("};\n\n")
        f.write("const char* const W_PH[WORD_COUNT] = {\n")
        for w in words:
            f.write('    "%s",\n' % esc(w["ph"]))
        f.write("};\n\n")
        f.write("const char* const W_ZH[WORD_COUNT] = {\n")
        for w in words:
            f.write('    "%s",\n' % esc(w["zh"]))
        f.write("};\n\n")

        def u8array(name, vals):
            f.write("const uint8_t %s[WORD_COUNT] = {" % name)
            for i in range(0, len(vals), 20):
                f.write("\n    " + ",".join(str(v) for v in vals[i : i + 20]) + ",")
            f.write("\n};\n\n")

        u8array("W_BOOK", [w["book"] for w in words])
        u8array("W_UNIT", [min(w["unit"], 255) for w in words])
        u8array("W_STAGE", [stage_of_book[w["book"]] for w in words])

        f.write("const char* const BOOK_NAME[BOOK_COUNT] = {\n")
        for b in books:
            f.write('    "%s",\n' % esc(b["title"]))
        f.write("};\n\n")
        f.write("const uint8_t BOOK_STAGE[BOOK_COUNT] = {\n    ")
        f.write(",".join(str(s) for s in stage_of_book) + "\n};\n\n")
        f.write("const uint16_t BOOK_WORDS[BOOK_COUNT] = {\n    ")
        f.write(",".join(str(c) for c in book_count) + "\n};\n")

    # ---------------- 统计信息
    spellable = sum(1 for w in words if is_spellable(w["en"]))
    print("可拼写单词(alpha only)=%d" % spellable)
    print("输出: src/words_data.cpp, src/font_data.cpp")


if __name__ == "__main__":
    main()
