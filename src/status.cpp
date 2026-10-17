#include "status.h"
#include "hal.h"
#include "clock.h"
#include "config.h"
#include <stdio.h>

namespace StatusBar {

#define BAT_W 13
#define BAT_H 8

void drawBattery(int x, int y, int pct) {
    uint16_t frame = C_DIM;
    gfx.drawRect(x, y, BAT_W, BAT_H, frame);
    gfx.fillRect(x + BAT_W, y + 2, 2, 4, frame);
    if (pct < 0) {                       // 隐藏：画个问号条
        gfx.fillRect(x + 2, y + 2, BAT_W - 4, BAT_H - 4, C_DIM);
        return;
    }
    if (pct > 100) pct = 100;
    int fill = (BAT_W - 4) * pct / 100;
    uint16_t col = (pct <= 15) ? C_ERR : (pct <= 35 ? C_WARN : C_OK);
    if (fill > 0) gfx.fillRect(x + 2, y + 2, fill, BAT_H - 4, col);
    else gfx.fillRect(x + 2, y + 2, 1, BAT_H - 4, col);
}

int drawClock(int xEnd, int y, uint32_t unixTs, bool hour24, uint16_t color) {
    char t[16];
    if (unixTs == 0) {
        snprintf(t, sizeof(t), "--:--");
    } else {
        Clock::fmtTime(unixTs, t, sizeof(t), hour24);
    }
    return gfx.textRight(t, xEnd, y, color, FONT_12);
}

int statusWidth() {
    int w = BAT_W + 2 + 3;               // 电池
    w += gfx.textWidth("00:00", FONT_12);
    return w + 2;
}

}  // namespace StatusBar
