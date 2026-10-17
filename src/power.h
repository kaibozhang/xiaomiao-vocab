#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace Power {
void begin();
void tick();

// 返回 0..100；-1 表示隐藏电量显示
int  percent();
bool charging();          // 本机无充电检测，恒为 false（保留接口）
int  millivolts();        // -1 表示未知
void markFull();          // 标记刚充满（估算模式下把用量清零）
const char* modeName(int mode);
int  estimatePercent(uint32_t usedMinutes);   // 纯函数，可单测
}
