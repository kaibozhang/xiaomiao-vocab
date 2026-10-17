#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

namespace Clock {

void begin();
void tick();                 // 主循环调用（1 秒内多次即可）

uint32_t now();              // unix 时间戳；未同步时返回 0
bool     synced();           // 是否已通过 NTP 对时
bool     everSynced();       // 是否有过一次成功对时（用于显示可信度）
void     setManual(int y, int mo, int d, int h, int mi, int s);
void     setUnix(uint32_t unixTs);   // 直接用时间戳设定（调试/上位机授时）

int  hour();   int minute();  int second();
int  year();   int month();   int day();
int  weekday();              // 0=周日

// 纯函数（可单测）
void fmtTime(uint32_t unixTs, char* out, size_t cap, bool hour24);  // "14:05" / "2:05"
void fmtTimeS(uint32_t unixTs, char* out, size_t cap);              // "14:05:07"
void fmtDate(uint32_t unixTs, char* out, size_t cap);               // "2026-10-17"
void fmtDateCn(uint32_t unixTs, char* out, size_t cap);             // "10月17日 周六"
const char* weekName(int wd);
int  daysInMonth(int y, int m);
bool validDate(int y, int m, int d);
}
