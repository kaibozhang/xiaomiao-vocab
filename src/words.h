#pragma once
#include <stdint.h>
#include <stddef.h>
#include "words_data.h"
#include <stdbool.h>

// 内置词库（编译进 flash）+ 用户自定义词（LittleFS）
#define BASE_WORD_COUNT 5211
#define MAX_USER_WORDS  256
#define MAX_WORDS       (BASE_WORD_COUNT + MAX_USER_WORDS)
#define USER_BOOK       BOOK_COUNT   // 自定义词所属"册"

// 统一访问接口：0..BASE_WORD_COUNT-1 为内置，之后为自定义
int         ws_count();
const char* ws_en(int i);
const char* ws_ph(int i);
const char* ws_zh(int i);
int         ws_book(int i);
bool        ws_isUser(int i);

// 用户词管理
int  ws_userCount();
void ws_init();                                   // 从 LittleFS 载入
int  ws_addUser(const char* en, const char* ph, const char* zh);  // -1 失败
bool ws_delUser(int idx);                         // idx 为全局索引
bool ws_clearUser();
bool ws_saveUser();
int  ws_importCsv(const char* text, bool replace); // 返回新增条数
void ws_dumpCsv(char* out, size_t cap);           // 导出用户词
