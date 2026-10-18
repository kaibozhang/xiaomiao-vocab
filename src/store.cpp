#include "store.h"
#include "words.h"
#include "clock.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_partition.h"

namespace Store {

WordState* st = nullptr;
static void* bigAlloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}
Config cfg;

static Preferences prefs;
static bool cfgDirty = false;             // 配置（NVS）脏
static bool progDirty = false;            // 有单词状态脏
static bool progNeedFull = false;         // 需要写头 + 全量数据（首次/迁移后/清空）
static uint32_t lastSave = 0;
static uint32_t lastProgSave = 0;
static bool fsOk = false;

static const char* PROG_PATH = "/prog.bin";   // 旧版 LittleFS 进度文件（仅迁移用）

// 学习进度直接存 prog 裸分区（不再走 LittleFS：其增量写会触发整文件压缩，~1s 卡顿）
// 布局：扇区0 = 头（magic/version/size），数据区是 st 的扁平字节镜像，按 4KB 扇区增量擦写
static const esp_partition_t* progPart = nullptr;
#define PROG_HDR_OFF  0
#define PROG_DATA_OFF 4096
#define PROG_MAGIC    0x50524F47u          // "PROG"

// 每个词 1 bit 的脏标记。数据区是 st 的扁平镜像，按 4KB 扇区增量擦写：
// 答 1 个词典型只擦写 1 个扇区（~60ms），告别整文件 1 秒卡顿
static uint8_t wordDirty[(MAX_WORDS + 7) >> 3];
#define WS_REC (sizeof(WordState))    // 10
#define WS_BLK 4096

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
    if (fresh) { defaults(); memset(st, 0, sizeof(WordState) * MAX_WORDS); }

    // LittleFS 挂独立 fs 分区：只放自定义词库（学习进度在裸 prog 分区，两者互不覆盖）
    fsOk = LittleFS.begin(false, "/fs", 8, "fs");
    if (!fsOk) {
        LittleFS.format();
        fsOk = LittleFS.begin(false, "/fs", 8, "fs");
    }

    // 学习进度：裸分区优先，其次迁移旧的 LittleFS prog.bin
    progPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "prog");
    bool loaded = false;
    if (progPart) {
        uint32_t hdr[3];
        if (esp_partition_read(progPart, PROG_HDR_OFF, hdr, sizeof(hdr)) == ESP_OK &&
            hdr[0] == PROG_MAGIC && hdr[2] == (uint32_t)(sizeof(WordState) * MAX_WORDS)) {
            loaded = (esp_partition_read(progPart, PROG_DATA_OFF, st,
                                         sizeof(WordState) * MAX_WORDS) == ESP_OK);
        }
    }
    if (!loaded && fsOk) {
        // 迁移：旧的 LittleFS /prog.bin（读进内存后删掉，下次保存落到裸分区）
        File f = LittleFS.open(PROG_PATH, "r");
        if (f && (uint32_t)f.size() == (uint32_t)(sizeof(WordState) * MAX_WORDS)) {
            f.read((uint8_t*)st, sizeof(WordState) * MAX_WORDS);
            loaded = true;
        }
        if (f) f.close();
        LittleFS.remove(PROG_PATH);
    }
    if (!loaded) {                  // 没有进度 → 全零，下次保存写头 + 全量
        memset(st, 0, sizeof(WordState) * MAX_WORDS);
        progNeedFull = true;
    }

    // 没有 RTC：每场开机 dayIndex +1，供无网络时退化使用
    cfg.dayIndex++;

    markDirty();
}

// ---------------------------------------------------------------- 保存
static void saveCfg() {
    prefs.putBytes("cfg", &cfg, sizeof(cfg));       // ~2ms
    cfgDirty = false;
    lastSave = millis();
}

// 写数据区里第 s 个 4KB 扇区（s 从 0 计，数据区起点 PROG_DATA_OFF 已 4KB 对齐）
static void writeProgSector(int s) {
    uint32_t total = (uint32_t)(sizeof(WordState) * MAX_WORDS);
    uint32_t so = (uint32_t)s * WS_BLK;
    if (so >= total) return;
    uint32_t len = (total - so < WS_BLK) ? (total - so) : WS_BLK;
    esp_partition_erase_range(progPart, PROG_DATA_OFF + so, WS_BLK);
    esp_partition_write(progPart, PROG_DATA_OFF + so, (const uint8_t*)st + so, len);
}

static void writeProgHeader() {
    uint32_t hdr[3] = {PROG_MAGIC, 1u, (uint32_t)(sizeof(WordState) * MAX_WORDS)};
    esp_partition_erase_range(progPart, PROG_HDR_OFF, WS_BLK);
    esp_partition_write(progPart, PROG_HDR_OFF, hdr, sizeof(hdr));
}

// 增量保存：把脏词按所属扇区聚合，只擦写受影响的 4KB 扇区。
// 答 1 个词典型 1 个扇区（erase+program ~60ms）；跨扇区词会带出相邻扇区
static void saveProgBlocks() {
    if (!progPart) return;
    uint32_t total = (uint32_t)(sizeof(WordState) * MAX_WORDS);
    int nsec = (int)((total + WS_BLK - 1) / WS_BLK);
    if (progNeedFull) {
        writeProgHeader();
        for (int s = 0; s < nsec; s++) writeProgSector(s);
        memset(wordDirty, 0, sizeof(wordDirty));
        progNeedFull = false;
        progDirty = false;
        lastProgSave = millis();
        return;
    }
    for (int s = 0; s < nsec; s++) {
        uint32_t so = (uint32_t)s * WS_BLK;
        uint32_t eo = so + WS_BLK;
        if (eo > total) eo = total;
        // 与扇区字节区间 [so, eo) 有交集的词
        int w0 = (int)(so / WS_REC);
        int w1 = (int)((eo + WS_REC - 1) / WS_REC);
        if (w1 > MAX_WORDS) w1 = MAX_WORDS;
        bool any = false;
        for (int i = w0; i < w1 && !any; i++)
            if (wordDirty[i >> 3] & (uint8_t)(1u << (i & 7))) any = true;
        if (!any) continue;
        writeProgSector(s);
        for (int i = w0; i < w1; i++)
            wordDirty[i >> 3] &= (uint8_t)~(1u << (i & 7));
    }
    progDirty = false;
    lastProgSave = millis();
}

void save() {
    saveCfg();
    if (progDirty) saveProgBlocks();
}

bool fsReady() { return fsOk; }

void markDirty() { cfgDirty = true; }

void markWordDirty(int i) {
    if (i < 0 || i >= MAX_WORDS) return;
    wordDirty[i >> 3] |= (uint8_t)(1u << (i & 7));
    progDirty = true;
}

void markAllProgDirty() { progDirty = true; progNeedFull = true; }

// 清空学习进度（保留设置）。设置界面里的「清空进度」也走这条路。
void clearProgress() {
    if (!st) return;
    memset(st, 0, sizeof(WordState) * MAX_WORDS);
    cfg.todayNew = 0;
    cfg.todayReview = 0;
    cfg.todayAnswers = 0;
    cfg.todayCorrect = 0;
    cfg.streak = 0;
    markDirty();
    markAllProgDirty();
    save();
}

// 恢复默认设置（保留 WiFi 凭据和学习进度）。自动化测试用来保证每轮起点一致。
void resetConfig() {
    Config keep = cfg;                       // 先整份留底
    defaults();
    memcpy(cfg.ssid, keep.ssid, sizeof(cfg.ssid));
    memcpy(cfg.pass, keep.pass, sizeof(cfg.pass));
    markDirty();
    save();
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

// 换日检查：日期变了就重置今日计数，并推进连续学习天数。
// 放在 saveIfDirty 里每次 loop 调用（today() 只是一次比较，开销可忽略）。
// 注意：未对时时 today() 退化为场次号（每次开机+1），此时每次开机都算新的一天。
static void rollDay() {
    int t = today();
    if (t == (int)cfg.lastDay) return;
    bool studied = (cfg.todayAnswers > 0);        // 刚过去的那天有没有学习
    cfg.todayNew = 0;
    cfg.todayReview = 0;
    cfg.todayAnswers = 0;
    cfg.todayCorrect = 0;
    cfg.streak = studied ? (uint16_t)(cfg.streak + 1) : 0;
    cfg.lastDay = (uint16_t)t;
    markDirty();                                  // 下次 saveCfg 落 NVS
}

void saveIfDirty() {
    rollDay();                                    // 跨天重置今日计数（一次比较，可每次 loop 调）
    if (cfgDirty) saveCfg();                      // 配置秒存（~2ms）
    if (progDirty && (uint32_t)(millis() - lastProgSave) > 10000) saveProgBlocks();
}

// ---------------------------------------------------------------- 状态查询
bool isDue(int i) { return st[i].status > 0 && st[i].due <= (uint16_t)today(); }
bool isWrong(int i) { return (st[i].flags & 1) != 0; }

void setWrong(int i, bool on) {
    if (on) st[i].flags |= 1;
    else st[i].flags &= (uint8_t)~1;
    markWordDirty(i);
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
    markWordDirty(i);
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
