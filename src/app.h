#pragma once
#include <stdint.h>

namespace App {
void init();
void update(uint8_t keys);
void render();
bool consumeDirty();

void tick(uint32_t nowMs);     // 屏保空闲检测 + 动画帧
void noteActivity();
bool inScreensaver();

void debugKey(uint8_t mask);   // 串口注入按键（发送 k <掩码>）
int  screenId();               // 当前屏幕编号（自检用）

void selfTest();               // 串口自检（发送 t）
void selfTestSaver();          // 串口屏保自检（发送 e）
void gameDump();               // 串口 dump 闯关内部状态（发送 g）
}
