#pragma once
#include <stdint.h>

namespace Saver {

enum Mode {
    M_CLOCK = 0,   // 数字时钟（默认）
    M_DIAL,        // 模拟表盘
    M_STAR,        // 星空穿越
    M_SNOW,        // 飘雪
    M_BOUNCE,      // 弹跳方块
    M_WAVE,        // 正弦波浪
    M_WORD,        // 单词轮播
    M_CAT,         // 猫咪眨眼
    M_COUNT
};

const char* name(int mode);
void enter();                 // 进入屏保，重置动画状态
void draw(int mode, uint32_t tMs);   // 全屏绘制（不含 header/footer）

// 纯计算（可单测）
uint32_t mix(uint32_t x);                 // xorshift，确定性随机
int      handX(int cx, int r, int minuteOfUnit, int units);
int      handY(int cy, int r, int minuteOfUnit, int units);
}
