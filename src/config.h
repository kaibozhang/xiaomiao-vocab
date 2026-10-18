#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// 学而思小喵掌机（KittenBot MeowBit 同版硬件）硬件定义
// 与社区 Retro-Go 移植完全一致，已在本机实测（COM3 可刷、4MB Flash、有 PSRAM）
// ---------------------------------------------------------------------------
#define SCR_W 160
#define SCR_H 128

// 6 个物理按键（低电平有效）。GPIO34/35 为输入专用脚，无内部上拉，靠外部电阻
#define PIN_UP    2
#define PIN_DOWN  13
#define PIN_LEFT  27
#define PIN_RIGHT 35
#define PIN_A     34
#define PIN_B     12

// ST7735 160x128，SPI2 与 TF 卡共享总线
#define PIN_LCD_CS   5
#define PIN_LCD_DC   4
#define PIN_LCD_MOSI 23
#define PIN_LCD_CLK  18
#define PIN_LCD_MISO 19
#define PIN_SD_CS    22   // 保持高电平，避免 TF 卡抢占 MISO
#define LCD_SPI_FREQ 20000000

// 无源蜂鸣器
#define PIN_BUZZER 14

// I2C 总线：接马达/LED 控制器（地址 0x40，寄存器 0xA0 控制灯）
// 官方标注图：SCL=GPIO15, SDA=GPIO21
#define PIN_I2C_SCL 15
#define PIN_I2C_SDA 21

// ---------------------------------------------------------------------------
// 配色（RGB565）
// ---------------------------------------------------------------------------
enum {
    C_BG     = 0x10C4,  // 主背景 深蓝灰
    C_PANEL  = 0x2126,  // 面板
    C_BAR    = 0x3186,  // 标题栏
    C_FG     = 0xFFFF,  // 主文字
    C_DIM    = 0x9515,  // 次级文字
    C_ACCENT = 0x0655,  // 强调 青绿
    C_OK     = 0x3E4A,  // 正确 绿
    C_ERR    = 0xE228,  // 错误 红
    C_WARN   = 0xFE47,  // 提示 黄
    C_SEL    = 0x2AD9,  // 选中条 蓝
};
