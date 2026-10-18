#pragma once
#include <stdint.h>
#include "words.h"

// 每个单词的学习状态，10 字节
struct WordState {
    uint16_t interval;  // 当前复习间隔（单位：天）
    uint16_t due;       // 到期日（自 2000-01-01 起的天数）
    uint8_t  status;    // 0 未学 1 学习中 2 复习中
    uint8_t  reps;      // 连续答对次数
    uint8_t  lapses;    // 遗忘次数
    uint8_t  ease10;    // 难度系数 x10（25 = 2.5）
    uint8_t  flags;     // bit0: 错词本
    uint8_t  rsv;
};

#define CFG_VERSION 2

struct Config {
    uint32_t magic;
    uint16_t version;
    uint16_t dayIndex;      // 学习场次（无网络时的退化计数）
    uint8_t  dailyNew;      // 每日新词量
    uint8_t  soundOn;
    uint8_t  orient;        // 屏幕方向 0..3
    uint8_t  bgr;
    uint32_t bookMask;      // 位掩码，21 组（20 册 + 自定义）
    uint32_t totalAnswers, totalCorrect;
    uint32_t wrongCount;
    uint16_t streak;        // 连续学习天数
    uint16_t lastDay;
    uint16_t todayNew, todayReview, todayAnswers, todayCorrect;

    // ---- v2：网络 / 屏保 / 电量 ----
    char     ssid[33];
    char     pass[65];
    uint8_t  wifiOn;        // 1=开机自动连接
    uint8_t  saverMode;     // 屏保类型 0..7
    uint16_t saverSec;      // 空闲多少秒进屏保，0=关闭
    uint8_t  battMode;      // 0 估算 1 扩展口 ADC 2 隐藏
    uint8_t  battPin;       // ADC 引脚编号（32/33/25/26）
    uint16_t battFullMv;    // 满电电压 mV
    uint16_t battEmptyMv;   // 耗尽电压 mV
    uint32_t usedMinutes;   // 估算模式：自上次标记满电后的使用分钟
    uint32_t lastUnix;      // 上次保存的 unix 时间
    uint8_t  autoSync;      // 开机自动对时
    uint8_t  syncDone;      // 本次开机是否已对时成功
    uint8_t  hour24;        // 1=24 小时制
    // ---- rsv2 复用为闯关进度（不改结构大小）----
    // rsv2[0]      : 已解锁的最高关（1-based，0=还没玩过），上限 GAME_MAXLV
    // rsv2[1..8]   : 每关星级，2 bit x 30 关 = 60 bit，低 60 位有效
    uint8_t  rsv2[12];
};

#define GAME_MAXLV 30

namespace Store {
void begin();
void save();                      // 立即保存：配置 + 脏掉的进度块
void saveIfDirty();
void markDirty();                 // 配置脏（设置项），下次 saveIfDirty 秒存 NVS
void markWordDirty(int i);        // 第 i 个词的学习状态脏了（按 4KB 块增量写）
void markAllProgDirty();          // 整个进度数组脏（清空进度等），下次保存整文件重写
void clearProgress();             // 清空学习进度（保留设置）
void resetConfig();               // 恢复默认设置（保留 WiFi 凭据与学习进度）

extern WordState* st;
extern Config cfg;

bool fsReady();
bool bookEnabled(int b);
void setBookEnabled(int b, bool en);
int  enabledWordCount();
int  bookCount();
const char* bookName(int b);
int  bookWordCount(int b);
int  today();               // 今天（无网络时退化为 dayIndex）

bool isDue(int i);
bool isWrong(int i);
void setWrong(int i, bool on);
void answer(int i, bool good);     // 调度 + 统计
int  countDue();
int  countNew();
int  countWrong();
int  countMastered();

// 纯逻辑（可在 PC 上单测）
void schedule(WordState& s, bool good, int today);
uint32_t unixToDay(uint32_t unixTime);

// 闯关进度
uint8_t gameUnlocked();                    // 已解锁最高关（0=未玩）
int     gameStars(int lv);                 // 某关星级 0..3（lv 1-based）
void    setGameResult(int lv, int stars);  // 记录结算并解锁下一关
int     gameStarsTotal();
}
