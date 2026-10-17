#include "power.h"
#include "store.h"
#include <Arduino.h>

namespace Power {

// 本机（学而思小喵掌机）没有电池电压检测电路，只有光照 GPIO36 / 热敏 GPIO39
// 两个 ADC。所以电量默认走"估算"：以「上次标记满电以来的开机时长」推算。
#define FULL_MINUTES 600        // 满电约 10 小时续航
#define ADC_DIVIDER  2.0f       // 外部分压比（1:1 电阻），可在设置里校准

static int  lastPct = -1;
static uint32_t minuteMark = 0;
static int  mvCache = -1;

const char* modeName(int mode) {
    switch (mode) {
        case 0: return "估算";
        case 1: return "扩展口";
        default: return "隐藏";
    }
}

int estimatePercent(uint32_t usedMinutes) {
    if (usedMinutes >= FULL_MINUTES) return 0;
    int p = 100 - (int)((usedMinutes * 100u) / FULL_MINUTES);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

void begin() {
    minuteMark = millis();
    if (Store::cfg.battMode == 1) {
        pinMode(Store::cfg.battPin, INPUT);
        analogSetAttenuation(ADC_11db);
        analogReadResolution(12);
    }
}

void tick() {
    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - minuteMark) >= 60000) {
        minuteMark = nowMs;
        if (Store::cfg.usedMinutes < 0xFFFFFFFFu) Store::cfg.usedMinutes++;
        Store::markDirty();
    }
    if (Store::cfg.battMode == 1) {
        int raw = analogRead(Store::cfg.battPin);
        // ESP32 ADC 非线性，这里只做线性近似
        float v = (float)raw / 4095.0f * 3.3f * ADC_DIVIDER;
        mvCache = (int)(v * 1000.0f);
    } else {
        mvCache = -1;
    }
}

int millivolts() { return mvCache; }

int percent() {
    if (Store::cfg.battMode == 2) return -1;
    if (Store::cfg.battMode == 1 && mvCache > 0) {
        int full = Store::cfg.battFullMv > 0 ? Store::cfg.battFullMv : 4200;
        int empty = Store::cfg.battEmptyMv > 0 ? Store::cfg.battEmptyMv : 3300;
        if (full <= empty) return 0;
        int p = (mvCache - empty) * 100 / (full - empty);
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        return p;
    }
    return estimatePercent(Store::cfg.usedMinutes);
}

bool charging() { return false; }

void markFull() {
    Store::cfg.usedMinutes = 0;
    Store::markDirty();
    Store::save();
}

}  // namespace Power
