#include "lights.h"
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// 小喵掌机 I2C：SCL=GPIO15, SDA=GPIO21，马达/LED 控制器地址 0x40
// 灯的寄存器：0xA0（写 1 开 / 0 关，见官方标注图）
static const uint8_t LIGHT_ADDR = 0x40;
static const uint8_t LIGHT_REG  = 0xA0;

namespace Lights {

static bool present = false;
static uint8_t curMask = 0;

// 非阻塞闪烁调度器：pulse() 只登记请求，tick() 在主循环里推进开关，
// 全程不 delay，绝不卡按键/渲染（压测时也不会变慢）
struct Blink {
    bool     active = false;
    bool     on = false;
    int      times = 0;        // 计划闪几下
    int      done  = 0;        // 已闪几下
    int      onMs = 0, offMs = 0;
    uint32_t next = 0;         // 下一次状态切换的时刻
};
static Blink bl;

// 在指定引脚上初始化 I2C 并探测 0x40；返回是否在线
// 注意：ESP32 核心 TwoWire::begin() 签名是 (sdaPin, sclPin, freq)，SDA 在前
bool beginBus(int scl, int sda) {
    Wire.end();
    Wire.begin(sda, scl, 100000);
    Wire.beginTransmission(LIGHT_ADDR);
    present = (Wire.endTransmission() == 0);
    curMask = 0;
    if (present) set(0);
    return present;
}

void init() { beginBus(PIN_I2C_SCL, PIN_I2C_SDA); }

bool available() { return present; }

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LIGHT_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void set(uint8_t mask) {
    if (!present || mask == curMask) return;
    if (writeReg(LIGHT_REG, mask)) curMask = mask;
}

// 强制关灯并同步内部状态（set(0) 在 already-0 时是空操作，调试/自检需要真写一次）
bool off() {
    if (!present) return false;
    bool okw = writeReg(LIGHT_REG, 0);
    if (okw) curMask = 0;
    return okw;
}

// 登记一次闪烁。限流策略：已有闪烁在进行时，只允许更高优先级
// （err=3 > ok=2 > click=1）打断，避免连闪成灯海
void pulse(int times, int onMs, int offMs) {
    if (!present || times <= 0) return;
    if (bl.active && times <= bl.times) return;
    bl.active = true;
    bl.on = false;
    bl.done = 0;
    bl.times = times;
    bl.onMs = onMs;
    bl.offMs = offMs;
    bl.next = millis();        // 下一个 tick 立即点亮
}

// 由主循环调用：到点切换灯的状态
void tick() {
    if (!bl.active) return;
    uint32_t now = millis();
    if ((int32_t)(now - bl.next) < 0) return;   // 还没到切换时刻
    if (bl.on) {
        set(0);
        bl.on = false;
        bl.done++;
        if (bl.done >= bl.times) { bl.active = false; return; }
        bl.next = now + bl.offMs;
    } else {
        set(1);
        bl.on = true;
        bl.next = now + bl.onMs;
    }
}

void click() { pulse(1, 25, 0); }    // 单击：1 短闪
void ok()    { pulse(2, 60, 60); }   // 答对：2 闪
void err()   { pulse(3, 40, 40); }   // 答错：3 急闪

}  // namespace Lights
