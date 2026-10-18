#pragma once
#include <stdint.h>

// 小喵掌机 I2C 马达/LED 控制器（地址 0x40，SCL=GPIO15 SDA=GPIO21）
// 灯的官方控制方式：i2c.writeto_mem(0x40, 0xA0, [1])  —— 寄存器 0xA0 写 1 开灯
// 用途：静音（无蜂鸣器）时用灯光做操作反馈
namespace Lights {

void init();                     // 在默认引脚上初始化 I2C 并探测 0x40
bool beginBus(int scl, int sda); // 在指定引脚上初始化 I2C（换脚调试用），返回 0x40 是否在线
bool available();                // 0x40 是否在线
bool writeReg(uint8_t reg, uint8_t val);   // 裸寄存器写（调试用）
void set(uint8_t mask);          // 灯开关：mask 位掩码写入 0xA0，0 = 全灭
bool off();                      // 强制关灯并同步内部状态，返回 I2C 写是否成功
void pulse(int times, int onMs, int offMs); // 登记一次闪烁（非阻塞，tick 推进）
void tick();                     // 主循环调用：推进闪烁状态机
void click();                    // 单击反馈：1 短闪
void ok();                       // 答对反馈：2 闪
void err();                      // 答错反馈：3 急闪
}
