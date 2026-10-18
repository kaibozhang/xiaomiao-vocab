#include "hal.h"
#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "lights.h"

Gfx gfx;

// ===========================================================================
//  显示驱动：ST7735 160x128（无硬件复位、无背光控制，走软件复位）
// ===========================================================================
static inline void dcCmd() { digitalWrite(PIN_LCD_DC, LOW); }
static inline void dcDat() { digitalWrite(PIN_LCD_DC, HIGH); }
static inline void csSel() { digitalWrite(PIN_LCD_CS, LOW); }
static inline void csRel() { digitalWrite(PIN_LCD_CS, HIGH); }

static void wrCmd(uint8_t c) {
    dcCmd(); csSel(); SPI.transfer(c); csRel();
}
static void wrDat(uint8_t d) {
    dcDat(); csSel(); SPI.transfer(d); csRel();
}
static void wrDatN(const uint8_t* p, size_t n) {
    dcDat(); csSel();
    for (size_t i = 0; i < n; i++) SPI.transfer(p[i]);
    csRel();
}

static const uint8_t GAMMA_P[] = {0x0F,0x1A,0x0F,0x18,0x2F,0x28,0x20,0x22,0x1F,0x1B,0x23,0x37,0x00,0x07,0x02,0x10};
static const uint8_t GAMMA_N[] = {0x0F,0x1B,0x0F,0x17,0x33,0x2C,0x29,0x2E,0x30,0x30,0x39,0x3F,0x00,0x07,0x03,0x10};

static void setWindow(int x, int y, int w, int h) {
    wrCmd(0x2A);
    wrDat(0); wrDat((uint8_t)x); wrDat(0); wrDat((uint8_t)(x + w - 1));
    wrCmd(0x2B);
    wrDat(0); wrDat((uint8_t)y); wrDat(0); wrDat((uint8_t)(y + h - 1));
    wrCmd(0x2C);
}

static void applyMadctl(uint8_t m) {
    wrCmd(0x36); wrDat(m);
}

void Gfx::init() {
    if (!fb) {
        fb = (uint16_t*)heap_caps_malloc(SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
        if (!fb) fb = (uint16_t*)malloc(SCR_W * SCR_H * 2);
    }
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);           // 让 TF 卡彻底离线
    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);
    pinMode(PIN_LCD_DC, OUTPUT);
    digitalWrite(PIN_LCD_DC, HIGH);

    SPI.begin(PIN_LCD_CLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
    SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));

    wrCmd(0x01); delay(150);                 // SWRESET
    wrCmd(0x11); delay(250);                 // SLPOUT
    wrCmd(0xB1); wrDatN((const uint8_t[]){0x01,0x2C,0x2D}, 3);
    wrCmd(0xB2); wrDatN((const uint8_t[]){0x01,0x2C,0x2D}, 3);
    wrCmd(0xB3); wrDatN((const uint8_t[]){0x01,0x2C,0x2D,0x01,0x2C,0x2D}, 6);
    wrCmd(0xB4); wrDat(0x07);                // INVCTR
    wrCmd(0xC0); wrDatN((const uint8_t[]){0xA2,0x02,0x84}, 3);
    wrCmd(0xC1); wrDat(0xC5);
    wrCmd(0xC2); wrDatN((const uint8_t[]){0x0A,0x00}, 2);
    wrCmd(0xC3); wrDatN((const uint8_t[]){0x8A,0x2A}, 2);
    wrCmd(0xC4); wrDatN((const uint8_t[]){0x8A,0xEE}, 2);
    wrCmd(0xC5); wrDat(0x0E);                // VMCTR1
    wrCmd(0x20);                             // INVOFF
    applyMadctl(madctl);
    wrCmd(0x3A); wrDat(0x05);                // COLMOD 16bit
    wrCmd(0xE0); wrDatN(GAMMA_P, 16);
    wrCmd(0xE1); wrDatN(GAMMA_N, 16);
    wrCmd(0x13); delay(10);                  // NORON
    wrCmd(0x29); delay(100);                 // DISPON

    clear(C_BG);
    flush();
}

void Gfx::setOrientation(uint8_t mode) {
    uint8_t base;
    switch (mode) {
        case 1:  base = 0x20; break;  // 去掉 MX：水平镜像
        case 2:  base = 0xE0; break;  // 加 MY：上下翻转
        case 3:  base = 0xA0; break;  // 180
        default: base = 0x60; break;  // 与 Retro-Go 一致
    }
    madctl = base | (madctl & 0x08);
    applyMadctl(madctl);
}

void Gfx::setBgr(bool on) {
    madctl = (madctl & ~0x08) | (on ? 0x08 : 0x00);
    applyMadctl(madctl);
}

void Gfx::clear(uint16_t c) {
    uint32_t v = ((uint32_t)c << 16) | c;
    uint32_t* p = (uint32_t*)fb;
    for (int i = 0; i < SCR_W * SCR_H / 2; i++) p[i] = v;
}

void Gfx::fillRect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t* row = fb + yy * SCR_W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = c;
    }
}

void Gfx::drawRect(int x, int y, int w, int h, uint16_t c) {
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void Gfx::hline(int x, int y, int w, uint16_t c) {
    if (y < 0 || y >= SCR_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    uint16_t* row = fb + y * SCR_W + x;
    for (int i = 0; i < w; i++) row[i] = c;
}

void Gfx::vline(int x, int y, int h, uint16_t c) {
    if (x < 0 || x >= SCR_W) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > SCR_H) h = SCR_H - y;
    for (int i = 0; i < h; i++) fb[(y + i) * SCR_W + x] = c;
}

void Gfx::pixel(int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    fb[y * SCR_W + x] = c;
}

void Gfx::line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Gfx::circle(int cx, int cy, int r, uint16_t c) {
    int x = -r, y = 0, err = 2 - 2 * r;
    do {
        pixel(cx - x, cy + y, c);
        pixel(cx + x, cy + y, c);
        pixel(cx + x, cy - y, c);
        pixel(cx - x, cy - y, c);
        pixel(cx + y, cy + x, c);
        pixel(cx - y, cy + x, c);
        pixel(cx - y, cy - x, c);
        pixel(cx + y, cy - x, c);
        int e2 = err;
        if (e2 <= y) err += ++y * 2 + 1;
        if (e2 > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}

void Gfx::disc(int cx, int cy, int r, uint16_t c) {
    for (int y = -r; y <= r; y++) {
        int dx = (int)(r * r - y * y);
        int w = dx > 0 ? (int)(sqrtf((float)dx)) : 0;
        hline(cx - w, cy + y, 2 * w + 1, c);
    }
}

static inline int orient2(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

void Gfx::tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = x0 < x1 ? x0 : x1; minx = minx < x2 ? minx : x2;
    int maxx = x0 > x1 ? x0 : x1; maxx = maxx > x2 ? maxx : x2;
    int miny = y0 < y1 ? y0 : y1; miny = miny < y2 ? miny : y2;
    int maxy = y0 > y1 ? y0 : y1; maxy = maxy > y2 ? maxy : y2;
    int a0 = orient2(x0, y0, x1, y1, x2, y2);
    if (a0 == 0) return;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int w0 = orient2(x0, y0, x1, y1, x, y);
            int w1 = orient2(x1, y1, x2, y2, x, y);
            int w2 = orient2(x2, y2, x0, y0, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                pixel(x, y, c);
        }
    }
}

// ---------------------------------------------------------------- 文本绘制
static uint32_t utf8Next(const char*& s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    int n; uint32_t v;
    if ((c & 0xE0) == 0xC0)      { v = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { v = c & 0x07; n = 3; }
    else return c;
    for (int i = 0; i < n && *s; i++) v = (v << 6) | ((uint8_t)*s++ & 0x3F);
    return v;
}

const Glyph* Gfx::findGlyph(const FontDef& f, uint16_t cp) {
    int lo = 0, hi = (int)f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t m = f.glyphs[mid].cp;
        if (m == cp) return &f.glyphs[mid];
        if (m < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

static int chWidth(const FontDef& f, uint32_t cp, const Glyph* g) {
    if (!g) return f.ascent / 2 + 1;
    return g->adv;
}

int Gfx::textWidth(const char* s, int font) {
    const FontDef& f = FONTS[font];
    int w = 0;
    while (*s) {
        uint32_t cp = utf8Next(s);
        w += chWidth(f, cp, findGlyph(f, cp));
    }
    return w;
}

int Gfx::textLen(const char* s) {
    int n = 0;
    while (*s) { utf8Next(s); n++; }
    return n;
}

int Gfx::text(const char* s, int x, int y, uint16_t col, int font) {
    const FontDef& f = FONTS[font];
    int baseline = y + f.ascent;
    int pen = x;
    while (*s) {
        uint32_t cp = utf8Next(s);
        const Glyph* g = findGlyph(f, cp);
        if (!g) { pen += f.ascent / 2 + 1; continue; }
        int stride = (g->w + 7) >> 3;
        const uint8_t* bmp = f.bmp + g->off;
        int topClip = y;                 // 行盒顶（禁止越过）
        int botClip = y + f.lineHeight;  // 行盒底
        for (int ry = 0; ry < g->h; ry++) {
            int py = baseline + g->top + ry;
            if (py < topClip || py >= botClip) continue;  // 裁剪到本行内，防止跨行遮挡
            const uint8_t* rowp = bmp + ry * stride;
            for (int rx = 0; rx < g->w; rx++) {
                if (rowp[rx >> 3] & (1 << (7 - (rx & 7)))) {
                    int px = pen + g->left + rx;
                    if (px >= 0 && px < SCR_W) fb[py * SCR_W + px] = col;
                }
            }
        }
        pen += g->adv;
    }
    return pen - x;
}

int Gfx::textMax(const char* s, int x, int y, int maxw, uint16_t col, int font) {
    const FontDef& f = FONTS[font];
    int full = textWidth(s, font);
    if (full <= maxw) return text(s, x, y, col, font);
    // 逐字累加，留出一个 "…" 的位置
    int ell = chWidth(f, 0x2026, findGlyph(f, 0x2026));
    int w = 0;
    const char* p = s;
    const char* q = s;
    int bytes = 0;
    while (*q) {
        const char* r = q;
        uint32_t cp = utf8Next(r);
        int aw = chWidth(f, cp, findGlyph(f, cp));
        if (w + aw + ell > maxw) break;
        w += aw; q = r; bytes = (int)(q - p);
    }
    char tmp[256];
    int n = bytes < 255 ? bytes : 255;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    int used = text(tmp, x, y, col, font);
    text("…", x + used, y, col, font);
    return used + ell;
}

int Gfx::textCenter(const char* s, int y, uint16_t col, int font) {
    int w = textWidth(s, font);
    int x = (SCR_W - w) / 2;
    if (x < 0) x = 0;
    return text(s, x, y, col, font);
}

int Gfx::textCenter2(const char* s, int x, int y, int w, uint16_t col, int font) {
    int tw = textWidth(s, font);
    int px = x + (w - tw) / 2;
    if (px < 0) px = 0;
    return text(s, px, y, col, font);
}

int Gfx::textRight(const char* s, int xEnd, int y, uint16_t col, int font) {
    int w = textWidth(s, font);
    return text(s, xEnd - w, y, col, font);
}

int Gfx::textWrap(const char* s, int x, int y, int maxw, uint16_t col, int font, int lineh, int maxLines) {
    const FontDef& f = FONTS[font];
    int lines = 0;
    const char* p = s;
    char tmp[256];
    while (*p && lines < maxLines) {
        int w = 0, bytes = 0;
        const char* q = p;
        while (*q) {
            const char* r = q;
            uint32_t cp = utf8Next(r);
            int aw = chWidth(f, cp, findGlyph(f, cp));
            if (w + aw > maxw && bytes > 0) break;
            w += aw; q = r; bytes = (int)(q - p);
        }
        if (bytes == 0) { utf8Next(q); bytes = (int)(q - p); }
        int n = bytes < 255 ? bytes : 255;
        memcpy(tmp, p, n); tmp[n] = 0;
        text(tmp, x, y + lines * lineh, col, font);
        p += bytes;
        lines++;
    }
    return lines;
}

void Gfx::flush() {
    setWindow(0, 0, SCR_W, SCR_H);
    static uint16_t line[SCR_W];
    dcDat(); csSel();
    for (int y = 0; y < SCR_H; y++) {
        const uint16_t* src = fb + y * SCR_W;
        for (int x = 0; x < SCR_W; x++) {
            uint16_t v = src[x];
            line[x] = (uint16_t)((v >> 8) | (v << 8));   // 转成大端线序
        }
        SPI.writeBytes((const uint8_t*)line, SCR_W * 2);
    }
    csRel();
}

// ===========================================================================
//  按键
// ===========================================================================
namespace Input {
static const uint8_t PINS[6] = {PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT, PIN_A, PIN_B};
static uint8_t cur = 0, prev = 0;
static uint32_t nextRepeat[6];

void init() {
    for (int i = 0; i < 6; i++) {
        uint8_t p = PINS[i];
        if (p == 34 || p == 35) pinMode(p, INPUT);   // 输入专用脚，无内部上拉
        else pinMode(p, INPUT_PULLUP);
    }
}

uint8_t poll() {
    uint8_t raw = 0;
    for (int i = 0; i < 6; i++) {
        if (digitalRead(PINS[i]) == LOW) raw |= (1u << i);
    }
    uint32_t now = millis();
    uint8_t edge = raw & ~prev;
    for (int i = 0; i < 6; i++) {
        uint8_t bit = (1u << i);
        if (raw & bit) {
            if (prev & bit) {
                if (nextRepeat[i] && now >= nextRepeat[i]) {
                    edge |= bit;
                    nextRepeat[i] = now + 110;
                }
            } else {
                nextRepeat[i] = now + 420;   // 长按 420ms 后开始连发
            }
        } else {
            nextRepeat[i] = 0;
        }
    }
    prev = raw;
    cur = raw;
    return edge;
}

uint8_t held() { return cur; }
}  // namespace Input

// ===========================================================================
//  蜂鸣器（GPIO14 无源蜂鸣器，LEDC PWM）
// ===========================================================================
namespace Sound {
static bool ready = false;
static bool on = true;

void init() {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 2000;
    t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) return;

    ledc_channel_config_t c = {};
    c.gpio_num = PIN_BUZZER;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.hpoint = 0;
    if (ledc_channel_config(&c) != ESP_OK) return;
    ready = true;
}

void setEnabled(bool en) { on = en; }
bool enabled() { return on; }

void tone(uint32_t freq, uint32_t ms) {
    if (!ready || !on) return;              // 非阻塞：关音效时直接返回
    if (freq) ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    delay(ms);                               // 发声时长（无源蜂鸣器需持续 PWM）
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void click() { if (on) tone(2400, 12); else Lights::click(); }
void ok()    { if (on) { tone(1568, 70); delay(20); tone(2093, 90); } else Lights::ok(); }
void err()   { if (on) tone(220, 200); else Lights::err(); }
}  // namespace Sound
