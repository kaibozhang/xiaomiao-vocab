#pragma once
#include <stdint.h>
#include "config.h"
#include "font_data.h"

// ---------------------------------------------------------------------------
// 显示
// ---------------------------------------------------------------------------
class Gfx {
public:
    void init();
    void setOrientation(uint8_t mode);  // 0 默认 1 水平镜像 2 上下翻转 3 旋转180
    void setBgr(bool on);

    void clear(uint16_t c);
    void fillRect(int x, int y, int w, int h, uint16_t c);
    void drawRect(int x, int y, int w, int h, uint16_t c);
    void hline(int x, int y, int w, uint16_t c);
    void vline(int x, int y, int h, uint16_t c);
    void pixel(int x, int y, uint16_t c);
    void line(int x0, int y0, int x1, int y1, uint16_t c);
    void circle(int cx, int cy, int r, uint16_t c);
    void disc(int cx, int cy, int r, uint16_t c);
    void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

    int  textWidth(const char* s, int font);
    int  textLen(const char* s);                       // 字符数（非字节）
    int  text(const char* s, int x, int y, uint16_t c, int font);      // 返回宽度
    int  textMax(const char* s, int x, int y, int maxw, uint16_t c, int font); // 超宽截断层叠…
    int  textCenter(const char* s, int y, uint16_t c, int font);
    int  textCenter2(const char* s, int x, int y, int w, uint16_t c, int font);
    int  textRight(const char* s, int xEnd, int y, uint16_t c, int font);
    int  textWrap(const char* s, int x, int y, int maxw, uint16_t c, int font, int lineh, int maxLines);
    void flush();

private:
    uint16_t* fb = nullptr;
    uint8_t  madctl = 0x60;
    const Glyph* findGlyph(const FontDef& f, uint16_t cp);
};

extern Gfx gfx;

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
enum {
    BTN_UP = 1u << 0,
    BTN_DOWN = 1u << 1,
    BTN_LEFT = 1u << 2,
    BTN_RIGHT = 1u << 3,
    BTN_A = 1u << 4,
    BTN_B = 1u << 5,
};

namespace Input {
void init();
uint8_t poll();      // 返回「本帧新按下」的位掩码（含长按重复）
uint8_t held();      // 当前按住的位掩码
}

// ---------------------------------------------------------------------------
// 蜂鸣器
// ---------------------------------------------------------------------------
namespace Sound {
void init();
void setEnabled(bool on);
bool enabled();
void tone(uint32_t freq, uint32_t ms);
void click();   // 移动光标
void ok();      // 答对
void err();     // 答错
}
