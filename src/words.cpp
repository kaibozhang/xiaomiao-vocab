#include "words.h"
#include "words_data.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

static const char* USER_PATH = "/user.csv";

static char (*uEn)[24] = nullptr;
static char (*uPh)[32] = nullptr;
static char (*uZh)[64] = nullptr;
static void* bigAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}
static int  uCount = 0;

int ws_count() { return BASE_WORD_COUNT + uCount; }
int ws_userCount() { return uCount; }

const char* ws_en(int i) {
    return i < BASE_WORD_COUNT ? W_EN[i] : uEn[i - BASE_WORD_COUNT];
}
const char* ws_ph(int i) {
    return i < BASE_WORD_COUNT ? W_PH[i] : uPh[i - BASE_WORD_COUNT];
}
const char* ws_zh(int i) {
    return i < BASE_WORD_COUNT ? W_ZH[i] : uZh[i - BASE_WORD_COUNT];
}
int ws_book(int i) {
    return i < BASE_WORD_COUNT ? (int)W_BOOK[i] : USER_BOOK;
}
bool ws_isUser(int i) { return i >= BASE_WORD_COUNT; }

// ---------------------------------------------------------------- 字段裁剪
static void trim(char* s) {
    // 去掉首尾空白与 \r
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0;
    size_t pre = 0;
    while (s[pre] == ' ' || s[pre] == '\t') pre++;
    if (pre) memmove(s, s + pre, n - pre + 1);
}

// 把一行拆成最多 3 个字段（支持逗号 / 制表符 / 分号分隔，忽略引号包裹）
static int splitLine(char* line, char* f[3]) {
    char sep = ',';
    if (!strchr(line, ',') && strchr(line, '\t')) sep = '\t';
    else if (!strchr(line, ',') && strchr(line, ';')) sep = ';';
    int n = 0;
    char* p = line;
    while (n < 3) {
        char* q = strchr(p, sep);
        if (q) *q = 0;
        f[n] = p;
        trim(f[n]);
        // 去掉配对引号
        size_t L = strlen(f[n]);
        if (L >= 2 && f[n][0] == '"' && f[n][L - 1] == '"') {
            memmove(f[n], f[n] + 1, L - 1);
            f[n][L - 2] = 0;
        }
        n++;
        if (!q) break;
        p = q + 1;
    }
    return n;
}

static bool headerLine(const char* a) {
    return (strcasecmp(a, "en") == 0 || strcasecmp(a, "word") == 0 ||
            strcasecmp(a, "english") == 0 || strcasecmp(a, "单词") == 0);
}

// ---------------------------------------------------------------- 增删改
static int findUser(const char* en) {
    for (int i = 0; i < uCount; i++)
        if (strcasecmp(uEn[i], en) == 0) return i;
    return -1;
}

int ws_addUser(const char* en, const char* ph, const char* zh) {
    if (!en || !en[0] || !zh || !zh[0]) return -1;
    size_t le = strlen(en), lp = ph ? strlen(ph) : 0, lz = strlen(zh);
    if (le >= sizeof(uEn[0]) || lp >= sizeof(uPh[0]) || lz >= sizeof(uZh[0])) return -1;
    int at = findUser(en);
    if (at < 0) {
        if (uCount >= MAX_USER_WORDS) return -1;
        at = uCount++;
    }
    strcpy(uEn[at], en);
    strcpy(uPh[at], ph ? ph : "");
    strcpy(uZh[at], zh);
    return BASE_WORD_COUNT + at;
}

bool ws_delUser(int idx) {
    if (!ws_isUser(idx)) return false;
    int at = idx - BASE_WORD_COUNT;
    if (at < 0 || at >= uCount) return false;
    for (int i = at; i < uCount - 1; i++) {
        strcpy(uEn[i], uEn[i + 1]);
        strcpy(uPh[i], uPh[i + 1]);
        strcpy(uZh[i], uZh[i + 1]);
    }
    uCount--;
    return true;
}

bool ws_clearUser() { uCount = 0; return true; }

bool ws_saveUser() {
    File f = LittleFS.open(USER_PATH, "w");
    if (!f) return false;
    for (int i = 0; i < uCount; i++) {
        f.print(uEn[i]);
        f.print(',');
        f.print(uPh[i]);
        f.print(',');
        f.print(uZh[i]);
        f.print('\n');
    }
    f.close();
    return true;
}

void ws_init() {
    if (!uEn) {
        uEn = (char(*)[24])bigAlloc(sizeof(char) * 24 * MAX_USER_WORDS);
        uPh = (char(*)[32])bigAlloc(sizeof(char) * 32 * MAX_USER_WORDS);
        uZh = (char(*)[64])bigAlloc(sizeof(char) * 64 * MAX_USER_WORDS);
    }
    uCount = 0;
    File f = LittleFS.open(USER_PATH, "r");
    if (!f) return;
    while (f.available() && uCount < MAX_USER_WORDS) {
        String line = f.readStringUntil('\n');
        if (line.length() < 3) continue;
        char buf[160];
        size_t n = line.length();
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, line.c_str(), n);
        buf[n] = 0;
        char* fl[3] = {0, 0, 0};
        int k = splitLine(buf, fl);
        if (k < 2) continue;
        if (headerLine(fl[0])) continue;
        const char* en = fl[0];
        const char* zh = (k >= 3) ? fl[2] : fl[1];
        const char* ph = (k >= 3) ? fl[1] : "";
        ws_addUser(en, ph, zh);
    }
    f.close();
}

int ws_importCsv(const char* text, bool replace) {
    if (replace) uCount = 0;
    int added = 0;
    const char* p = text;
    char buf[200];
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = 0;
        p = e ? e + 1 : p + strlen(p);
        if (strlen(buf) < 3) continue;
        char* fl[3] = {0, 0, 0};
        int k = splitLine(buf, fl);
        if (k < 2) continue;
        if (headerLine(fl[0])) continue;
        const char* en = fl[0];
        const char* zh = (k >= 3) ? fl[2] : fl[1];
        const char* ph = (k >= 3) ? fl[1] : "";
        if (ws_addUser(en, ph, zh) >= 0) added++;
        if (uCount >= MAX_USER_WORDS) break;
    }
    return added;
}

void ws_dumpCsv(char* out, size_t cap) {
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < uCount && used + 120 < cap; i++) {
        int n = snprintf(out + used, cap - used, "%s,%s,%s\n", uEn[i], uPh[i], uZh[i]);
        if (n <= 0) break;
        used += (size_t)n;
    }
}
