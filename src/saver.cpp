#include "saver.h"
#include "hal.h"
#include "config.h"
#include "clock.h"
#include "power.h"
#include "store.h"
#include "words.h"
#include "status.h"
#include "net.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace Saver {

const char* name(int mode) {
    switch (mode) {
        case M_CLOCK:  return "数字时钟";
        case M_DIAL:   return "模拟表盘";
        case M_STAR:   return "星空穿越";
        case M_SNOW:   return "飘雪";
        case M_BOUNCE: return "弹跳方块";
        case M_WAVE:   return "水波";
        case M_WORD:   return "单词轮播";
        case M_CAT:    return "猫咪";
        default:       return "?";
    }
}

uint32_t mix(uint32_t x) {
    x ^= x << 13; x &= 0xFFFFFFFFu;
    x ^= x >> 17;
    x ^= x << 5;  x &= 0xFFFFFFFFu;
    return x;
}

int handX(int cx, int r, int v, int units) {
    float a = (float)v / (float)units * 6.2831853f - 1.5707963f;
    return cx + (int)(r * cosf(a));
}
int handY(int cy, int r, int v, int units) {
    float a = (float)v / (float)units * 6.2831853f - 1.5707963f;
    return cy + (int)(r * sinf(a));
}

// ---------------------------------------------------------------- 动画状态
#define STARS 70
#define FLAKES 46
static struct { float x, y, z; } stars[STARS];
static struct { float x, y, v, ph; } flakes[FLAKES];
static float bx = 20, by = 30, bvx = 0.9f, bvy = 0.7f;
static uint32_t seed = 0x12345678;
static bool inited = false;
static int wordIdx = -1;
static uint32_t wordAt = 0;

static uint32_t rnd() { seed = mix(seed); return seed; }

void enter() {
    inited = false;
    wordIdx = -1;
    wordAt = 0;
}

static void ensure(uint32_t tMs) {
    (void)tMs;
    if (inited) return;
    inited = true;
    for (int i = 0; i < STARS; i++) {
        stars[i].x = (float)((int)(rnd() % 1600) - 800) / 10.0f;
        stars[i].y = (float)((int)(rnd() % 1280) - 640) / 10.0f;
        stars[i].z = 1.0f + (float)(rnd() % 600) / 10.0f;
    }
    for (int i = 0; i < FLAKES; i++) {
        flakes[i].x = (float)(rnd() % SCR_W);
        flakes[i].y = (float)(rnd() % SCR_H);
        flakes[i].v = 0.25f + (float)(rnd() % 60) / 100.0f;
        flakes[i].ph = (float)(rnd() % 628) / 100.0f;
    }
    bx = 24; by = 34; bvx = 0.85f; bvy = 0.62f;
}

static void footerInfo(int y) {
    char b[40];
    snprintf(b, sizeof(b), "复习 %d  新词 %d", Store::countDue(), Store::countNew());
    gfx.textCenter(b, y, C_DIM, FONT_12);
}

// ---------------------------------------------------------------- 各屏保
static void drawClock(uint32_t tMs) {
    (void)tMs;
    char big[12], sec[8], d[24];
    uint32_t u = Clock::now();
    if (u == 0) {
        gfx.textCenter("未设置时间", 20, C_DIM, FONT_12);
        snprintf(big, sizeof(big), "--:--");
        sec[0] = 0;
        snprintf(d, sizeof(d), "连 WiFi 后自动对时");
    } else {
        struct tm p;
        time_t tt = (time_t)u;
        localtime_r(&tt, &p);
        snprintf(big, sizeof(big), "%02d:%02d", p.tm_hour, p.tm_min);
        snprintf(sec, sizeof(sec), ":%02d", p.tm_sec);
        Clock::fmtDateCn(u, d, sizeof(d));
    }
    gfx.textCenter(d, 16, C_DIM, FONT_12);
    int w = gfx.textWidth(big, FONT_24);
    int x = (SCR_W - w) / 2;
    gfx.text(big, x, 32, C_FG, FONT_24);
    gfx.text(sec, x + w + 1, 44, C_ACCENT, FONT_12);

    // 秒进度条
    int sw = 120, sx = (SCR_W - sw) / 2;
    gfx.drawRect(sx, 66, sw, 5, C_PANEL);
    if (u) gfx.fillRect(sx + 1, 67, (sw - 2) * Clock::second() / 59, 3, C_ACCENT);

    // 电量
    int pct = Power::percent();
    if (pct >= 0) {
        char b[16];
        snprintf(b, sizeof(b), "%d%%", pct);
        StatusBar::drawBattery(24, 82, pct);
        gfx.text(b, 42, 81, C_DIM, FONT_12);
    }
    footerInfo(100);
    if (!Store::cfg.ssid[0]) gfx.textCenter("设置 → WiFi 开启网络", 114, C_DIM, FONT_12);
    else if (!Net::connected()) gfx.textCenter("WiFi 连接中…", 114, C_DIM, FONT_12);
    else gfx.textCenter("xiaomiao.local", 114, C_ACCENT, FONT_12);
}

static void drawDial(uint32_t tMs) {
    (void)tMs;
    int cx = SCR_W / 2, cy = 58, r = 44;
    gfx.circle(cx, cy, r, C_DIM);
    gfx.circle(cx, cy, r - 1, C_BAR);
    for (int i = 0; i < 12; i++) {
        int x1 = handX(cx, r - 2, i, 12), y1 = handY(cy, r - 2, i, 12);
        int x0 = handX(cx, r - (i % 3 == 0 ? 8 : 5), i, 12);
        int y0 = handY(cy, r - (i % 3 == 0 ? 8 : 5), i, 12);
        gfx.line(x0, y0, x1, y1, i % 3 == 0 ? C_FG : C_DIM);
    }
    uint32_t u = Clock::now();
    int h = Clock::hour(), m = Clock::minute(), s = Clock::second();
    if (u == 0) { h = 10; m = 10; s = 0; }
    // 时针按 12 小时格，加上分钟偏移
    int hi = (h % 12) * 5 + m / 12;
    gfx.line(cx, cy, handX(cx, 24, hi, 60), handY(cy, 24, hi, 60), C_FG);
    gfx.line(cx, cy, handX(cx, 34, m, 60), handY(cy, 34, m, 60), C_ACCENT);
    gfx.line(cx, cy, handX(cx, 40, s, 60), handY(cy, 40, s, 60), C_WARN);
    gfx.disc(cx, cy, 2, C_FG);
    if (u == 0) gfx.textCenter("未对时", 20, C_DIM, FONT_12);
    else {
        char d[28];
        Clock::fmtDateCn(u, d, sizeof(d));
        gfx.textCenter(d, 20, C_DIM, FONT_12);
    }
    char t[16];
    Clock::fmtTime(u, t, sizeof(t), true);
    gfx.textCenter(t, 104, C_FG, FONT_12);
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(SCR_W - 18, 112, pct);
    footerInfo(126 - 8);
}

static void drawStar(uint32_t tMs) {
    (void)tMs;
    int cx = SCR_W / 2, cy = SCR_H / 2;
    for (int i = 0; i < STARS; i++) {
        stars[i].z -= 0.55f;
        if (stars[i].z <= 0.6f) {
            stars[i].x = (float)((int)(rnd() % 1600) - 800) / 10.0f;
            stars[i].y = (float)((int)(rnd() % 1280) - 640) / 10.0f;
            stars[i].z = 60.0f;
        }
        float k = 42.0f / stars[i].z;
        int sx = cx + (int)(stars[i].x * k);
        int sy = cy + (int)(stars[i].y * k);
        if (sx < 0 || sx >= SCR_W || sy < 0 || sy >= SCR_H) continue;
        int b = (int)(60.0f - stars[i].z);
        if (b < 0) b = 0;
        if (b > 4) b = 4;
        static const uint16_t COL[5] = {0x630C, 0x8410, 0xB596, 0xDEDB, 0xFFFF};
        gfx.pixel(sx, sy, COL[b]);
        if (b >= 3) gfx.pixel(sx + 1, sy, COL[b]);
    }
    char t[16];
    uint32_t u = Clock::now();
    if (u) {
        Clock::fmtTime(u, t, sizeof(t), true);
        gfx.textRight(t, SCR_W - 3, 3, C_DIM, FONT_12);
    }
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(3, 3, pct);
}

static void drawSnow(uint32_t tMs) {
    (void)tMs;
    for (int i = 0; i < FLAKES; i++) {
        flakes[i].y += flakes[i].v;
        flakes[i].ph += 0.03f;
        flakes[i].x += sinf(flakes[i].ph) * 0.35f;
        if (flakes[i].y > SCR_H) { flakes[i].y = -1; flakes[i].x = (float)(rnd() % SCR_W); }
        if (flakes[i].x < 0) flakes[i].x += SCR_W;
        if (flakes[i].x >= SCR_W) flakes[i].x -= SCR_W;
        int x = (int)flakes[i].x, y = (int)flakes[i].y;
        gfx.pixel(x, y, 0xFFFF);
        if (flakes[i].v > 0.6f) gfx.pixel(x, y - 1, 0xC618);
    }
    // 底部积雪
    for (int x = 0; x < SCR_W; x++) {
        int h = 3 + (int)(2.0f * sinf(x * 0.18f));
        for (int y = 0; y < h; y++) gfx.pixel(x, SCR_H - 1 - y, 0xE71C);
    }
    char t[16];
    uint32_t u = Clock::now();
    if (u) {
        Clock::fmtTime(u, t, sizeof(t), true);
        gfx.textRight(t, SCR_W - 3, 3, C_DIM, FONT_12);
    }
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(3, 3, pct);
}

static void drawBounce(uint32_t tMs) {
    (void)tMs;
    int bw = 38, bh = 16;
    bx += bvx; by += bvy;
    if (bx < 0) { bx = 0; bvx = -bvx; }
    if (bx + bw > SCR_W) { bx = SCR_W - bw; bvx = -bvx; }
    if (by < 0) { by = 0; bvy = -bvy; }
    if (by + bh > SCR_H) { by = SCR_H - bh; bvy = -bvy; }
    static const uint16_t PAL[6] = {0x0655, 0x3E4A, 0xFE47, 0xE228, 0x2AD9, 0xA31F};
    uint16_t c = PAL[(millis() / 900) % 6];
    gfx.fillRect((int)bx, (int)by, bw, bh, C_PANEL);
    gfx.drawRect((int)bx, (int)by, bw, bh, c);
    gfx.textCenter("小喵", (int)by + 2, c, FONT_12);
    char t[16];
    uint32_t u = Clock::now();
    if (u) {
        Clock::fmtTime(u, t, sizeof(t), true);
        gfx.textCenter(t, 4, C_DIM, FONT_12);
    }
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(3, SCR_H - 12, pct);
}

static void drawWave(uint32_t tMs) {
    float ph = (float)tMs / 260.0f;
    static const uint16_t COL[3] = {0x0655, 0x2AD9, 0x3E4A};
    for (int w = 0; w < 3; w++) {
        int cy = 40 + w * 22;
        float amp = 10.0f - w * 2.0f;
        float f = 0.055f + w * 0.012f;
        for (int x = 0; x < SCR_W; x += 2) {
            int y = cy + (int)(amp * sinf(x * f + ph + w));
            if (y < 0 || y >= SCR_H) continue;
            gfx.pixel(x, y, COL[w]);
            gfx.pixel(x, y + 1, COL[w]);
        }
    }
    char t[16];
    uint32_t u = Clock::now();
    if (u) {
        Clock::fmtTime(u, t, sizeof(t), true);
        gfx.textCenter(t, 100, C_FG, FONT_12);
    }
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(3, 3, pct);
}

static void drawWord(uint32_t tMs) {
    if (wordIdx < 0 || (uint32_t)(tMs - wordAt) > 5000) {
        wordAt = tMs;
        int n = ws_count();
        int tries = 0;
        do {
            wordIdx = (int)(rnd() % (uint32_t)n);
            tries++;
        } while (tries < 24 && !Store::bookEnabled(ws_book(wordIdx)));
    }
    int i = wordIdx;
    if (i < 0 || i >= ws_count()) i = 0;
    gfx.textCenter(ws_en(i), 26, C_FG, gfx.textWidth(ws_en(i), FONT_24) <= SCR_W - 12 ? FONT_24 : FONT_16);
    if (ws_ph(i)[0]) gfx.textCenter(ws_ph(i), 54, C_ACCENT, FONT_12);
    gfx.textWrap(ws_zh(i), 4, 74, SCR_W - 8, C_FG, FONT_12, 15, 2);
    char t[16];
    uint32_t u = Clock::now();
    if (u) {
        Clock::fmtTime(u, t, sizeof(t), true);
        gfx.textRight(t, SCR_W - 3, 3, C_DIM, FONT_12);
    }
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(3, 3, pct);
    // 剩余切换时间提示
    int left = 5 - (int)((tMs - wordAt) / 1000);
    if (left < 1) left = 1;
    char b[12];
    snprintf(b, sizeof(b), "%ds", left);
    gfx.textRight(b, SCR_W - 3, 114, C_DIM, FONT_12);
}

static void drawCat(uint32_t tMs) {
    int cx = SCR_W / 2, cy = 62;
    uint16_t fur = 0xFE47;      // 暖黄
    uint16_t dark = 0xB345;
    // 耳朵
    gfx.tri(cx - 26, cy - 22, cx - 10, cy - 40, cx - 6, cy - 16, fur);
    gfx.tri(cx + 26, cy - 22, cx + 10, cy - 40, cx + 6, cy - 16, fur);
    gfx.tri(cx - 22, cy - 22, cx - 13, cy - 33, cx - 11, cy - 19, 0xF800);
    gfx.tri(cx + 22, cy - 22, cx + 13, cy - 33, cx + 11, cy - 19, 0xF800);
    // 头
    gfx.disc(cx, cy, 28, fur);
    gfx.disc(cx, cy + 4, 26, 0xFEC0);
    // 眼睛（每 3 秒眨一次）
    int ph = (int)(tMs % 3000);
    bool blink = ph < 160;
    for (int s = -1; s <= 1; s += 2) {
        int ex = cx + s * 11, ey = cy - 4;
        if (blink) {
            gfx.hline(ex - 4, ey, 9, dark);
            gfx.hline(ex - 4, ey + 1, 9, dark);
        } else {
            gfx.disc(ex, ey, 4, 0x0000);
            gfx.disc(ex + 1, ey - 1, 1, 0xFFFF);
        }
    }
    // 鼻子 + 嘴
    gfx.tri(cx - 3, cy + 5, cx + 3, cy + 5, cx, cy + 9, 0xF800);
    gfx.line(cx, cy + 9, cx, cy + 12, dark);
    gfx.line(cx - 6, cy + 12, cx, cy + 9, dark);
    gfx.line(cx + 6, cy + 12, cx, cy + 9, dark);
    // 胡须
    for (int k = -1; k <= 1; k++) {
        gfx.line(cx - 12, cy + 6 + k * 3, cx - 30, cy + 3 + k * 4, C_DIM);
        gfx.line(cx + 12, cy + 6 + k * 3, cx + 30, cy + 3 + k * 4, C_DIM);
    }
    char t[16];
    uint32_t u = Clock::now();
    if (u == 0) snprintf(t, sizeof(t), "--:--");
    else Clock::fmtTime(u, t, sizeof(t), true);
    gfx.textCenter(t, 108, C_ACCENT, FONT_12);
    int pct = Power::percent();
    if (pct >= 0) StatusBar::drawBattery(SCR_W - 18, 2, pct);
    gfx.text("喵~", 4, 2, C_DIM, FONT_12);
}

void draw(int mode, uint32_t tMs) {
    ensure(tMs);
    switch (mode) {
        case M_DIAL:   drawDial(tMs); break;
        case M_STAR:   drawStar(tMs); break;
        case M_SNOW:   drawSnow(tMs); break;
        case M_BOUNCE: drawBounce(tMs); break;
        case M_WAVE:   drawWave(tMs); break;
        case M_WORD:   drawWord(tMs); break;
        case M_CAT:    drawCat(tMs); break;
        default:       drawClock(tMs); break;
    }
}

}  // namespace Saver
