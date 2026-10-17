#pragma once
#include <stdint.h>

struct Glyph {
    uint16_t cp;    // unicode codepoint
    uint8_t  w, h;  // 点阵尺寸
    uint8_t  adv;   // 步进宽度
    int8_t   left;  // 相对笔位左偏移
    int8_t   top;   // 相对基线顶部偏移
    uint16_t rsv;
    uint32_t off;   // 位图偏移
};

struct FontDef {
    const Glyph* glyphs;
    const uint8_t* bmp;
    uint16_t count;
    uint8_t  ascent;      // 基线距行顶
    uint8_t  lineHeight;
};

enum { FONT_12 = 0, FONT_16 = 1, FONT_24 = 2, FONT_COUNT = 3 };

extern const FontDef FONTS[FONT_COUNT];
