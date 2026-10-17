#pragma once
#include <stdint.h>

namespace StatusBar {
// 电池图标：宽 15，高 9；x 为左端
void drawBattery(int x, int y, int pct);
// 时间文本（右对齐到 xEnd）
int  drawClock(int xEnd, int y, uint32_t unixTs, bool hour24, uint16_t color);
int  statusWidth();   // 电量 + 时间 占用的总宽度
}
