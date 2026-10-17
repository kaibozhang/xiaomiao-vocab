#include "store.h"
#include "words.h"
#include "clock.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <string.h>
#include "esp_heap_caps.h"

namespace Store {

WordState* st = nullptr;
static void* bigAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}
Config cfg;

static Preferences prefs;
static bool dirty = false;
static uint32_t lastSave = 0;
static bool fsOk = false;

static const char* PROG_PATH = "/prog.bin";

// ---------------------------------------------------------------- 纯逻辑
// 2000-01-01 是星期几：星期六。days = unix/86400 + 10957
uint32_t unixToDay(uint32_t t) { return t / 86400u + 10957u; }

void schedule(WordState& s, bool good, int today) {
    if (s.ease10 == 0) s.ease10 = 25;
    if (good) {
        if (s.interval == 0) s.interval = 1;
        else if (s.interval == 1) s.interval = 3;
        else {
            uint32_t iv = (uint32_t)s.interval * s.ease10 / 10;
            if (iv < (uint32_t)s.interval + 1) iv = (uint32_t)s.interval + 1;
            if (iv > 3650) iv = 3650;          // 先保证递增，再封顶，确保永不超上限
            s.interval = (uint16_t)iv;
        }
        if (s.reps < 255) s.reps++;
        s.status = (s.interval >= 3) ? 2 : 1;
        s.due = (uint16_t)(today + s.interval);
        if (s.reps >= 3) s.flags &= (uint8_t)~1;
    } else {
        if (s.lapses < 255) s.lapses++;
        s.reps = 0;
        s.ease10 = (s.ease10 > 15) ? (uint8_t)(s.ease10 - 2) : 13;
        s.interval = 1;
        s.status = 1;
        s.due = (uint16_t)(today + 1);
        s.flags |= 1;
    }
}

// ---------------------------------------------------------------- 初始化
static void defaults() {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = 0x564F4321;   // "VOC!"
    cfg.version = CFG_VERSION;
    cfg.dayIndex = 0;
    cfg.dailyNew = 20;
    cfg.soundOn = 1;
    cfg.orient = 0;
    cfg.bgr = 0;
    cfg.bookMask = 0x001FFFFF;   // 默认全选 21 组
    cfg.streak = 1;
    cfg.ssid[0] = 0;
    cfg.pass[0] = 0;
    cfg.wifiOn = 1;
    cfg.saverMode = 0;          // 数字时钟
    cfg.saverSec = 60;
    cfg.battMode = 0;           // 估算
    cfg.battPin = 32;
    cfg.battFullMv = 4200;
    cfg.battEmptyMv = 3300;
    cfg.autoSync = 1;
    cfg.hour24 = 1;
}

void begin() {
    if (!st) {
        st = (WordState*)bigAlloc(sizeof(WordState) * MAX_WORDS);
        memset(st, 0, sizeof(WordState) * MAX_WORDS);
    }
    prefs.begin("vocab", false);

    size_t cn = prefs.getBytesLength("cfg");
    bool fresh = true;
    if (cn == sizeof(Config)) {
        prefs.getBytes("cfg", &cfg, cn);
        if (cfg.magic == 0x564F4321 && cfg.version == CFG_VERSION) fresh = false;
    }
    if (fresh) { defaults(); memset(st, 0, sizeof(st)); }

    // 学习进度放 LittleFS：NVS 单条 blob 有尺寸限制，50KB+ 装不下
    fsOk = LittleFS.begin(false, "/prog", 8, "prog");
    if (!fsOk) {
        LittleFS.format();
        fsOk = LittleFS.begin(false, "/prog", 8, "prog");
    }
    if (fsOk && !fresh) {
        File f = LittleFS.open(PROG_PATH, "r");
        if (f) {
            f.read((uint8_t*)st, sizeof(WordState) * MAX_WORDS);
            f.close();
        }
    }

    // 没有 RTC：每场开机 dayIndex +1，供无网络时退化使用
    cfg.dayIndex++;

    markDirty();
}

void save() {
    prefs.putBytes("cfg", &cfg, sizeof(cfg));
    if (fsOk) {
        File f = LittleFS.open(PROG_PATH, "w");
        if (f) {
            f.write((const uint8_t*)st, sizeof(WordState) * MAX_WORDS);
            f.close();
        }
    }
    dirty = false;
    lastSave = millis();
}

bool fsReady() { return fsOk; }

void markDirty() { dirty = true; }

void saveIfDirty() {
    if (dirty && (uint32_t)(millis() - lastSave) > 15000) save();
}

// ---------------------------------------------------------------- 词册
bool bookEnabled(int b) { return b >= 0 && b <= USER_BOOK && ((cfg.bookMask >> b) & 1u); }

void setBookEnabled(int b, bool en) {
    if (b < 0 || b > USER_BOOK) return;
    if (en) cfg.bookMask |= (1u << b);
    else cfg.bookMask &= ~(1u << b);
    markDirty();
}

int bookCount() { return USER_BOOK + 1; }

const char* bookName(int b) {
    if (b == USER_BOOK) return "自定义词";
    if (b < 0 || b >= BOOK_COUNT) return "";
    return BOOK_NAME[b];
}

int bookWordCount(int b) {
    if (b == USER_BOOK) return ws_userCount();
    if (b < 0 || b >= BOOK_COUNT) return 0;
    return (int)BOOK_WORDS[b];
}

int enabledWordCount() {
    int n = 0;
    for (int i = 0; i < ws_count(); i++)
        if (bookEnabled(ws_book(i))) n++;
    return n;
}

int today() {
    uint32_t u = Clock::now();
    if (u > 1000000000u) return (int)unixToDay(u);
    return (int)cfg.dayIndex;   // 没时间就用场次
}

// ---------------------------------------------------------------- 状态查询
bool isDue(int i) { return st[i].status > 0 && st[i].due <= (uint16_t)today(); }
bool isWrong(int i) { return (st[i].flags & 1) != 0; }

void setWrong(int i, bool on) {
    if (on) st[i].flags |= 1;
    else st[i].flags &= (uint8_t)~1;
}

void answer(int i, bool good) {
    bool wasNew = (st[i].status == 0);
    schedule(st[i], good, today());
    if (wasNew) { cfg.todayNew++; }
    else { cfg.todayReview++; }
    cfg.totalAnswers++;
    if (good) cfg.totalCorrect++;
    cfg.todayAnswers++;
    if (good) cfg.todayCorrect++;
    markDirty();
}

int countDue() {
    int n = 0, t = today();
    for (int i = 0; i < ws_count(); i++)
        if (bookEnabled(ws_book(i)) && st[i].status > 0 && st[i].due <= (uint16_t)t) n++;
    return n;
}

int countNew() {
    int n = 0;
    for (int i = 0; i < ws_count(); i++)
        if (bookEnabled(ws_book(i)) && st[i].status == 0) n++;
    return n;
}

int countWrong() {
    int n = 0;
    for (int i = 0; i < ws_count(); i++)
        if (bookEnabled(ws_book(i)) && isWrong(i)) n++;
    return n;
}

int countMastered() {
    int n = 0;
    for (int i = 0; i < ws_count(); i++)
        if (bookEnabled(ws_book(i)) && st[i].status == 2 && st[i].interval >= 21) n++;
    return n;
}

// ---- 闯关进度（cfg.rsv2）----
// rsv2[1..8] 作为 uint64_t：每关 2 bit 星级，第 lv 关在位 (lv-1)*2
static uint64_t starsBits() {
    uint64_t v = 0;
    memcpy(&v, &cfg.rsv2[1], 8);
    return v;
}
static void setStarsBits(uint64_t v) {
    memcpy(&cfg.rsv2[1], &v, 8);
}

uint8_t gameUnlocked() { return cfg.rsv2[0]; }

int gameStars(int lv) {                 // lv 1-based
    if (lv < 1 || lv > GAME_MAXLV) return 0;
    return (int)((starsBits() >> ((lv - 1) * 2)) & 3);
}

void setGameResult(int lv, int stars) {
    if (lv < 1 || lv > GAME_MAXLV) return;
    if (stars < 0) stars = 0;
    if (stars > 3) stars = 3;
    uint64_t v = starsBits();
    v &= ~(3ULL << ((lv - 1) * 2));
    v |= (uint64_t)stars << ((lv - 1) * 2);
    setStarsBits(v);
    if (lv > cfg.rsv2[0]) cfg.rsv2[0] = (uint8_t)lv;   // 解锁下一关
    markDirty();
}

int gameStarsTotal() {
    int n = 0;
    for (int lv = 1; lv <= GAME_MAXLV; lv++) n += gameStars(lv);
    return n;
}

}  // namespace Store
