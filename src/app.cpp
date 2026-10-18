#include "app.h"
#include "hal.h"
#include "store.h"
#include "config.h"
#include "words.h"
#include "clock.h"
#include "power.h"
#include "net.h"
#include "saver.h"
#include "status.h"
#include "lights.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"

// ===========================================================================
//  布局常量
// ===========================================================================
#define HEAD_H   15
#define BODY_TOP 16
#define BODY_END 112
#define FOOT_Y   113
#define LIST_TOP 17
#define ITEM_H   14

enum Screen {
    SCR_HOME, SCR_LEARN, SCR_QUIZ, SCR_SPELL, SCR_WRONG,
    SCR_STATS, SCR_SETTINGS, SCR_BOOKS, SCR_ABOUT, SCR_SUMMARY, SCR_MSG,
    SCR_WIFI, SCR_SCAN, SCR_PW, SCR_TIME,
    SCR_GLEVELS, SCR_GPLAY, SCR_GRESULT,
    SCR_MATCH, SCR_MATCHRESULT, SCR_SCRAMBLE, SCR_SCRAMBLERESULT,
    SCR_HANG, SCR_HANGEND, SCR_CHAIN, SCR_CHAINEND
};

enum SessMode { SM_CARD = 0, SM_QUIZ = 1, SM_SPELL = 2 };

namespace App {

static Screen scr = SCR_HOME;
static bool needDraw = true;
static int sel = 0;
static int scroll = 0;

// ---- 会话状态 ----
static SessMode sessMode = SM_CARD;
static int queue[256];
static int qn = 0, qi = 0, newStart = 0;
static bool revealed = false;
static int optWords[4], optCorrect = 0, optSel = 0;
static char typed[20];
static int typedLen = 0;
static int kbPos = 0;
static bool hintUsed = false;
static int feedback = 0;              // 0 无  1 对  -1 错
static uint32_t feedbackEnd = 0;
static int sNew = 0, sRev = 0, sOk = 0, sBad = 0;
static char msgLine[40];

// ---- 设置/错词本 ----
static int wrongScroll = 0, wrongSel = 0;
static bool clearConfirm = false;

// ---- 单词闯关 ----
#define GAME_QN     10      // 每关题数
#define GAME_TIME   15      // 每题限时（秒）
#define GAME_PASS   8       // 过关需答对数
static int  gLevel = 1;
static int  gQIdx = 0;                  // 当前第几题
static int  gCorrect = 0, gBad = 0;
static int  gCombo = 0, gMaxCombo = 0;
static int  gLives = 3;
static int  gQMode = 0;                 // 0 英→中  1 中→英  2 音标→英
static int  gTimer = GAME_TIME;
static uint32_t gLastSec = 0;
static int  gLevelSel = 0;              // 选关光标
static int  gGScroll = 0;
static int  gStars = 0;
static bool gWon = false;
static bool resetConfirm = false;

// ---- 单词配对消消乐 ----
#define M_CARDS   12                 // 12 张牌 = 6 对（3 列 x 4 行）
static int  mPairs = 6;                 // 每关配对数
static int  mSel1 = -1, mSel2 = -1;     // 已选中的两张牌
static int  mMatched[M_CARDS];          // 每张牌是否已消除（1 已消除）
static int  mCards[M_CARDS];            // 每张牌的单词 index
static int  mCardKind[M_CARDS];         // 0=英文 1=中文
static int  mCursor = 0;                // 当前光标（0..11）
static int  mScore = 0, mTries = 0;
static int  mLevel = 1;

// ---- 字母重组 ----
static int  sWord;                      // 当前词 index
static int  sScrambled[16];             // 打乱后的字母顺序（存 word 字母下标）
static int  sLen = 0;                   // 单词长度
static int  sPos = 0;                   // 当前填到第几位
static int  sSelIdx = 0;                // 候选字母光标
static int  sUsed[16];                  // 候选字母是否已用
static int  sBuilt[16];                 // 已填字母（存原单词下标）
static int  sScore = 0, sCorrect = 0, sTotal = 0;
static int  sLives = 3;

// ---- 猜单词（Hangman）----
#define HANG_LIVES 6
#define HANG_MAXLEN 10
static int  hWord = 0;                  // 当前词 index
static char hShown[HANG_MAXLEN + 1];    // 已猜出的显示（未猜出为 _）
static bool hGuessed[26];               // 26 个字母是否猜过
static int  hLives = HANG_LIVES;
static int  hKbPos = 0;                 // 键盘光标 0..25
static int  hScore = 0, hCorrect = 0, hTotal = 0;
static int  hPhase = 0;                 // 0 进行中  1 猜对等待  2 猜错等待
static uint32_t hPhaseEnd = 0;

// ---- 单词接龙 ----
static int  cWord = 0;                  // 当前词 index
static int  cOpts[3];                   // 3 个候选词 index
static int  cCorrect = 0;               // 正确选项下标 0..2
static int  cSel = 0;
static int  cLives = 3;
static int  cCombo = 0, cMaxCombo = 0;
static int  cScore = 0, cRound = 0;
static int  cPhase = 0;                 // 0 进行中  1 反馈等待
static uint32_t cPhaseEnd = 0;

// 新游戏帧推进（定义在后方，gameTick 需要前向声明）
static void hangTick(uint32_t nowMs);
static void chainTick(uint32_t nowMs);

static bool dirtyFlag = false;
static char buf[48];

// ---- 屏保 ----
static bool saverOn = false;
static uint32_t lastAct = 0;
static uint32_t saverFrame = 0;
static uint16_t saverSecRt = 0xFFFF;    // 运行时屏保延时覆盖（自动化测试用），0xFFFF=跟随设置
#define SAVER_FRAME_MS 100

// ---- WiFi / 密码输入 ----
static int  wifiSel = 0;
static int  wifiScroll = 0;
static bool wifiScanning = false;
static uint32_t wifiScanAt = 0;
static int  pwIdx = 0;                 // 正在输入密码的 SSID 序号
static char pwBuf[40];
static int  pwLen = 0;
static char pwSsid[40];
static uint32_t pwKbAt = 0;
static const char* PWKB = "abcdefghijklmnopqrstuvwxyz0123456789-_.@";

// ---- 手动时间 ----
static int timeField = 0;              // 0年1月2日3时4分
static int timeVal[5] = {2026, 10, 17, 8, 0};

static void markDraw() { needDraw = true; }

// ===========================================================================
//  基础绘制
// ===========================================================================
// 右上角固定显示 电量 + 时间；标题在左；right 显示在两者之间
static void header(const char* title, const char* right) {
    gfx.fillRect(0, 0, SCR_W, HEAD_H, C_BAR);
    int sw = StatusBar::statusWidth();                 // 电量 + HH:MM
    int bx = SCR_W - sw - 2;
    int pct = Power::percent();
    if (pct >= 0 || Store::cfg.battMode != 2) StatusBar::drawBattery(bx, 3, pct);
    uint32_t t = Clock::now();
    StatusBar::drawClock(SCR_W - 3, 1, t, Store::cfg.hour24 != 0,
                         t ? C_FG : C_WARN);
    gfx.text(title, 3, 1, C_FG, FONT_12);
    if (right && right[0]) {
        int limit = bx - 8;
        int tw = gfx.textWidth(title, FONT_12) + 6;
        if (limit - tw > 12) gfx.textMax(right, tw, 1, limit - tw, C_DIM, FONT_12);
    }
    gfx.hline(0, HEAD_H, SCR_W, C_ACCENT);
}

static void footer(const char* hint) {
    gfx.fillRect(0, FOOT_Y, SCR_W, SCR_H - FOOT_Y, C_PANEL);
    gfx.hline(0, FOOT_Y, SCR_W, C_BAR);
    gfx.text(hint, 3, FOOT_Y + 1, C_DIM, FONT_12);
}

static void rowItem(int idx, int y, const char* left, const char* right, bool selected) {
    if (selected) gfx.fillRect(0, y - 1, SCR_W, ITEM_H - 1, C_SEL);
    gfx.textMax(left, 4, y, SCR_W - 60, selected ? C_FG : C_FG, FONT_12);
    if (right && right[0]) gfx.textRight(right, SCR_W - 4, y, selected ? C_FG : C_ACCENT, FONT_12);
}

// ===========================================================================
//  队列构造
// ===========================================================================
static void shuffleRange(int* a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

static bool qHas(int idx, int upto) {
    for (int i = 0; i < upto; i++) if (queue[i] == idx) return true;
    return false;
}

static bool spellable(int i) {
    const char* s = ws_en(i);
    int n = 0;
    for (; *s; s++) {
        char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
        n++;
    }
    return n >= 2 && n <= 14;
}

static void buildCardQueue(bool reviewOnly) {
    qn = 0;
    const int CAP = 250;
    for (int i = 0; i < ws_count() && qn < CAP; i++) {
        if (Store::bookEnabled(ws_book(i)) && Store::isDue(i)) queue[qn++] = i;
    }
    shuffleRange(queue, qn);
    newStart = qn;
    if (reviewOnly) return;
    int limit = (int)Store::cfg.dailyNew - (int)Store::cfg.todayNew;
    if (limit < 0) limit = 0;
    if (limit > 60) limit = 60;
    int added = 0;
    for (int i = 0; i < ws_count() && qn < CAP && added < limit; i++) {
        if (Store::bookEnabled(ws_book(i)) && Store::st[i].status == 0) { queue[qn++] = i; added++; }
    }
}

static void buildQuizQueue(int count, bool wrongOnly) {
    qn = 0;
    // 错词优先
    int pool[600], pn = 0;
    if (wrongOnly) {
        for (int i = 0; i < ws_count() && pn < 600; i++)
            if (Store::bookEnabled(ws_book(i)) && Store::isWrong(i) && ws_zh(i)[0]) pool[pn++] = i;
    } else {
        for (int i = 0; i < ws_count() && pn < 600; i++)
            if (Store::bookEnabled(ws_book(i)) && Store::isWrong(i) && ws_zh(i)[0]) pool[pn++] = i;
        shuffleRange(pool, pn);
        int pn0 = pn;
        for (int i = 0; i < ws_count() && pn < 600; i++)
            if (Store::bookEnabled(ws_book(i)) && Store::isDue(i) && ws_zh(i)[0]) pool[pn++] = i;
        shuffleRange(pool + pn0, pn - pn0);
    }
    shuffleRange(pool, pn);
    for (int i = 0; i < pn && qn < count; i++) queue[qn++] = pool[i];
    // 不足则用随机词补齐
    int guard = 0;
    while (qn < count && guard++ < 4000) {
        int c = (int)(esp_random() % (uint32_t)ws_count());
        if (!Store::bookEnabled(ws_book(c)) || !ws_zh(c)[0]) continue;
        if (qHas(c, qn)) continue;
        queue[qn++] = c;
    }
}

static void buildSpellQueue(int count) {
    qn = 0;
    int pool[600], pn = 0;
    for (int i = 0; i < ws_count() && pn < 600; i++)
        if (Store::bookEnabled(ws_book(i)) && Store::isWrong(i) && spellable(i) && ws_zh(i)[0]) pool[pn++] = i;
    shuffleRange(pool, pn);
    int pn0 = pn;
    for (int i = 0; i < ws_count() && pn < 600; i++)
        if (Store::bookEnabled(ws_book(i)) && Store::isDue(i) && spellable(i) && ws_zh(i)[0]) pool[pn++] = i;
    shuffleRange(pool + pn0, pn - pn0);
    for (int i = 0; i < pn && qn < count; i++) queue[qn++] = pool[i];
    int guard = 0;
    while (qn < count && guard++ < 4000) {
        int c = (int)(esp_random() % (uint32_t)ws_count());
        if (!Store::bookEnabled(ws_book(c)) || !spellable(c) || !ws_zh(c)[0]) continue;
        if (qHas(c, qn)) continue;
        queue[qn++] = c;
    }
}

// ===========================================================================
//  题目
// ===========================================================================
static void makeOptions(int w) {
    optWords[0] = w;
    int k = 1, tries = 0;
    while (k < 4 && tries++ < 300) {
        int c = (int)(esp_random() % (uint32_t)ws_count());
        if (c == w || !ws_zh(c)[0]) continue;
        bool dup = false;
        for (int j = 0; j < k; j++)
            if (optWords[j] == c || strcmp(ws_zh(optWords[j]), ws_zh(c)) == 0) { dup = true; break; }
        if (dup) continue;
        optWords[k++] = c;
    }
    while (k < 4) optWords[k++] = w;
    shuffleRange(optWords, 4);
    optCorrect = 0;
    for (int i = 0; i < 4; i++) if (optWords[i] == w) optCorrect = i;
    optSel = 0;
}

static void nextQuestion() {
    revealed = false;
    typedLen = 0;
    typed[0] = 0;
    kbPos = 0;
    hintUsed = false;
    feedback = 0;
    if (qi < qn) {
        if (sessMode == SM_QUIZ) makeOptions(queue[qi]);
    }
}

static void startSession(SessMode m, bool reviewOnly, int count) {
    sessMode = m;
    qi = 0;
    sNew = sRev = sOk = sBad = 0;
    if (m == SM_CARD) buildCardQueue(reviewOnly);
    else if (m == SM_QUIZ) buildQuizQueue(count, false);
    else buildSpellQueue(count);
    nextQuestion();
    if (qn == 0) {
        scr = SCR_MSG;
        snprintf(msgLine, sizeof(msgLine), "%s",
                 m == SM_CARD ? (reviewOnly ? "今天没有要复习的词" : "今天的任务完成了") : "暂无可练习的单词");
    } else {
        scr = (m == SM_CARD) ? SCR_LEARN : (m == SM_QUIZ ? SCR_QUIZ : SCR_SPELL);
    }
    markDraw();
}

static void submitAnswer(bool good) {
    int w = queue[qi];
    bool wasNew = (Store::st[w].status == 0);
    Store::answer(w, good);
    if (wasNew) { Store::cfg.todayNew++; sNew++; }
    else { Store::cfg.todayReview++; sRev++; }
    if (good) sOk++; else sBad++;
    feedback = good ? 1 : -1;
    feedbackEnd = millis() + (good ? 420 : 700);
    if (good) Sound::ok(); else Sound::err();
    markDraw();
}

static void advance() {
    feedback = 0;
    qi++;
    if (qi >= qn) {
        Store::save();
        scr = SCR_SUMMARY;
    } else {
        nextQuestion();
    }
    markDraw();
}

// ===========================================================================
//  单词闯关
// ===========================================================================
static void gameMakeQ() {
    int w = queue[qi];
    optWords[0] = w;
    int k = 1, tries = 0;
    while (k < 4 && tries++ < 400) {
        int c = (int)(esp_random() % (uint32_t)ws_count());
        if (c == w || !ws_zh(c)[0] || !ws_en(c)[0]) continue;
        if (!Store::bookEnabled(ws_book(c))) continue;
        bool dup = false;
        for (int j = 0; j < k; j++) {
            if (optWords[j] == c) { dup = true; break; }
            if (gQMode == 0 && strcmp(ws_zh(optWords[j]), ws_zh(c)) == 0) { dup = true; break; }
            if (gQMode != 0 && strcmp(ws_en(optWords[j]), ws_en(c)) == 0) { dup = true; break; }
        }
        if (dup) continue;
        optWords[k++] = c;
    }
    while (k < 4) optWords[k++] = w;
    shuffleRange(optWords, 4);
    optCorrect = 0;
    for (int i = 0; i < 4; i++) if (optWords[i] == w) optCorrect = i;
    optSel = 0;
}

static void gameNextQ() {
    feedback = 0;
    gQMode = gQIdx % 3;             // 三种题型轮换
    gTimer = GAME_TIME;
    gLastSec = 0;
    gameMakeQ();
    markDraw();
}

static void gameStart(int lv) {
    gLevel = lv;
    gQIdx = 0;
    gCorrect = gBad = 0;
    gCombo = gMaxCombo = 0;
    gLives = 3;
    // 出题池：已学词优先，不足用启用词册随机补
    qn = 0;
    int pool[600], pn = 0;
    for (int i = 0; i < ws_count() && pn < 600; i++)
        if (Store::bookEnabled(ws_book(i)) && Store::st[i].status > 0 && ws_zh(i)[0]) pool[pn++] = i;
    shuffleRange(pool, pn);
    int pn0 = pn;
    for (int i = 0; i < ws_count() && pn < 600; i++)
        if (Store::bookEnabled(ws_book(i)) && ws_zh(i)[0]) pool[pn++] = i;
    shuffleRange(pool + pn0, pn - pn0);
    for (int i = 0; i < pn && qn < GAME_QN; i++) {
        bool dup = false;
        for (int j = 0; j < qn; j++) if (queue[j] == pool[i]) { dup = true; break; }
        if (!dup) queue[qn++] = pool[i];
    }
    qi = 0;
    App::noteActivity();
    scr = SCR_GPLAY;
    gameNextQ();
}

static void gameFinish() {
    gWon = (gCorrect >= GAME_PASS && gLives > 0);
    gStars = gWon ? (gCorrect - GAME_PASS + 1) : 0;
    if (gStars > 3) gStars = 3;
    if (gWon) Store::setGameResult(gLevel, gStars);
    Store::save();
    scr = SCR_GRESULT;
    markDraw();
}

// good: 是否答对；timeout: 超时（额外提示用）
static void gameSubmit(bool good) {
    int w = queue[qi];
    Store::answer(w, good);         // 同时计入 SM-2 调度与统计
    Store::cfg.todayAnswers++;
    if (good) Store::cfg.todayCorrect++;
    if (good) {
        gCorrect++;
        gCombo++;
        if (gCombo > gMaxCombo) gMaxCombo = gCombo;
        Sound::ok();
        feedback = 1;
    } else {
        gBad++;
        gCombo = 0;
        gLives--;
        Sound::err();
        feedback = -1;
    }
    feedbackEnd = millis() + (good ? 420 : 700);
    markDraw();
}

static void gameAdvance() {
    feedback = 0;
    gQIdx++;
    if (gLives <= 0 || gQIdx >= GAME_QN) gameFinish();
    else { qi = gQIdx; gameNextQ(); }
}

static void gameKeysPlay(uint8_t k) {
    if (feedback) return;
    if (k & BTN_UP)   { if (optSel > 0) { optSel--; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (optSel < 3) { optSel++; Sound::click(); } markDraw(); }
    if (k & BTN_A)    { gameSubmit(optSel == optCorrect); }
    if (k & BTN_B)    { Store::save(); scr = SCR_GLEVELS; markDraw(); }   // 中途退出不计成绩
}

static void gameKeysLevels(uint8_t k) {
    if (k & BTN_UP)   { if (gLevelSel > 0) { gLevelSel--; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (gLevelSel < GAME_MAXLV - 1) { gLevelSel++; Sound::click(); } markDraw(); }
    if (k & BTN_LEFT) { if (gLevelSel >= 5) { gLevelSel -= 5; Sound::click(); } markDraw(); }
    if (k & BTN_RIGHT){ if (gLevelSel <= GAME_MAXLV - 6) { gLevelSel += 5; Sound::click(); } markDraw(); }
    if (k & BTN_B)    { scr = SCR_HOME; sel = 4; markDraw(); }
    if (k & BTN_A) {
        uint8_t unlocked = Store::gameUnlocked();        int lv = gLevelSel + 1;
        if (lv <= unlocked + 1) {         // 下一关永远可挑战
            gameStart(lv);
        } else {
            Sound::err();
        }
        markDraw();
    }
}

static void gameKeysResult(uint8_t k) {
    if (k & BTN_A) { gameStart(gLevel); }                  // 再来一次
    if (k & BTN_B) { scr = SCR_GLEVELS; markDraw(); }      // 回选关
}

// ===========================================================================
//  单词配对消消乐（英→中配对）
// ===========================================================================
static void matchPickWords() {
    // 从启用词册随机挑 mPairs 个词，每个词取英文牌 + 中文牌，共 12 张
    int pool[64], pn = 0;
    for (int i = 0; i < ws_count() && pn < 64; i++)
        if (Store::bookEnabled(ws_book(i)) && ws_zh(i)[0] && ws_en(i)[0]) pool[pn++] = i;
    shuffleRange(pool, pn);
    for (int i = 0; i < mPairs && i < pn; i++) {
        int w = pool[i];
        mCards[i * 2] = w;                       // 偶数位英文
        mCardKind[i * 2] = 0;
        mCards[i * 2 + 1] = w;                   // 奇数位中文
        mCardKind[i * 2 + 1] = 1;
    }
    // 打乱 12 张牌的顺序
    int order[M_CARDS];
    for (int i = 0; i < M_CARDS; i++) order[i] = i;
    shuffleRange(order, M_CARDS);
    int tmpC[M_CARDS], tmpK[M_CARDS];
    for (int i = 0; i < M_CARDS; i++) { tmpC[i] = mCards[order[i]]; tmpK[i] = mCardKind[order[i]]; }
    for (int i = 0; i < M_CARDS; i++) { mCards[i] = tmpC[i]; mCardKind[i] = tmpK[i]; }
    for (int i = 0; i < M_CARDS; i++) mMatched[i] = 0;
}

static void matchStart() {
    mLevel = 1;
    mScore = 0;
    mTries = 0;
    mSel1 = mSel2 = -1;
    mCursor = 0;
    matchPickWords();
    App::noteActivity();
    scr = SCR_MATCH;
    markDraw();
}

static void matchNextRound() {
    mLevel++;
    mSel1 = mSel2 = -1;
    mCursor = 0;
    matchPickWords();
    markDraw();
}

static void matchFinish() {
    Store::save();
    scr = SCR_MATCHRESULT;
    markDraw();
}

static void matchKeys(uint8_t k) {
    if (k & BTN_B) { Store::save(); scr = SCR_HOME; sel = 5; markDraw(); return; }
    if (k & BTN_LEFT)  { if (mCursor % 3 > 0) { mCursor--; Sound::click(); markDraw(); } }
    if (k & BTN_RIGHT) { if (mCursor % 3 < 2 && mCursor < M_CARDS - 1) { mCursor++; Sound::click(); markDraw(); } }
    if (k & BTN_UP)    { if (mCursor >= 3) { mCursor -= 3; Sound::click(); markDraw(); } }
    if (k & BTN_DOWN)  { if (mCursor < M_CARDS - 3) { mCursor += 3; Sound::click(); markDraw(); } }
    if (k & BTN_A) {
        if (mMatched[mCursor]) return;               // 已消除的牌不可选
        if (mSel1 < 0) {
            mSel1 = mCursor; Sound::click(); markDraw();
        } else if (mCursor == mSel1) {
            mSel1 = -1; Sound::click(); markDraw();  // 取消选择
        } else {
            mSel2 = mCursor;
            mTries++;
            // 判断配对：两张牌对应同一个词
            if (mCards[mSel1] == mCards[mSel2]) {
                mMatched[mSel1] = mMatched[mSel2] = 1;
                mScore++;
                Sound::ok();
                int w = mCards[mSel1];
                Store::answer(w, true);
                mSel1 = mSel2 = -1;
                // 全部消除 → 下一轮
                bool done = true;
                for (int i = 0; i < M_CARDS; i++) if (!mMatched[i]) done = false;
                if (done) matchNextRound();
                markDraw();
            } else {
                Store::answer(mCards[mSel1], false);
                Sound::err();
                mSel1 = mSel2 = -1;
                markDraw();
            }
        }
    }
}

// ---- 配对消消乐渲染 ----
static void drawMatch() {
    snprintf(buf, sizeof(buf), "配对消消乐 %d", mLevel);
    header(buf, "");
    snprintf(buf, sizeof(buf), "配对 %d", mScore);
    gfx.textRight(buf, SCR_W - 3, 2, C_ACCENT, FONT_12);

    // 3 列 x 4 行网格，每张牌 51x23
    int cw = 51, chh = 23;
    int x0 = 3, y0 = 17;
    for (int i = 0; i < M_CARDS; i++) {
        int cx = x0 + (i % 3) * (cw + 1);
        int cy = y0 + (i / 3) * (chh + 1);
        bool sel = (mCursor == i) || (mSel1 == i);
        uint16_t bg = C_PANEL;
        if (mMatched[i]) bg = C_OK;
        else if (sel) bg = C_SEL;
        gfx.fillRect(cx, cy, cw, chh, bg);
        gfx.drawRect(cx, cy, cw, chh, mMatched[i] ? C_DIM : C_BAR);
        if (!mMatched[i]) {
            const char* txt = mCardKind[i] == 0 ? ws_en(mCards[i]) : ws_zh(mCards[i]);
            // 截断到卡片宽度内
            int maxw = cw - 8;
            int f = FONT_12;
            if (gfx.textWidth(txt, FONT_16) <= maxw) f = FONT_16;
            gfx.textMax(txt, cx + 4, cy + (f == FONT_16 ? 4 : 6), maxw, C_FG, f);
        }
    }
    footer("方向选   A 翻牌配对   B 退出");
}

static void drawMatchResult() {
    header("配对完成", "");
    gfx.textCenter("太棒了！", 22, C_OK, FONT_12);
    snprintf(buf, sizeof(buf), "共配对 %d 对", mScore);
    gfx.textCenter(buf, 44, C_FG, FONT_12);
    snprintf(buf, sizeof(buf), "尝试 %d 次", mTries);
    gfx.textCenter(buf, 62, C_DIM, FONT_12);
    footer("A 再来   B 返回");
}

static void matchResultKeys(uint8_t k) {
    if (k & BTN_A) matchStart();
    if (k & BTN_B) { scr = SCR_HOME; sel = 5; markDraw(); }
}

// ===========================================================================
//  字母重组（Word Scramble）
// ===========================================================================
static void scrambleMakeWord() {
    // 挑一个 3~8 字母的词
    int pool[64], pn = 0;
    for (int i = 0; i < ws_count() && pn < 64; i++) {
        int l = (int)strlen(ws_en(i));
        if (Store::bookEnabled(ws_book(i)) && ws_zh(i)[0] && l >= 3 && l <= 8) pool[pn++] = i;
    }
    if (pn == 0) {                    // 理论上不会发生；防御性退出
        Store::save();
        scr = SCR_HOME;
        sel = 6;
        return;
    }
    sWord = pool[esp_random() % pn];
    const char* w = ws_en(sWord);
    sLen = (int)strlen(w);
    for (int i = 0; i < sLen; i++) sScrambled[i] = i;
    shuffleRange(sScrambled, sLen);
    for (int i = 0; i < sLen; i++) sUsed[i] = 0;
    for (int i = 0; i < sLen; i++) sBuilt[i] = -1;
    sPos = 0;
    sSelIdx = 0;
}

static void scrambleStart() {
    sScore = 0;
    sCorrect = 0;
    sTotal = 0;
    sLives = 3;
    scrambleMakeWord();
    App::noteActivity();
    scr = SCR_SCRAMBLE;
    markDraw();
}

static void scrambleNextWord() {
    sTotal++;
    scrambleMakeWord();
    markDraw();
}

static void scrambleFinish() {
    Store::save();
    scr = SCR_SCRAMBLERESULT;
    markDraw();
}

static void scrambleKeys(uint8_t k) {
    if (k & BTN_B) { Store::save(); scr = SCR_HOME; sel = 6; markDraw(); return; }
    if (k & BTN_LEFT)  { if (sSelIdx > 0) { sSelIdx--; Sound::click(); markDraw(); } }
    if (k & BTN_RIGHT) { if (sSelIdx < sLen - 1) { sSelIdx++; Sound::click(); markDraw(); } }
    if (k & BTN_A) {
        // 选中候选字母，填入当前位
        if (!sUsed[sSelIdx]) {
            sUsed[sSelIdx] = 1;
            sBuilt[sPos] = sScrambled[sSelIdx];
            sPos++;
            Sound::click();
            // 填满则判定（按字母比较，含重复字母的词也能拼对）
            if (sPos >= sLen) {
                bool ok = true;
                const char* w = ws_en(sWord);
                for (int i = 0; i < sLen; i++) {
                    char a = w[sBuilt[i]]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                    char b = w[i];         if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                    if (a != b) { ok = false; break; }
                }
                if (ok) {
                    sCorrect++; sScore += 10;
                    Store::answer(sWord, true);
                    Sound::ok();
                    if (sCorrect >= 8) scrambleFinish();
                    else scrambleNextWord();
                } else {
                    sLives--;
                    Store::answer(sWord, false);
                    Sound::err();
                    if (sLives <= 0) scrambleFinish();
                    else scrambleMakeWord();   // 重新打乱同一个词
                }
                markDraw();
                return;
            }
            // 光标跳到下一个未用字母
            while (sSelIdx < sLen && sUsed[sSelIdx]) sSelIdx++;
            if (sSelIdx >= sLen) for (sSelIdx = 0; sSelIdx < sLen && sUsed[sSelIdx]; sSelIdx++);
            markDraw();
        }
    }
}

// ---- 字母重组渲染 ----
static void drawScramble() {
    snprintf(buf, sizeof(buf), "字母重组 %d", sTotal + 1);
    header(buf, "");
    for (int i = 0; i < 3; i++)
        gfx.fillRect(SCR_W - 10 - i * 8, 4, 6, 6, i < sLives ? C_ERR : C_DIM);

    // 中文释义
    gfx.textWrap(ws_zh(sWord), 3, 16, SCR_W - 6, C_FG, FONT_16, 20, 2);

    // 已填字母（下划线）
    int step = (SCR_W - 16) / (sLen > 0 ? sLen : 1);
    if (step > 14) step = 14;
    int sx = (SCR_W - sLen * step) / 2;
    char one[2] = {0, 0};
    for (int i = 0; i < sLen; i++) {
        int x = sx + i * step;
        if (sBuilt[i] >= 0) {
            one[0] = ws_en(sWord)[sBuilt[i]];
            gfx.text(one, x, 44, C_FG, FONT_16);
        } else {
            gfx.text("_", x, 44, C_DIM, FONT_16);
        }
    }

    // 候选字母（打乱）
    int cstep = 16;
    int cx = (SCR_W - sLen * cstep) / 2;
    for (int i = 0; i < sLen; i++) {
        int x = cx + i * cstep;
        bool sel = (sSelIdx == i);
        if (sUsed[i]) {
            gfx.fillRect(x - 2, 64, 14, 16, C_DIM);
        } else {
            gfx.fillRect(x - 2, 64, 14, 16, sel ? C_ACCENT : C_PANEL);
            one[0] = ws_en(sWord)[sScrambled[i]];
            gfx.text(one, x, 65, sel ? C_BG : C_FG, FONT_16);
        }
    }

    snprintf(buf, sizeof(buf), "得分 %d", sScore);
    gfx.textCenter(buf, 92, C_ACCENT, FONT_12);
    footer("←→选字母   A 填入   B 退出");
}

static void drawScrambleResult() {
    header("重组完成", "");
    gfx.textCenter("拼写挑战结束", 22, C_OK, FONT_12);
    snprintf(buf, sizeof(buf), "答对 %d/%d", sCorrect, sTotal);
    gfx.textCenter(buf, 44, C_FG, FONT_12);
    snprintf(buf, sizeof(buf), "得分 %d", sScore);
    gfx.textCenter(buf, 62, C_ACCENT, FONT_12);
    footer("A 再来   B 返回");
}

static void scrambleResultKeys(uint8_t k) {
    if (k & BTN_A) scrambleStart();
    if (k & BTN_B) { scr = SCR_HOME; sel = 6; markDraw(); }
}

// 每 250ms 调用一次（App::tick），负责倒计时与反馈推进
static void gameTick(uint32_t nowMs) {
    if (scr == SCR_GPLAY) {
        if (feedback) {
            if ((int32_t)(nowMs - feedbackEnd) >= 0) gameAdvance();
            return;
        }
        if (gLastSec == 0) { gLastSec = nowMs; return; }
        if (nowMs - gLastSec >= 1000) {
            gLastSec += 1000;
            if (--gTimer <= 0) { gTimer = 0; gameSubmit(false); }
            markDraw();
        }
    } else if (scr == SCR_HANG) {
        hangTick(nowMs);
    } else if (scr == SCR_CHAIN) {
        chainTick(nowMs);
    }
}

// ===========================================================================
//  猜单词（Hangman）：看中文释义猜英文拼写，一次猜一个字母
// ===========================================================================
static void hangPickWord() {
    int pool[64], pn = 0;
    for (int i = 0; i < ws_count() && pn < 64; i++) {
        int l = (int)strlen(ws_en(i));
        if (Store::bookEnabled(ws_book(i)) && ws_zh(i)[0] && l >= 3 && l <= HANG_MAXLEN)
            pool[pn++] = i;
    }
    if (pn == 0) {                      // 防御：没有可猜的词就回主菜单
        Store::save();
        scr = SCR_HOME;
        sel = 7;
        return;
    }
    hWord = pool[esp_random() % pn];
    const char* w = ws_en(hWord);
    int n = (int)strlen(w);
    for (int i = 0; i < n; i++) hShown[i] = '_';
    hShown[n] = 0;
    for (int i = 0; i < 26; i++) hGuessed[i] = false;
    hLives = HANG_LIVES;
    hKbPos = 0;
    hPhase = 0;
}

static void hangStart() {
    hScore = hCorrect = hTotal = 0;
    hangPickWord();
    App::noteActivity();
    scr = SCR_HANG;
    markDraw();
}

static void hangNextWord() {
    hTotal++;
    hangPickWord();
    markDraw();
}

static void hangGuess(int letter) {
    if (hGuessed[letter]) { Sound::click(); return; }
    hGuessed[letter] = true;
    const char* w = ws_en(hWord);
    bool hit = false;
    for (int i = 0; w[i]; i++) {
        char c = w[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c == (char)('a' + letter)) { hShown[i] = c; hit = true; }
    }
    if (hit) {
        bool full = true;
        for (int i = 0; hShown[i]; i++) if (hShown[i] == '_') full = false;
        if (full) {
            hCorrect++;
            hScore += 20 + hLives * 5;
            Store::answer(hWord, true);
            Sound::ok();
            hPhase = 1;
            hPhaseEnd = millis() + 1300;
        } else {
            Sound::click();
        }
    } else {
        hLives--;
        Sound::err();
        if (hLives <= 0) {
            Store::answer(hWord, false);
            hPhase = 2;
            hPhaseEnd = millis() + 2300;
        }
    }
    markDraw();
}

static void hangTick(uint32_t nowMs) {
    if (hPhase == 1 || hPhase == 2) {
        if ((int32_t)(nowMs - hPhaseEnd) >= 0) hangNextWord();
    }
}

static void hangKeys(uint8_t k) {
    if (hPhase != 0) return;             // 反馈动画期间屏蔽输入
    if (k & BTN_B) { Store::save(); scr = SCR_HOME; sel = 7; markDraw(); return; }
    if (k & BTN_LEFT)  { if (hKbPos % 7 > 0) { hKbPos--; Sound::click(); markDraw(); } }
    if (k & BTN_RIGHT) { if (hKbPos % 7 < 6 && hKbPos < 25) { hKbPos++; Sound::click(); markDraw(); } }
    if (k & BTN_UP)    { if (hKbPos >= 7) { hKbPos -= 7; Sound::click(); markDraw(); } }
    if (k & BTN_DOWN)  { if (hKbPos < 19) { hKbPos += 7; Sound::click(); markDraw(); } }
    if (k & BTN_A) { hangGuess(hKbPos); }
}

static void drawGallows(int wrong) {
    // 绞架
    gfx.hline(6, 66, 36, C_DIM);        // 底座
    gfx.vline(12, 24, 42, C_DIM);       // 立柱
    gfx.hline(12, 24, 30, C_DIM);       // 横梁
    gfx.vline(40, 24, 8, C_DIM);        // 绳子
    if (wrong < 1) return;
    // 头
    gfx.circle(40, 38, 6, C_FG);
    if (wrong == 1) return;
    // 身体
    gfx.line(40, 44, 40, 56, C_FG);
    if (wrong == 2) return;
    // 左臂
    gfx.line(40, 48, 32, 54, C_FG);
    if (wrong == 3) return;
    // 右臂
    gfx.line(40, 48, 48, 54, C_FG);
    if (wrong == 4) return;
    // 左腿
    gfx.line(40, 56, 33, 64, C_FG);
    if (wrong == 5) return;
    // 右腿
    gfx.line(40, 56, 47, 64, C_FG);
    // 输掉时的难过脸
    gfx.line(36, 36, 39, 39, C_ERR);
    gfx.line(39, 36, 36, 39, C_ERR);
    gfx.line(41, 36, 44, 39, C_ERR);
    gfx.line(44, 36, 41, 39, C_ERR);
    gfx.line(37, 42, 43, 42, C_ERR);
}

static void drawHang() {
    snprintf(buf, sizeof(buf), "猜单词 %d/%d", hCorrect, hTotal);
    header(buf, "");
    for (int i = 0; i < 3; i++)                 // 生命（小方块）
        gfx.fillRect(SCR_W - 12 - i * 8, 4, 6, 6, i < hLives ? C_ERR : C_DIM);

    drawGallows(HANG_LIVES - hLives);

    // 待猜单词（右侧）
    int n = (int)strlen(hShown);
    int cell = n > 0 ? (92 / n) : 16;
    if (cell > 16) cell = 16;
    int sx = 64 + (92 - n * cell) / 2;
    for (int i = 0; i < n; i++) {
        char one[2] = {hShown[i], 0};
        gfx.text(one[0] == '_' ? "_" : one, sx + i * cell, 26, hShown[i] == '_' ? C_DIM : C_FG, FONT_16);
        gfx.hline(sx + i * cell + 1, 46, cell - 2, C_BAR);
    }
    // 中文释义提示
    gfx.textWrap(ws_zh(hWord), 62, 50, 94, C_ACCENT, FONT_12, 14, 3);

    // 猜错的字母
    char wrong[32]; int wn = 0;
    wrong[0] = 0;
    for (int i = 0; i < 26; i++)
        if (hGuessed[i] && !strchr(ws_en(hWord), 'a' + i) && !strchr(ws_en(hWord), 'A' + i)) {
            if (wn < 28) { wrong[wn++] = (char)('a' + i); wrong[wn++] = ' '; wrong[wn] = 0; }
        }
    if (wn) gfx.textMax(wrong, 4, 68, 152, C_ERR, FONT_12);

    // 字母键盘 7 列 x 4 行
    for (int i = 0; i < 26; i++) {
        int cx = 4 + (i % 7) * 22;
        int cy = 82 + (i / 7) * 12;
        bool cur = (hKbPos == i);
        if (cur) gfx.fillRect(cx - 1, cy - 1, 19, 11, C_SEL);
        uint16_t col = C_FG;
        if (hGuessed[i]) {
            bool inWord = strchr(ws_en(hWord), 'a' + i) || strchr(ws_en(hWord), 'A' + i);
            col = inWord ? C_OK : C_ERR;
        }
        char one[2] = {(char)('a' + i), 0};
        gfx.text(one, cx + 4, cy, col, FONT_12);
    }
    // 阶段提示
    if (hPhase == 1) {
        gfx.fillRect(20, 54, 120, 18, C_PANEL);
        gfx.drawRect(20, 54, 120, 18, C_OK);
        gfx.textCenter("猜对啦！", 57, C_OK, FONT_12);
    } else if (hPhase == 2) {
        gfx.fillRect(8, 54, 144, 18, C_PANEL);
        gfx.drawRect(8, 54, 144, 18, C_ERR);
        snprintf(buf, sizeof(buf), "答案是 %s", ws_en(hWord));
        gfx.textCenter(buf, 57, C_ERR, FONT_12);
    }
    footer("方向选字母   A 猜   B 退出");
}

static void drawHangEnd() {
    header("猜单词结束", "");
    gfx.textCenter("游戏结束", 22, C_ACCENT, FONT_16);
    snprintf(buf, sizeof(buf), "猜对 %d / %d", hCorrect, hTotal);
    gfx.textCenter(buf, 46, C_FG, FONT_12);
    snprintf(buf, sizeof(buf), "得分 %d", hScore);
    gfx.textCenter(buf, 62, C_DIM, FONT_12);
    footer("A 再来   B 返回");
}

// ===========================================================================
//  单词接龙：选下一个词，首字母接上一个词的尾字母
// ===========================================================================
static void chainPickWord() {
    int pool[64], pn = 0;
    for (int i = 0; i < ws_count() && pn < 64; i++) {
        int l = (int)strlen(ws_en(i));
        if (Store::bookEnabled(ws_book(i)) && ws_zh(i)[0] && ws_en(i)[0] && l >= 3 && l <= 10)
            pool[pn++] = i;
    }
    if (pn == 0) {                      // 防御：没有可玩的词就回主菜单
        Store::save();
        scr = SCR_HOME;
        sel = 8;
        return;
    }
    cWord = pool[esp_random() % pn];
}

// 生成 3 个选项：1 个首字母接得上（正确），2 个接不上；cCorrect 记录正确项下标
static void chainMakeOpts() {
    const char* w = ws_en(cWord);
    char last = w[strlen(w) - 1];
    if (last >= 'A' && last <= 'Z') last = (char)(last + 32);
    int right[24], rn = 0, wrong[64], wn = 0;
    for (int i = 0; i < ws_count(); i++) {
        if (i == cWord || !ws_zh(i)[0] || !ws_en(i)[0]) continue;
        if (!Store::bookEnabled(ws_book(i))) continue;
        char f = ws_en(i)[0];
        if (f >= 'A' && f <= 'Z') f = (char)(f + 32);
        if (f == last) { if (rn < 24) right[rn++] = i; }
        else if (wn < 64) wrong[wn++] = i;
    }
    if (rn == 0 || wn < 2) {
        // 当前词接不上（词册被关到极少时的极端情况）：换一个当前词重试一次
        chainPickWord();
        const char* w2 = ws_en(cWord);
        char last2 = w2[strlen(w2) - 1];
        if (last2 >= 'A' && last2 <= 'Z') last2 = (char)(last2 + 32);
        rn = wn = 0;
        for (int i = 0; i < ws_count(); i++) {
            if (i == cWord || !ws_zh(i)[0] || !ws_en(i)[0]) continue;
            if (!Store::bookEnabled(ws_book(i))) continue;
            char f = ws_en(i)[0];
            if (f >= 'A' && f <= 'Z') f = (char)(f + 32);
            if (f == last2) { if (rn < 24) right[rn++] = i; }
            else if (wn < 64) wrong[wn++] = i;
        }
    }
    int good, b1, b2;
    if (rn > 0 && wn >= 2) {
        good = right[esp_random() % rn];
        b1 = wrong[esp_random() % wn];
        b2 = wrong[esp_random() % wn];
        int guard = 0;
        while ((b1 == b2 || b1 == good || b2 == good) && guard++ < 50) {
            b1 = wrong[esp_random() % wn];
            b2 = wrong[esp_random() % wn];
        }
        if (b1 == b2 || b1 == good || b2 == good) {   // 理论上到不了
            b1 = wrong[0];
            b2 = wrong[wn - 1];
        }
    } else {
        // 词册被关到极少时的退化处理：保证有 3 个选项、不卡死
        good = (rn > 0) ? right[0] : cWord;
        b1 = cWord;
        b2 = (wn > 0) ? wrong[0] : good;
        if (b2 == b1) b2 = good;
    }
    int trio[3] = {good, b1, b2};
    shuffleRange(trio, 3);
    for (int i = 0; i < 3; i++) {
        cOpts[i] = trio[i];
        if (trio[i] == good) cCorrect = i;
    }
    cSel = 0;
}

static void chainStart() {
    cLives = 3;
    cCombo = cMaxCombo = 0;
    cScore = cRound = 0;
    chainPickWord();
    chainMakeOpts();
    App::noteActivity();
    scr = SCR_CHAIN;
    markDraw();
}

static void chainTick(uint32_t nowMs) {
    if (cPhase == 1 && (int32_t)(nowMs - cPhaseEnd) >= 0) {
        cPhase = 0;
        chainPickWord();
        chainMakeOpts();
        markDraw();
    }
}

static void chainKeys(uint8_t k) {
    if (cPhase != 0) return;
    if (k & BTN_B) { Store::save(); scr = SCR_HOME; sel = 8; markDraw(); return; }
    if (k & BTN_UP)   { if (cSel > 0) { cSel--; Sound::click(); markDraw(); } }
    if (k & BTN_DOWN) { if (cSel < 2) { cSel++; Sound::click(); markDraw(); } }
    if (k & BTN_A) {
        int pick = cOpts[cSel];
        if (cSel == cCorrect) {
            Store::answer(pick, true);
            cCombo++;
            if (cCombo > cMaxCombo) cMaxCombo = cCombo;
            cScore += 10 + cCombo * 2;
            cRound++;
            cWord = pick;               // 接龙：选中词成为新当前词
            chainMakeOpts();
            Sound::ok();
        } else {
            Store::answer(cOpts[cCorrect], false);
            cLives--;
            cCombo = 0;
            Sound::err();
            if (cLives <= 0) {
                Store::save();
                scr = SCR_CHAINEND;
            } else {
                cPhase = 1;
                cPhaseEnd = millis() + 1500;
            }
        }
        markDraw();
    }
}

static void drawChain() {
    snprintf(buf, sizeof(buf), "单词接龙 %d", cRound + 1);
    header(buf, "");
    for (int i = 0; i < 3; i++)                 // 生命
        gfx.fillRect(SCR_W - 12 - i * 8, 4, 6, 6, i < cLives ? C_ERR : C_DIM);
    if (cCombo > 1) {
        snprintf(buf, sizeof(buf), "连击x%d", cCombo);
        gfx.text(buf, 3, 15, C_ACCENT, FONT_12);
    }

    // 当前词 + 释义
    const char* w = ws_en(cWord);
    int f = gfx.textWidth(w, FONT_24) <= SCR_W - 16 ? FONT_24 : FONT_16;
    gfx.textCenter(w, 24, C_FG, f);
    gfx.textWrap(ws_zh(cWord), 4, 46, SCR_W - 8, C_DIM, FONT_12, 14, 2);

    // 尾字母提示
    char last = w[strlen(w) - 1];
    if (last >= 'A' && last <= 'Z') last = (char)(last + 32);
    snprintf(buf, sizeof(buf), "尾字母 %c", last);
    gfx.textCenter(buf, 62, C_ACCENT, FONT_16);

    // 3 个候选
    for (int i = 0; i < 3; i++) {
        int y = 78 + i * 12;
        bool sel = (cSel == i);
        if (sel) gfx.fillRect(4, y - 1, SCR_W - 8, 12, C_SEL);
        gfx.textMax(ws_en(cOpts[i]), 8, y, 96, sel ? C_FG : C_FG, FONT_12);
        gfx.textRight(ws_zh(cOpts[i]), SCR_W - 8, y, sel ? C_ACCENT : C_DIM, FONT_12);
    }
    // 反馈提示
    if (cPhase == 1) {
        bool good = (cSel == cCorrect);
        snprintf(buf, sizeof(buf), good ? "" : "正确的是 %s", ws_en(cOpts[cCorrect]));
        if (!good) gfx.textCenter(buf, 66, C_ERR, FONT_12);
    }
    footer("↑↓ 选择   A 确认   B 退出");
}

static void drawChainEnd() {
    header("接龙结束", "");
    gfx.textCenter("游戏结束", 22, C_ACCENT, FONT_16);
    snprintf(buf, sizeof(buf), "接龙 %d 个   最高连击 %d", cRound, cMaxCombo);
    gfx.textCenter(buf, 46, C_FG, FONT_12);
    snprintf(buf, sizeof(buf), "得分 %d", cScore);
    gfx.textCenter(buf, 62, C_DIM, FONT_12);
    footer("A 再来   B 返回");
}

static void drawHome() {
    header("小喵背单词", "");
    int due = Store::countDue();
    int nw = Store::countNew();
    int take = nw < (int)Store::cfg.dailyNew - (int)Store::cfg.todayNew ? nw : (int)Store::cfg.dailyNew - (int)Store::cfg.todayNew;
    if (take < 0) take = 0;
    const char* names[12] = {"今日学习", "快速复习", "四选一测验", "拼写练习", "单词闯关", "配对消消乐",
                             "字母重组", "猜单词", "单词接龙", "错词本", "统计", "设置"};
    if (scroll < 0) scroll = 0;
    if (scroll > 12 - 7) scroll = 12 - 7;
    for (int r = 0; r < 7; r++) {
        int i = scroll + r;
        if (i >= 12) break;
        const char* right = "";
        if (i == 0) { snprintf(buf, sizeof(buf), "%d新 %d复", take, due); right = buf; }
        else if (i == 1) { snprintf(buf, sizeof(buf), "%d", due); right = buf; }
        else if (i == 4) { snprintf(buf, sizeof(buf), "星%d 关%d", Store::gameStarsTotal(), Store::gameUnlocked()); right = buf; }
        else if (i == 9) { snprintf(buf, sizeof(buf), "%d", Store::countWrong()); right = buf; }
        rowItem(i, LIST_TOP + r * ITEM_H, names[i], right, sel == i);
    }
    snprintf(buf, sizeof(buf), "第%d场  %d词", Store::cfg.dayIndex, Store::enabledWordCount());
    footer(buf);
}

static void drawCard() {
    int w = queue[qi];
    snprintf(buf, sizeof(buf), "%d/%d", qi + 1, qn);
    header(sessMode == SM_CARD ? "背单词" : "复习", buf);

    const char* word = ws_en(w);
    // 英文单词大字（24px，超长自动降级 16px）
    int f = (gfx.textWidth(word, FONT_24) <= SCR_W - 12) ? FONT_24 : FONT_16;
    int wy = (f == FONT_24) ? 26 : 32;
    gfx.textCenter(word, wy, C_FG, f);

    // 音标
    if (ws_ph(w)[0]) gfx.textCenter(ws_ph(w), wy + (f == FONT_24 ? 28 : 19), C_ACCENT, FONT_12);

    gfx.hline(12, 74, SCR_W - 24, C_BAR);

    // 中文释义：16px 大字号，最多 2 行
    if (revealed || feedback) {
        gfx.textWrap(ws_zh(w), 4, 82, SCR_W - 8, C_FG, FONT_16, 20, 2);
    } else {
        gfx.textCenter("按 A 显示释义", 86, C_DIM, FONT_12);
    }

    if (feedback) {
        uint16_t c = feedback > 0 ? C_OK : C_ERR;
        gfx.drawRect(0, BODY_TOP, SCR_W, BODY_END - BODY_TOP, c);
        gfx.textCenter(feedback > 0 ? "记得" : "忘了", 104, c, FONT_12);
    }

    if (revealed && !feedback) footer("← 忘了       记得 →");
    else if (!revealed) footer("A 显示释义    B 结束");
    else footer("");
}

static void drawQuiz() {
    int w = queue[qi];
    char title[24], r[20];
    snprintf(title, sizeof(title), "四选一 %d/%d", qi + 1, qn);
    snprintf(r, sizeof(r), "对%d 错%d", sOk, sBad);
    header(title, r);

    const char* word = ws_en(w);
    int f = (gfx.textWidth(word, FONT_24) <= SCR_W - 12) ? FONT_24 : FONT_16;
    gfx.textCenter(word, 14, C_FG, f);
    if (ws_ph(w)[0]) gfx.textCenter(ws_ph(w), f == FONT_24 ? 41 : 33, C_ACCENT, FONT_12);

    static const char* tag = "ABCD";
    char line[32];
    for (int i = 0; i < 4; i++) {
        int y = 56 + i * 19;
        bool chosen = (optSel == i);
        uint16_t bg = C_PANEL;
        uint16_t fg = C_FG;
        if (feedback) {
            if (i == optCorrect) { bg = C_OK; fg = C_FG; }
            else if (i == optSel) { bg = C_ERR; fg = C_FG; }
        } else if (chosen) {
            bg = C_SEL; fg = C_FG;
        }
        gfx.fillRect(2, y - 1, SCR_W - 4, 18, bg);
        snprintf(line, sizeof(line), "%c %s", tag[i], ws_zh(optWords[i]));
        gfx.textMax(line, 4, y + 1, SCR_W - 8, fg, FONT_16);
    }
    footer(feedback ? "" : "↑↓ 选择   A 确认   B 结束");
}

// ---- 闯关：选关 ----
static void drawGLevels() {
    char r[16];
    snprintf(r, sizeof(r), "%d/%d", Store::gameStarsTotal(), GAME_MAXLV * 3);
    header("单词闯关", r);
    uint8_t unlocked = Store::gameUnlocked();
    for (int i = 0; i < GAME_MAXLV; i++) {
        int col = i % 5, row = i / 5;
        int x = 4 + col * 31, y = 18 + row * 16;
        int lv = i + 1;
        bool locked = (lv > unlocked + 1);
        bool cur = (gLevelSel == i);
        uint16_t bg = cur ? C_SEL : C_PANEL;
        gfx.fillRect(x, y, 29, 14, bg);
        uint16_t fg = locked ? C_DIM : (cur ? C_FG : C_ACCENT);
        char num[4];
        snprintf(num, sizeof(num), "%d", lv);
        gfx.textCenter2(num, x, y + (locked ? 2 : 1), 29, fg, FONT_12);
        if (!locked) {
            int st = Store::gameStars(lv);
            for (int s = 0; s < 3; s++) {
                uint16_t c = s < st ? C_WARN : C_DIM;
                gfx.fillRect(x + 6 + s * 6, y + 10, 4, 2, c);
            }
        }
    }
    footer("←→↑↓ 选关  A 开始  B 返回");
}

// ---- 闯关：答题 ----
static void drawGPlay() {
    snprintf(buf, sizeof(buf), "第%d关 %d/%d", gLevel, gQIdx + 1, GAME_QN);
    header(buf, "");

    // 生命
    for (int i = 0; i < 3; i++)
        gfx.fillRect(SCR_W - 10 - i * 8, 4, 6, 6, i < gLives ? C_ERR : C_DIM);

    int w = queue[qi];
    if (gQMode == 0) {                       // 英 → 中
        int f = (gfx.textWidth(ws_en(w), FONT_24) <= SCR_W - 12) ? FONT_24 : FONT_16;
        gfx.textCenter(ws_en(w), 16, C_FG, f);
        if (ws_ph(w)[0]) gfx.textCenter(ws_ph(w), f == FONT_24 ? 44 : 34, C_DIM, FONT_12);
    } else if (gQMode == 1) {                // 中 → 英
        gfx.textWrap(ws_zh(w), 3, 16, SCR_W - 6, C_FG, FONT_16, 20, 2);
    } else {                                 // 音标 → 英
        if (ws_ph(w)[0]) gfx.textCenter(ws_ph(w), 20, C_ACCENT, FONT_16);
        else gfx.textCenter(ws_en(w), 16, C_FG, (gfx.textWidth(ws_en(w), FONT_24) <= SCR_W - 12) ? FONT_24 : FONT_16);
    }

    // 连击（画在标题栏右端、生命方块左边）
    if (gCombo >= 2) {
        snprintf(buf, sizeof(buf), "x%d", gCombo);
        gfx.textRight(buf, SCR_W - 30, 1, C_WARN, FONT_12);
    }

    // 倒计时条
    int bw = (SCR_W - 8) * gTimer / GAME_TIME;
    gfx.fillRect(4, 52, SCR_W - 8, 3, C_PANEL);
    if (bw > 0) gfx.fillRect(4, 52, bw, 3, gTimer <= 5 ? C_ERR : C_OK);

    static const char* tag = "ABCD";
    char line[32];
    for (int i = 0; i < 4; i++) {
        int y = 58 + i * 18;
        bool chosen = (optSel == i);
        uint16_t bg = C_PANEL, fg = C_FG;
        if (feedback) {
            if (i == optCorrect) { bg = C_OK; fg = C_FG; }
            else if (i == optSel) { bg = C_ERR; fg = C_FG; }
        } else if (chosen) {
            bg = C_SEL; fg = C_FG;
        }
        gfx.fillRect(2, y - 1, SCR_W - 4, 17, bg);
        const char* t = (gQMode == 0) ? ws_zh(optWords[i]) : ws_en(optWords[i]);
        snprintf(line, sizeof(line), "%c %s", tag[i], t);
        gfx.textMax(line, 4, y + 1, SCR_W - 8, fg, FONT_16);
    }
    footer(feedback ? "" : "↑↓ 选择   A 确认   B 退出");
}

// ---- 闯关：结算 ----
static void drawGResult() {
    header(gWon ? "通关" : "挑战结束", "");
    if (gWon) gfx.textCenter("好棒，闯关成功", 22, C_OK, FONT_12);
    else gfx.textCenter("差一点点，再来", 22, C_WARN, FONT_12);

    for (int s = 0; s < 3; s++) {            // 大星星（方块表示）
        uint16_t c = s < gStars ? C_WARN : C_DIM;
        gfx.fillRect(52 + s * 20, 36, 14, 14, c);
    }
    snprintf(buf, sizeof(buf), "答对 %d/%d   最高连击 %d", gCorrect, GAME_QN, gMaxCombo);
    gfx.textCenter(buf, 58, C_FG, FONT_12);
    snprintf(buf, sizeof(buf), "第%d关  星级 %d", gLevel, gStars);
    gfx.textCenter(buf, 74, C_DIM, FONT_12);
    footer("A 再来一次   B 选关");
}

static const char* KB = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static void drawSpell() {
    int w = queue[qi];
    snprintf(buf, sizeof(buf), "%d/%d", qi + 1, qn);
    header("拼写练习", buf);

    // 中文释义 16px
    gfx.textWrap(ws_zh(w), 3, BODY_TOP + 1, SCR_W - 6, C_WARN, FONT_16, 20, 2);

    // 已输入字母 24px 大字
    int len = (int)strlen(ws_en(w));
    if (len > 12) len = 12;
    int step = len > 0 ? (SCR_W - 8) / len : 12;
    if (step > 13) step = 13;
    int startX = (SCR_W - len * step) / 2;
    char one[2] = {0, 0};
    for (int i = 0; i < len; i++) {
        int x = startX + i * step;
        if (i < typedLen) {
            one[0] = typed[i];
            gfx.text(one, x, 46, C_FG, FONT_24);
        } else {
            gfx.text("_", x, 46, C_DIM, FONT_24);
        }
    }

    // 键盘 7x4
    int kx = 3, ky = 68;
    for (int i = 0; i < 28; i++) {
        int c = i % 7, r = i / 7;
        int x = kx + c * 22, y = ky + r * 11;
        bool cur = (kbPos == i);
        gfx.fillRect(x, y, 21, 11, cur ? C_ACCENT : C_PANEL);
        if (i < 26) {
            one[0] = KB[i];
            gfx.text(one, x + 7, y - 1, cur ? C_BG : C_FG, FONT_12);
        } else if (i == 26) {
            gfx.text("←", x + 6, y - 1, cur ? C_BG : C_WARN, FONT_12);
        } else {
            gfx.text("OK", x + 3, y - 1, cur ? C_BG : C_OK, FONT_12);
        }
    }

    if (feedback) {
        uint16_t c = feedback > 0 ? C_OK : C_ERR;
        gfx.drawRect(0, BODY_TOP, SCR_W, BODY_END - BODY_TOP, c);
        gfx.textCenter(ws_en(w), 100, c, FONT_16);
    }
    footer("方向选  A输入  B退格");
}

static void drawWrongList() {
    int total = Store::countWrong();
    snprintf(buf, sizeof(buf), "错词本 %d", total);
    header("错词本", buf);

    // 收集
    static int list[256];
    static int ln = 0;
    if (needDraw || true) {
        ln = 0;
        for (int i = 0; i < ws_count() && ln < 250; i++)
            if (Store::bookEnabled(ws_book(i)) && Store::isWrong(i)) list[ln++] = i;
    }
    int items = ln + 1;   // 第 0 项是"清空错词本"
    if (wrongSel >= items) wrongSel = items - 1;
    if (wrongSel < 0) wrongSel = 0;
    if (wrongSel < wrongScroll) wrongScroll = wrongSel;
    if (wrongSel >= wrongScroll + 7) wrongScroll = wrongSel - 6;

    for (int r = 0; r < 7; r++) {
        int idx = wrongScroll + r;
        int y = LIST_TOP + r * ITEM_H;
        if (idx >= items) break;
        bool chosen = (idx == wrongSel);
        if (chosen) gfx.fillRect(0, y - 1, SCR_W, ITEM_H, C_SEL);
        if (idx == 0) {
            gfx.text(clearConfirm ? "再按 A 确认清空" : "清空错词本", 4, y, clearConfirm ? C_ERR : C_WARN, FONT_12);
        } else {
            int w = list[idx - 1];
            char line[40];
            snprintf(line, sizeof(line), "%s", ws_en(w));
            gfx.textMax(line, 4, y, 90, C_FG, FONT_12);
            gfx.textMax(ws_zh(w), 96, y, SCR_W - 100, C_DIM, FONT_12);
        }
    }
    footer("A 练习此项   B 返回");
}

static void drawStats() {
    header("学习统计", "");
    int learned = 0, mastered = 0;
    for (int i = 0; i < ws_count(); i++) {
        if (!Store::bookEnabled(ws_book(i))) continue;
        if (Store::st[i].status > 0) learned++;
        if (Store::st[i].status == 2 && Store::st[i].interval >= 21) mastered++;
    }
    int total = Store::enabledWordCount();
    int due = Store::countDue();

    int y = BODY_TOP + 2;
    snprintf(buf, sizeof(buf), "已学 %d / %d", learned, total);
    gfx.text(buf, 4, y, C_FG, FONT_12);
    y += 15;
    snprintf(buf, sizeof(buf), "掌握 %d   待复习 %d", mastered, due);
    gfx.text(buf, 4, y, C_ACCENT, FONT_12);
    y += 15;
    snprintf(buf, sizeof(buf), "错词 %d   连续 %d 场", Store::countWrong(), Store::cfg.streak);
    gfx.text(buf, 4, y, C_WARN, FONT_12);
    y += 15;

    int acc = Store::cfg.totalAnswers ? (Store::cfg.totalCorrect * 100 / Store::cfg.totalAnswers) : 0;
    int tacc = Store::cfg.todayAnswers ? (Store::cfg.todayCorrect * 100 / Store::cfg.todayAnswers) : 0;
    snprintf(buf, sizeof(buf), "今日 新%d 复%d %d%%", Store::cfg.todayNew, Store::cfg.todayReview, tacc);
    gfx.text(buf, 4, y, C_FG, FONT_12);
    y += 15;
    snprintf(buf, sizeof(buf), "累计 %d 题  正确 %d%%", Store::cfg.totalAnswers, acc);
    gfx.text(buf, 4, y, C_DIM, FONT_12);
    y += 15;

    // 进度条
    gfx.drawRect(4, y + 2, SCR_W - 8, 10, C_BAR);
    int pct = total ? learned * 100 / total : 0;
    int fw = (SCR_W - 10) * pct / 100;
    if (fw > 0) gfx.fillRect(5, y + 3, fw, 8, C_ACCENT);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    gfx.textCenter(buf, y + 2, C_FG, FONT_12);

    footer("B 返回");
}

#define SETTINGS_N 12
static const char* SET_NAME[SETTINGS_N] = {
    "选择词册", "每日新词", "音效", "显示方向", "颜色模式", "屏保样式",
    "屏保延时", "电量显示", "WiFi 设置", "时间设置", "清空进度", "关于"
};
static const char* ORI_NAME[4] = {"正常", "镜像", "翻转", "旋转180"};
static const char* SAVER_SEC_NAME[6] = {"关闭", "30秒", "1分钟", "2分钟", "5分钟", "10分钟"};
static const uint16_t SAVER_SEC_VAL[6] = {0, 30, 60, 120, 300, 600};

static void drawSettings() {
    header("设置", "");
    if (scroll < 0) scroll = 0;
    if (scroll > SETTINGS_N - 7) scroll = SETTINGS_N - 7;
    for (int r = 0; r < 7; r++) {
        int i = scroll + r;
        if (i >= SETTINGS_N) break;
        const char* right = "";
        switch (i) {
            case 0: snprintf(buf, sizeof(buf), "%d/%d", __builtin_popcount(Store::cfg.bookMask & 0x1FFFFF), Store::bookCount()); right = buf; break;
            case 1: snprintf(buf, sizeof(buf), "%d", Store::cfg.dailyNew); right = buf; break;
            case 2: right = Store::cfg.soundOn ? "开" : "关"; break;
            case 3: right = ORI_NAME[Store::cfg.orient & 3]; break;
            case 4: right = Store::cfg.bgr ? "BGR" : "RGB"; break;
            case 5: right = Saver::name(Store::cfg.saverMode % Saver::M_COUNT); break;
            case 6: {
                int k = 2;
                for (int j = 0; j < 6; j++) if (SAVER_SEC_VAL[j] == Store::cfg.saverSec) k = j;
                right = SAVER_SEC_NAME[k];
                break;
            }
            case 7: right = Power::modeName(Store::cfg.battMode); break;
            case 8: right = Net::connected() ? "已连" : (Store::cfg.ssid[0] ? "已存" : "未设置"); break;
            case 9: right = Clock::synced() ? "已同步" : "未同步"; break;
            case 10: right = resetConfirm ? "再按 A" : ""; break;
            default: right = ""; break;
        }
        rowItem(i, LIST_TOP + r * ITEM_H, SET_NAME[i], right, sel == i);
    }
    footer("←→ 调整   A 进入   B 返回");
}

static void drawBooks() {
    int bc = Store::bookCount();
    snprintf(buf, sizeof(buf), "%d/%d", __builtin_popcount(Store::cfg.bookMask & 0x1FFFFF), bc);
    header("选择词册", buf);
    if (scroll < 0) scroll = 0;
    if (scroll > bc - 7) scroll = bc - 7;
    for (int r = 0; r < 7; r++) {
        int bi = scroll + r;
        if (bi >= bc) break;
        int y = LIST_TOP + r * ITEM_H;
        bool chosen = (bi == sel);
        if (chosen) gfx.fillRect(0, y - 1, SCR_W, ITEM_H, C_SEL);
        bool en = Store::bookEnabled(bi);
        gfx.text(en ? "■" : "□", 4, y, en ? C_ACCENT : C_DIM, FONT_12);
        gfx.textMax(Store::bookName(bi), 16, y, 100, C_FG, FONT_12);
        snprintf(buf, sizeof(buf), "%d", Store::bookWordCount(bi));
        gfx.textRight(buf, SCR_W - 4, y, C_DIM, FONT_12);
    }
    footer("A 勾选   ←全选   →清空   B 返回");
}

static void drawAbout() {
    header("关于", "");
    int y = BODY_TOP + 4;
    gfx.textCenter("小喵掌机 背单词固件", y, C_ACCENT, FONT_12); y += 16;
    snprintf(buf, sizeof(buf), "词库 %d 词 / %d 册", ws_count(), BOOK_COUNT);
    gfx.textCenter(buf, y, C_FG, FONT_12); y += 16;
    gfx.textCenter("人教版 PEP 小学→高中", y, C_FG, FONT_12); y += 16;
    gfx.textCenter("ST7735 160x128 · ESP32", y, C_DIM, FONT_12); y += 16;
    snprintf(buf, sizeof(buf), "Flash 4MB  第%d场", Store::cfg.dayIndex);
    gfx.textCenter(buf, y, C_DIM, FONT_12); y += 16;
    gfx.textCenter("B 返回", y + 4, C_DIM, FONT_12);
    footer("");
}

static void drawSummary() {
    header("本次完成", "");
    int y = BODY_TOP + 8;
    snprintf(buf, sizeof(buf), "新学 %d   复习 %d", sNew, sRev);
    gfx.textCenter(buf, y, C_FG, FONT_12); y += 18;
    snprintf(buf, sizeof(buf), "答对 %d   答错 %d", sOk, sBad);
    gfx.textCenter(buf, y, sBad ? C_WARN : C_OK, FONT_12); y += 18;
    int tot = sOk + sBad;
    snprintf(buf, sizeof(buf), "正确率 %d%%", tot ? sOk * 100 / tot : 0);
    gfx.textCenter(buf, y, C_ACCENT, FONT_12); y += 20;
    gfx.textCenter("A 继续    B 回家", y, C_DIM, FONT_12);
    footer("");
}

static void drawMsg() {
    header("提示", "");
    gfx.textCenter(msgLine, 50, C_WARN, FONT_12);
    gfx.textCenter("B 返回", 70, C_DIM, FONT_12);
    footer("");
}

// ===========================================================================
//  WiFi / 密码 / 时间
// ===========================================================================
#define WIFI_ROWS 7
static const char* WIFI_ITEM[WIFI_ROWS] = {"扫描并连接", "断开连接", "忘记此网络", "立即对时", "手动改时", "查看网址", "返回"};

static void drawWifi() {
    header("WiFi", "");
    const char* stt;
    uint16_t sc = C_DIM;
    switch (Net::status()) {
        case Net::CONNECTED:  stt = "已连接"; sc = C_OK; break;
        case Net::CONNECTING: stt = "连接中"; sc = C_WARN; break;
        case Net::FAILED:     stt = "连接失败"; sc = C_ERR; break;
        case Net::NOSAVED:    stt = "未设置"; break;
        default:              stt = "已关闭"; break;
    }
    int y = BODY_TOP + 1;
    snprintf(buf, sizeof(buf), "状态 %s", stt);
    gfx.text(buf, 4, y, sc, FONT_12); y += 13;
    snprintf(buf, sizeof(buf), "账号 %s", Store::cfg.ssid[0] ? Store::cfg.ssid : "--");
    gfx.textMax(buf, 4, y, SCR_W - 8, C_FG, FONT_12); y += 13;
    snprintf(buf, sizeof(buf), "IP %s", Net::connected() ? Net::ipString().c_str() : "--");
    gfx.textMax(buf, 4, y, SCR_W - 8, C_DIM, FONT_12); y += 15;
    for (int r = 0; r < WIFI_ROWS; r++) {
        rowItem(r, y + r * ITEM_H, WIFI_ITEM[r], "", wifiSel == r);
    }
    footer("A 执行   B 返回");
}

static void drawScan() {
    header("选择 WiFi", "");
    int n = Net::scanCount();
    if (wifiScanning) {
        gfx.textCenter("扫描中…", 50, C_WARN, FONT_12);
        gfx.textCenter("B 返回", 70, C_DIM, FONT_12);
        footer("");
        return;
    }
    if (n == 0) {
        gfx.textCenter("没有搜到网络", 46, C_DIM, FONT_12);
        gfx.textCenter("A 重新扫描   B 返回", 66, C_WARN, FONT_12);
        footer("");
        return;
    }
    if (wifiSel >= n) wifiSel = n - 1;
    if (wifiSel < 0) wifiSel = 0;
    if (wifiSel < wifiScroll) wifiScroll = wifiSel;
    if (wifiSel >= wifiScroll + 7) wifiScroll = wifiSel - 6;
    for (int r = 0; r < 7; r++) {
        int i = wifiScroll + r;
        if (i >= n) break;
        int y = LIST_TOP + r * ITEM_H;
        bool ch = (i == wifiSel);
        if (ch) gfx.fillRect(0, y - 1, SCR_W, ITEM_H, C_SEL);
        gfx.textMax(Net::scanSsid(i).c_str(), 4, y, 108, ch ? C_FG : C_FG, FONT_12);
        char rssi[12];
        int db = Net::scanRssi(i);
        snprintf(rssi, sizeof(rssi), "%d", db);
        gfx.textRight(rssi, SCR_W - 16, y, db > -67 ? C_OK : (db > -80 ? C_WARN : C_DIM), FONT_12);
        gfx.text(Net::scanEnc(i) ? "*" : " ", SCR_W - 10, y, C_WARN, FONT_12);
    }
    footer("A 连接   ↑↓ 选择   B 返回");
}

static void drawPw() {
    header("WiFi 密码", Net::scanSsid(pwIdx).c_str());
    gfx.textMax(Net::scanSsid(pwIdx).c_str(), 4, BODY_TOP + 1, SCR_W - 8, C_DIM, FONT_12);
    // 输入框
    gfx.fillRect(4, 30, SCR_W - 8, 14, C_PANEL);
    gfx.drawRect(4, 30, SCR_W - 8, 14, kbPos < 40 ? C_DIM : C_ACCENT);
    int maxShow = (SCR_W - 16) / 7;
    int from = pwLen > maxShow ? pwLen - maxShow : 0;
    char show[44];
    snprintf(show, sizeof(show), "%s", pwBuf + from);
    gfx.text(show, 8, 33, C_FG, FONT_12);
    if ((millis() / 400) & 1) gfx.fillRect(8 + (int)strlen(show) * 7, 33, 5, 9, C_ACCENT);

    // 键盘 8x5
    for (int i = 0; i < 40; i++) {
        int c = i % 8, r = i / 8;
        int x = 2 + c * 20, y = 48 + r * 13;
        bool cur = (kbPos == i);
        gfx.fillRect(x, y, 19, 12, cur ? C_ACCENT : C_PANEL);
        char one[2] = {PWKB[i], 0};
        gfx.text(one, x + 6, y, cur ? C_BG : C_FG, FONT_12);
    }
    // 三个按钮
    static const char* BTN3[3] = {"退格", "确定", "取消"};
    static const int BW[3] = {50, 52, 42};
    int bx[3] = {3, 57, 113};
    for (int i = 0; i < 3; i++) {
        bool cur = (kbPos == 40 + i);
        gfx.fillRect(bx[i], 113, BW[i], 13, cur ? C_ACCENT : C_PANEL);
        gfx.text(BTN3[i], bx[i] + 14, 114, cur ? C_BG : (i == 1 ? C_OK : C_FG), FONT_12);
    }
    // 无 footer，提示放最后一行下方不放
}

static void drawTime() {
    header("时间设置", "");
    static const char* LBL[5] = {"年", "月", "日", "时", "分"};
    int y = BODY_TOP + 2;
    for (int i = 0; i < 5; i++) {
        int x = 6 + i * 31;
        bool ch = (i == timeField);
        gfx.fillRect(x - 2, y - 1, 29, 24, ch ? C_SEL : C_PANEL);
        snprintf(buf, sizeof(buf), "%d", timeVal[i]);
        gfx.textCenter2(buf, x - 2, y + 1, 29, ch ? C_FG : C_FG, FONT_12);
        gfx.text(LBL[i], x + 10, y + 14, C_DIM, FONT_12);
    }
    y += 30;
    char prev[32];
    uint32_t u = Clock::now();
    if (u) {
        char d[24], t[16];
        Clock::fmtDate(u, d, sizeof(d));
        Clock::fmtTimeS(u, t, sizeof(t));
        snprintf(prev, sizeof(prev), "现在 %s %s", d, t);
    } else {
        snprintf(prev, sizeof(prev), "现在 未对时");
    }
    gfx.textCenter(prev, y, C_DIM, FONT_12);
    y += 15;
    gfx.textCenter(Net::connected() ? "也可连网自动对时" : "连网可自动对时", y, C_WARN, FONT_12);
    footer("←→ 切字段  ↑↓ 改   A 保存  B 返回");
}

// ===========================================================================
//  交互
// ===========================================================================
static void homeKeys(uint8_t k) {
    if (k & BTN_UP) { if (sel > 0) { sel--; Sound::click(); } if (sel < scroll) scroll = sel; markDraw(); }
    if (k & BTN_DOWN) { if (sel < 11) { sel++; Sound::click(); } if (sel > scroll + 6) scroll = sel - 6; markDraw(); }
    if (k & BTN_A) {
        Sound::click();
        switch (sel) {
            case 0: startSession(SM_CARD, false, 0); break;
            case 1: startSession(SM_CARD, true, 0); break;
            case 2: startSession(SM_QUIZ, false, 20); break;
            case 3: startSession(SM_SPELL, false, 15); break;
            case 4: scr = SCR_GLEVELS; gLevelSel = 0; break;
            case 5: matchStart(); break;
            case 6: scrambleStart(); break;
            case 7: hangStart(); break;
            case 8: chainStart(); break;
            case 9: scr = SCR_WRONG; wrongSel = 0; wrongScroll = 0; clearConfirm = false; break;
            case 10: scr = SCR_STATS; break;
            case 11: scr = SCR_SETTINGS; sel = 0; scroll = 0; resetConfirm = false; break;
        }
        markDraw();
    }
}

static void cardKeys(uint8_t k) {
    if (feedback) return;
    if (k & BTN_B) { Store::save(); scr = (qi > 0 || sOk + sBad > 0) ? SCR_SUMMARY : SCR_HOME; markDraw(); return; }
    if (!revealed) {
        if (k & (BTN_A | BTN_DOWN | BTN_RIGHT)) { revealed = true; Sound::click(); markDraw(); }
        return;
    }
    if (k & BTN_LEFT) { submitAnswer(false); return; }
    if (k & (BTN_RIGHT | BTN_A)) { submitAnswer(true); return; }
}

static void quizKeys(uint8_t k) {
    if (feedback) return;
    if (k & BTN_B) { Store::save(); scr = (sOk + sBad > 0) ? SCR_SUMMARY : SCR_HOME; markDraw(); return; }
    if (k & BTN_UP) { if (optSel > 0) { optSel--; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (optSel < 3) { optSel++; Sound::click(); } markDraw(); }
    if (k & BTN_A) { submitAnswer(optSel == optCorrect); }
}

static void spellKeys(uint8_t k) {
    if (feedback) return;
    int w = queue[qi];
    int len = (int)strlen(ws_en(w));
    if (len > 14) len = 14;
    if (k & BTN_B) {
        if (typedLen > 0) { typedLen--; Sound::click(); }
        else { Store::save(); scr = (sOk + sBad > 0) ? SCR_SUMMARY : SCR_HOME; }
        markDraw(); return;
    }
    if (k & BTN_UP) { if (kbPos >= 7) { kbPos -= 7; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (kbPos < 21) { kbPos += 7; Sound::click(); } markDraw(); }
    if (k & BTN_LEFT) { if (kbPos % 7 > 0) { kbPos--; Sound::click(); } markDraw(); }
    if (k & BTN_RIGHT) { if (kbPos % 7 < 6) { kbPos++; Sound::click(); } markDraw(); }
    // 提示：左+右 同时
    if ((Input::held() & BTN_LEFT) && (Input::held() & BTN_RIGHT)) {
        if (!hintUsed && typedLen < len) { typed[typedLen++] = ws_en(w)[0]; typed[typedLen] = 0; hintUsed = true; markDraw(); }
    }
    if (k & BTN_A) {
        if (kbPos < 26) {
            if (typedLen < len) { typed[typedLen++] = KB[kbPos]; typed[typedLen] = 0; Sound::click(); }
        } else if (kbPos == 26) {
            if (typedLen > 0) { typedLen--; Sound::click(); }
        } else {
            // 提交
            bool good = true;
            if (typedLen != len) good = false;
            else {
                for (int i = 0; i < len; i++) {
                    char a = typed[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                    char b = ws_en(w)[i]; if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                    if (a != b) { good = false; break; }
                }
            }
            submitAnswer(good);
            return;
        }
        markDraw();
    }
}

static void wrongKeys(uint8_t k) {
    int total = Store::countWrong();
    int items = total + 1;
    if (k & BTN_B) { scr = SCR_HOME; sel = 9; markDraw(); return; }
    if (k & BTN_UP) { if (wrongSel > 0) { wrongSel--; Sound::click(); } clearConfirm = false; markDraw(); }
    if (k & BTN_DOWN) { if (wrongSel < items - 1) { wrongSel++; Sound::click(); } clearConfirm = false; markDraw(); }
    if (k & BTN_LEFT) { if (wrongSel >= 7) { wrongSel -= 7; Sound::click(); } clearConfirm = false; markDraw(); }
    if (k & BTN_RIGHT) { if (wrongSel + 7 < items) { wrongSel += 7; Sound::click(); } clearConfirm = false; markDraw(); }
    if (k & BTN_A) {
        Sound::click();
        if (wrongSel == 0) {
            if (!clearConfirm) { clearConfirm = true; }
            else {
                for (int i = 0; i < ws_count(); i++) Store::setWrong(i, false);
                Store::save();
                clearConfirm = false;
                wrongSel = 0;
            }
        } else {
            // 从选中项开始练习全部错词
            buildQuizQueue(60, true);
            if (qn == 0) { markDraw(); return; }
            qi = 0;
            sNew = sRev = sOk = sBad = 0;
            int start = wrongSel - 1;
            if (start > 0 && start < qn) {
                // 把选中的词挪到最前
                int target = -1, cnt = 0;
                for (int i = 0; i < ws_count(); i++) {
                    if (Store::bookEnabled(ws_book(i)) && Store::isWrong(i)) {
                        if (cnt == start) { target = i; break; }
                        cnt++;
                    }
                }
                if (target >= 0) {
                    for (int i = 0; i < qn; i++) if (queue[i] == target) { queue[i] = queue[0]; queue[0] = target; break; }
                }
            }
            sessMode = SM_QUIZ;
            nextQuestion();
            scr = SCR_QUIZ;
        }
        markDraw();
    }
}

static void settingsKeys(uint8_t k) {
    if (k & BTN_UP) {
        if (sel > 0) { sel--; Sound::click(); }
        if (sel < scroll) scroll = sel;
        resetConfirm = false; markDraw();
    }
    if (k & BTN_DOWN) {
        if (sel < SETTINGS_N - 1) { sel++; Sound::click(); }
        if (sel > scroll + 6) scroll = sel - 6;
        resetConfirm = false; markDraw();
    }
    if (k & BTN_A) {
        Sound::click();
        switch (sel) {
            case 0:  scr = SCR_BOOKS; sel = 0; scroll = 0; break;
            case 7:  // 电量模式循环
                if (Store::cfg.battMode == 0 && Power::millivolts() < 0) Store::cfg.battMode = 2;
                else Store::cfg.battMode = (uint8_t)((Store::cfg.battMode + 1) % 3);
                Power::begin();
                Store::markDirty();
                break;
            case 8:  scr = SCR_WIFI; wifiSel = 0; wifiScroll = 0; break;
            case 9:
                if (Clock::now()) {
                    struct tm p; time_t tt = (time_t)Clock::now(); localtime_r(&tt, &p);
                    timeVal[0] = p.tm_year + 1900; timeVal[1] = p.tm_mon + 1;
                    timeVal[2] = p.tm_mday; timeVal[3] = p.tm_hour; timeVal[4] = p.tm_min;
                }
                timeField = 0; scr = SCR_TIME; break;
            case 10:
                if (!resetConfirm) resetConfirm = true;
                else {
                    Store::clearProgress();      // 复用 store 的实现，避免两处逻辑走偏
                    Store::cfg.totalAnswers = Store::cfg.totalCorrect = 0;
                    Store::cfg.streak = 1;
                    Store::save();
                    resetConfirm = false;
                    snprintf(msgLine, sizeof(msgLine), "进度已清空");
                    scr = SCR_MSG;
                }
                break;
            case 11: scr = SCR_ABOUT; break;
            default: break;
        }
        if (sel == 5) { Saver::enter(); saverOn = true; lastAct = millis(); }   // 直接预览屏保
        markDraw();
        return;
    }
    if (k & (BTN_LEFT | BTN_RIGHT)) {
        int dir = (k & BTN_LEFT) ? -1 : 1;
        switch (sel) {
            case 1: {
                const int opts[5] = {10, 15, 20, 30, 50};
                int cur = 2;
                for (int i = 0; i < 5; i++) if (opts[i] == Store::cfg.dailyNew) cur = i;
                cur += dir; if (cur < 0) cur = 0; if (cur > 4) cur = 4;
                Store::cfg.dailyNew = (uint8_t)opts[cur];
                Store::markDirty();
                break;
            }
            case 2:
                Store::cfg.soundOn = !Store::cfg.soundOn;
                Sound::setEnabled(Store::cfg.soundOn);
                Store::markDirty();
                break;
            case 3:
                Store::cfg.orient = (uint8_t)((Store::cfg.orient + (dir > 0 ? 1 : 3)) & 3);
                gfx.setOrientation(Store::cfg.orient);
                Store::markDirty();
                break;
            case 4:
                Store::cfg.bgr = !Store::cfg.bgr;
                gfx.setBgr(Store::cfg.bgr);
                Store::markDirty();
                break;
            case 5:
                Store::cfg.saverMode = (uint8_t)((Store::cfg.saverMode + (dir > 0 ? 1 : Saver::M_COUNT - 1)) % Saver::M_COUNT);
                Saver::enter();
                Store::cfg.saverSec = Store::cfg.saverSec ? Store::cfg.saverSec : 60;
                Store::markDirty();
                break;
            case 6: {
                int k2 = 2;
                for (int j = 0; j < 6; j++) if (SAVER_SEC_VAL[j] == Store::cfg.saverSec) k2 = j;
                k2 += dir; if (k2 < 0) k2 = 0; if (k2 > 5) k2 = 5;
                Store::cfg.saverSec = SAVER_SEC_VAL[k2];
                Store::markDirty();
                break;
            }
            case 7:
                Store::cfg.battMode = (uint8_t)((Store::cfg.battMode + (dir > 0 ? 1 : 2)) % 3);
                Power::begin();
                Store::markDirty();
                break;
            default: break;
        }
        Sound::click();
        markDraw();
    }
    if (k & BTN_B) { Store::save(); scr = SCR_HOME; sel = 11; scroll = 0; markDraw(); }
}

static void booksKeys(uint8_t k) {
    int bc = Store::bookCount();
    if (k & BTN_UP) { if (sel > 0) { sel--; Sound::click(); } if (sel < scroll) scroll = sel; markDraw(); }
    if (k & BTN_DOWN) { if (sel < bc - 1) { sel++; Sound::click(); } if (sel > scroll + 6) scroll = sel - 6; markDraw(); }
    if (k & BTN_A) { Store::setBookEnabled(sel, !Store::bookEnabled(sel)); Sound::click(); markDraw(); }
    if (k & BTN_LEFT) { for (int i = 0; i < bc; i++) Store::setBookEnabled(i, true); Sound::click(); markDraw(); }
    if (k & BTN_RIGHT) { for (int i = 0; i < bc; i++) Store::setBookEnabled(i, false); Sound::click(); markDraw(); }
    if (k & BTN_B) { Store::save(); scr = SCR_SETTINGS; sel = 0; scroll = 0; markDraw(); }
}

static void wifiKeys(uint8_t k) {
    if (k & BTN_UP) { if (wifiSel > 0) { wifiSel--; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (wifiSel < WIFI_ROWS - 1) { wifiSel++; Sound::click(); } markDraw(); }
    if (k & BTN_B) { scr = SCR_SETTINGS; sel = 8; markDraw(); return; }
    if (k & BTN_A) {
        Sound::click();
        switch (wifiSel) {
            case 0:
                Net::clearScan();
                Net::startScan();
                wifiScanning = true;
                wifiScanAt = millis();
                wifiSel = 0;
                wifiScroll = 0;
                scr = SCR_SCAN;
                break;
            case 1: Net::disconnect(); break;
            case 2: Net::forget(); break;
            case 3:
                if (!Net::syncTime()) { snprintf(msgLine, sizeof(msgLine), "请先连接 WiFi"); scr = SCR_MSG; }
                break;
            case 4:
                if (Clock::now()) {
                    struct tm p; time_t tt = (time_t)Clock::now(); localtime_r(&tt, &p);
                    timeVal[0] = p.tm_year + 1900; timeVal[1] = p.tm_mon + 1;
                    timeVal[2] = p.tm_mday; timeVal[3] = p.tm_hour; timeVal[4] = p.tm_min;
                }
                timeField = 0; scr = SCR_TIME;
                break;
            case 5:
                snprintf(msgLine, sizeof(msgLine), Net::connected() ? "xiaomiao.local" : "连网后可用");
                scr = SCR_MSG;
                break;
            default: scr = SCR_SETTINGS; sel = 8; break;
        }
        markDraw();
    }
}

static void scanKeys(uint8_t k) {
    if (wifiScanning) {
        if (Net::scanDone() || (uint32_t)(millis() - wifiScanAt) > 12000) wifiScanning = false;
        if (k & BTN_B) { scr = SCR_WIFI; wifiSel = 0; }
        markDraw();
        return;
    }
    int n = Net::scanCount();
    if (k & BTN_B) { scr = SCR_WIFI; wifiSel = 0; markDraw(); return; }
    if (k & BTN_A) {
        if (n == 0) {
            Net::clearScan(); Net::startScan(); wifiScanning = true; wifiScanAt = millis();
            markDraw(); return;
        }
        Sound::click();
        snprintf(pwSsid, sizeof(pwSsid), "%s", Net::scanSsid(wifiSel).c_str());
        pwIdx = wifiSel;
        pwLen = 0; pwBuf[0] = 0; kbPos = 0;
        if (Net::scanEnc(wifiSel) == 0) {
            Net::connectTo(pwSsid, "");
            scr = SCR_WIFI; wifiSel = 0;
        } else {
            scr = SCR_PW;
        }
        markDraw();
        return;
    }
    if (k & BTN_UP) { if (wifiSel > 0) { wifiSel--; Sound::click(); } markDraw(); }
    if (k & BTN_DOWN) { if (wifiSel < n - 1) { wifiSel++; Sound::click(); } markDraw(); }
    if (k & BTN_LEFT) { if (wifiSel >= 7) { wifiSel -= 7; Sound::click(); } markDraw(); }
    if (k & BTN_RIGHT) { if (wifiSel + 7 < n) { wifiSel += 7; Sound::click(); } markDraw(); }
}

static void pwKeys(uint8_t k) {
    int row = (kbPos < 40) ? kbPos / 8 : 5;
    int col = (kbPos < 40) ? kbPos % 8 : kbPos - 40;
    if (k & BTN_UP) {
        if (row == 5) kbPos = 32 + col * 2 > 39 ? 39 : 32 + col * 2;
        else if (row > 0) kbPos -= 8;
        Sound::click(); markDraw();
    }
    if (k & BTN_DOWN) {
        if (row < 4) kbPos += 8;
        else if (row == 4) kbPos = 40 + (col / 3);
        Sound::click(); markDraw();
    }
    if (k & BTN_LEFT) {
        if (row < 5) { if (col > 0) kbPos = row * 8 + col - 1; }
        else if (col > 0) kbPos = 40 + col - 1;
        Sound::click(); markDraw();
    }
    if (k & BTN_RIGHT) {
        if (row < 5) { if (col < 7) kbPos = row * 8 + col + 1; }
        else if (col < 2) kbPos = 40 + col + 1;
        Sound::click(); markDraw();
    }
    if (k & BTN_B) {   // B 直接退格
        if (pwLen > 0) { pwBuf[--pwLen] = 0; Sound::click(); }
        markDraw();
    }
    if (k & BTN_A) {
        Sound::click();
        if (kbPos < 40) {
            if (pwLen < (int)sizeof(pwBuf) - 1) { pwBuf[pwLen++] = PWKB[kbPos]; pwBuf[pwLen] = 0; }
        } else if (kbPos == 40) {
            if (pwLen > 0) pwBuf[--pwLen] = 0;
        } else if (kbPos == 41) {
            Net::connectTo(pwSsid, pwBuf);
            scr = SCR_WIFI; wifiSel = 0;
        } else {
            scr = SCR_SCAN;
        }
        markDraw();
    }
}

static void timeKeys(uint8_t k) {
    static const int LIM[5][2] = {{2000, 2099}, {1, 12}, {1, 31}, {0, 23}, {0, 59}};
    if (k & BTN_LEFT) { if (timeField > 0) timeField--; Sound::click(); markDraw(); }
    if (k & BTN_RIGHT) { if (timeField < 4) timeField++; Sound::click(); markDraw(); }
    if (k & BTN_UP) {
        timeVal[timeField]++;
        if (timeVal[timeField] > LIM[timeField][1]) timeVal[timeField] = LIM[timeField][0];
        Sound::click(); markDraw();
    }
    if (k & BTN_DOWN) {
        timeVal[timeField]--;
        if (timeVal[timeField] < LIM[timeField][0]) timeVal[timeField] = LIM[timeField][1];
        Sound::click(); markDraw();
    }
    if (k & BTN_A) {
        // 日按当月天数夹紧
        int dim = Clock::daysInMonth(timeVal[0], timeVal[1]);
        if (dim > 0 && timeVal[2] > dim) timeVal[2] = dim;
        Clock::setManual(timeVal[0], timeVal[1], timeVal[2], timeVal[3], timeVal[4], 0);
        Sound::ok();
        snprintf(msgLine, sizeof(msgLine), "时间已设置为 %d-%02d-%02d %02d:%02d",
                 timeVal[0], timeVal[1], timeVal[2], timeVal[3], timeVal[4]);
        scr = SCR_MSG;
        markDraw();
    }
    if (k & BTN_B) { scr = SCR_SETTINGS; sel = 9; markDraw(); }
}

// ===========================================================================
//  屏保
// ===========================================================================
bool inScreensaver() { return saverOn; }

// 自动化测试用：临时覆盖屏保闲置时长（秒），不写入 NVS；sec<0 恢复跟随设置
void setSaverSecRuntime(int sec) {
    saverSecRt = (sec < 0 || sec > 65534) ? 0xFFFF : (uint16_t)sec;
    lastAct = millis();                 // 立即重新计时，避免切换瞬间误触
}

// 有按键到来：刷新活跃时间；若正在屏保，退出屏保并"消费"掉这个按键
// （唤醒键不穿透到当前界面，避免醒屏瞬间误触功能——比如设置里预览屏保时按 A 又触发预览）
bool noteActivity() {
    lastAct = millis();
    if (saverOn) {
        saverOn = false;
        markDraw();
        return true;
    }
    return false;
}

void tick(uint32_t nowMs) {
    if (saverOn) {
        if ((uint32_t)(nowMs - saverFrame) >= SAVER_FRAME_MS) {
            saverFrame = nowMs;
            markDraw();
        }
        return;
    }
    // 防误触：lastAct 可能在本帧 update() 内刚被刷新（例如进游戏/退出屏保时），
    // 此时 nowMs 比 lastAct 旧，直接相减会无符号下溢成 ~42.9 亿，被误判为闲置过久。
    // 只有 nowMs >= lastAct 时才比较闲置时长；下一帧 nowMs 变新后自然恢复。
    uint32_t secs = (saverSecRt != 0xFFFF) ? saverSecRt : Store::cfg.saverSec;
    if (secs && nowMs >= lastAct &&
        (uint32_t)(nowMs - lastAct) > secs * 1000u) {
        saverOn = true;
        Saver::enter();
        saverFrame = nowMs;
        markDraw();
    }
    gameTick(nowMs);
}

void gameDump() {
    Serial.printf("[game] scr=%d lv=%d q=%d/%d correct=%d bad=%d lives=%d combo=%d timer=%d fb=%d optC=%d optSel=%d qmode=%d\n",
        (int)scr, gLevel, gQIdx, GAME_QN, gCorrect, gBad, gLives, gCombo, gTimer, feedback, optCorrect, optSel, gQMode);
    if (scr == SCR_HANG) {
        Serial.printf("[hang] word=%s shown=%s lives=%d kb=%d phase=%d score=%d ok=%d/%d\n",
                      ws_en(hWord), hShown, hLives, hKbPos, hPhase, hScore, hCorrect, hTotal);
    } else if (scr == SCR_CHAIN) {
        Serial.printf("[chain] cur=%s opts=%s,%s,%s correct=%d sel=%d lives=%d combo=%d round=%d phase=%d\n",
                      ws_en(cWord), ws_en(cOpts[0]), ws_en(cOpts[1]), ws_en(cOpts[2]),
                      cCorrect, cSel, cLives, cCombo, cRound, cPhase);
    } else if (scr == SCR_SCRAMBLE) {
        Serial.printf("[scr] word=%s len=%d pos=%d sel=%d lives=%d score=%d ok=%d/%d order=",
                      ws_en(sWord), sLen, sPos, sSelIdx, sLives, sScore, sCorrect, sTotal);
        for (int i = 0; i < sLen; i++) Serial.printf("%d,", sScrambled[i]);
        Serial.printf(" built=");
        for (int i = 0; i < sLen; i++) Serial.printf("%d,", sBuilt[i]);
        Serial.println();
    } else if (scr == SCR_MATCH) {
        Serial.printf("[match] cards=");
        for (int i = 0; i < M_CARDS; i++) Serial.printf("%d,", mCards[i]);
        Serial.printf(" matched=");
        for (int i = 0; i < M_CARDS; i++) Serial.printf("%d,", mMatched[i]);
        Serial.printf(" cursor=%d sel1=%d sel2=%d score=%d tries=%d level=%d\n",
                      mCursor, mSel1, mSel2, mScore, mTries, mLevel);
    }
}

// ===========================================================================
//  自检
// ===========================================================================
static int gPass, gFail;
static void ck(bool cond, const char* what) {
    if (cond) gPass++;
    else { gFail++; Serial.print("  [FAIL] "); Serial.println(what); }
}

static bool glyphOk(uint32_t cp) {
    for (int f = 0; f < FONT_COUNT; f++) {
        const FontDef& fd = FONTS[f];
        int lo = 0, hi = (int)fd.count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            uint16_t m = fd.glyphs[mid].cp;
            if (m == cp) return true;
            if (m < cp) lo = mid + 1; else hi = mid - 1;
        }
    }
    return false;
}

// 检查字符串里每个字符是否至少在某一套字号里有字模
static void scanText(const char* s, const char* what, int* miss) {
    const char* p = s;
    while (*p) {
        uint8_t c = (uint8_t)*p;
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        for (int i = 0; i < n && p[1]; i++) { p++; cp = (cp << 6) | ((uint8_t)*p & 0x3F); }
        p++;
        if (cp == ' ' || cp == '\n') continue;
        if (!glyphOk(cp)) {
            (*miss)++;
            if (*miss <= 5) Serial.printf("  [字体缺字] %s U+%04X\n", what, (unsigned)cp);
        }
    }
}

void debugKey(uint8_t mask) {
    if (noteActivity()) return;   // 与真实按键路径一致：唤醒键被消费，不穿透
    update(mask);
}

int screenId() { return (int)scr; }

void selfTest() {
    gPass = gFail = 0;
    Serial.println("\n===== 小喵背单词 自检 =====");
    uint32_t t0 = millis();
    Serial.printf("  DRAM 空闲 %u 最低水位 %u  PSRAM 空闲 %u\n",
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 1. 词库
    int n = ws_count();
    ck(n >= BASE_WORD_COUNT, "词库数量");
    int badEn = 0, badZh = 0;
    for (int i = 0; i < n; i++) {
        const char* e = ws_en(i);
        const char* z = ws_zh(i);
        if (!e || !e[0]) badEn++;
        if (!z || !z[0]) badZh++;
    }
    ck(badEn == 0, "存在空英文词");
    ck(badZh == 0, "存在空释义");
    Serial.printf("  词库 %d（自定义 %d）空英文=%d 空释义=%d\n", n, ws_userCount(), badEn, badZh);

    // 2. 词册
    int sum = 0;
    for (int b = 0; b < Store::bookCount(); b++) sum += Store::bookWordCount(b);
    ck(sum >= BASE_WORD_COUNT, "词册统计");
    Serial.printf("  词册 %d 组合计 %d\n", Store::bookCount(), sum);

    // 3. 调度算法
    {
        WordState s;
        memset(&s, 0, sizeof(s));
        int today = 9700;
        Store::schedule(s, true, today);
        ck(s.interval == 1 && s.due == today + 1, "首次答对 -> 1 天");
        Store::schedule(s, true, today);
        ck(s.interval == 3, "第二次答对 -> 3 天");
        uint16_t iv3 = s.interval;
        Store::schedule(s, true, today);
        ck(s.interval > iv3, "第三次答对间隔增长");
        ck(s.status == 2, "进入复习中");
        Store::schedule(s, false, today);
        ck(s.interval == 1 && s.status == 1 && (s.flags & 1), "答错回退并进错词本");
        ck(s.ease10 <= 25, "难度系数下降");
        for (int i = 0; i < 40; i++) Store::schedule(s, true, today);
        ck(s.interval <= 3650, "间隔有上限");
    }

    // 4. 日期换算
    ck(Store::unixToDay(0) == 10957, "1970-01-01 -> 第 10957 天");
    ck(Clock::daysInMonth(2024, 2) == 29, "2024 闰年");
    ck(Clock::daysInMonth(2100, 2) == 28, "2100 非闰年");
    ck(Clock::validDate(2026, 2, 30) == false, "非法日期被拒");

    // 5. 字体覆盖（抽样）
    {
        int miss = 0;
        for (int i = 0; i < n; i += 7) scanText(ws_en(i), ws_en(i), &miss);
        for (int i = 0; i < n; i += 11) scanText(ws_zh(i), ws_en(i), &miss);
        for (int i = 0; i < n; i += 13) if (ws_ph(i)[0]) scanText(ws_ph(i), ws_en(i), &miss);
        Serial.printf("  字体抽样缺字 %d 处\n", miss);
        ck(miss == 0, "字模覆盖");
        const char* ui[] = {"今日学习", "快速复习", "四选一测验", "拼写练习", "单词闯关", "错词本", "统计", "设置",
                            "选择词册", "每日新词", "音效", "显示方向", "颜色模式", "屏保样式", "屏保延时",
                            "电量显示", "WiFi 设置", "时间设置", "清空进度", "关于", "小喵背单词",
                            "数字时钟", "模拟表盘", "星空穿越", "飘雪", "弹跳方块", "水波", "单词轮播", "猫咪",
                            "扫描并连接", "断开连接", "忘记此网络", "立即对时", "手动改时", "查看网址",
                            "未对时", "连网后可用", "xiaomiao.local", "按 A 显示释义", "← 忘了       记得 →",
                            "↑↓ 选择   A 确认   B 结束", "方向选  A输入  B退格", "A 练习此项   B 返回",
                            "←→ 调整   A 进入   B 返回", "A 勾选   ←全选   →清空   B 返回", "B 返回",
                            "第1关 1/10", "星0 关0", "连击 x2", "通关", "挑战结束", "好棒，闯关成功",
                            "差一点点，再来", "答对 8/10   最高连击 3", "第1关  星级 3",
                            "A 再来一次   B 选关", "←→↑↓ 选关  A 开始  B 返回",
                            "配对消消乐", "配对完成", "太棒了！", "共配对", "尝试", "A 再来   B 返回",
                            "方向选   A 翻牌配对   B 退出", "字母重组", "字母重组 1", "猜单词", "猜单词结束",
                            "猜对啦！", "答案是", "方向选字母   A 猜   B 退出", "单词接龙", "接龙结束",
                            "尾字母", "连击x", "游戏结束", "猜对", "得分", "接龙"};
        int um = 0;
        for (size_t i = 0; i < sizeof(ui) / sizeof(ui[0]); i++) scanText(ui[i], ui[i], &um);
        Serial.printf("  界面文案缺字 %d 处\n", um);
        ck(um == 0, "界面文案字模");
    }

    // 6. LittleFS 读写
    {
        bool ok = false;
        File f = LittleFS.open("/selftest.txt", "w");
        if (f) { f.print("ok"); f.close(); ok = LittleFS.exists("/selftest.txt"); }
        if (ok) LittleFS.remove("/selftest.txt");
        ck(ok, "LittleFS 可写");
        ck(Store::fsReady(), "LittleFS 已挂载");
    }

    // 7. 显示输出（逐屏渲染一遍，看有无异常耗时/崩溃）
    {
        Screen saveScr = scr;
        uint32_t t1 = millis();
        for (int s = 0; s <= SCR_CHAINEND; s++) {
            scr = (Screen)s;
            gfx.clear(C_BG);
            render();
        }
        uint32_t dt = millis() - t1;
        scr = saveScr;
        Serial.printf("  26 屏渲染耗时 %lu ms\n", (unsigned long)dt);
        ck(dt < 4000, "渲染耗时正常");
        gfx.flush();
    }

    // 8. 屏保 8 种各画 3 帧
    {
        uint32_t t1 = millis();
        for (int m = 0; m < Saver::M_COUNT; m++) {
            Saver::enter();
            for (int f = 0; f < 3; f++) {
                gfx.clear(C_BG);
                Saver::draw(m, f * 100);
            }
        }
        uint32_t dt = millis() - t1;
        Serial.printf("  屏保 %d 种 x3 帧耗时 %lu ms\n", (int)Saver::M_COUNT, (unsigned long)dt);
        ck(dt < 6000, "屏保渲染耗时");
        gfx.flush();
    }

    // 9. 电量
    {
        ck(Power::estimatePercent(0) == 100, "满电 100%");
        ck(Power::estimatePercent(300) == 50, "半程 50%");
        ck(Power::estimatePercent(10000) == 0, "超期 0%");
        int p = Power::percent();
        ck(p >= -1 && p <= 100, "电量取值范围");
        Serial.printf("  电量 %d%%（模式 %s）\n", p, Power::modeName(Store::cfg.battMode));
    }

    // 9b. 屏保误触竞态回归（now 旧于 lastAct 时不得开启/重开屏保）
    {
        Screen  svScr = scr;
        bool    svSaver = saverOn;
        int     svFb = feedback;
        uint32_t svFbEnd = feedbackEnd;
        uint16_t svSec = Store::cfg.saverSec;
        uint16_t svRt = saverSecRt;

        scr = SCR_HOME;                 // 离开游戏屏，避免 gameTick 副作用
        feedback = 0;
        saverOn = false;
        Store::cfg.saverSec = 30;
        saverSecRt = 0xFFFF;            // 屏蔽测试用的运行时覆盖，保证用例确定性

        // (a) 本帧内刷新 lastAct 后，用过旧的时间戳 tick：绝不允许开启屏保
        uint32_t t = millis();
        delay(3);
        noteActivity();                 // lastAct = millis() > t（模拟 update() 内刷新）
        tick(t);
        ck(!saverOn, "屏保防误触：时间戳旧于 lastAct");

        // (b) 退出屏保的那个按键，不得让屏保立刻重开
        saverOn = true;
        uint32_t t2 = millis();
        delay(3);
        update(BTN_A);                  // 内部 saverOn=false, lastAct=millis()>t2
        ck(!saverOn, "按键可退出屏保");
        tick(t2);
        ck(!saverOn, "退出屏保后不立刻重开");

        // (c) 正常路径仍然有效：闲置超时必须能开启
        tick(millis() + 31000);
        ck(saverOn, "闲置超时正常开启屏保");

        saverOn = svSaver;
        scr = svScr;
        feedback = svFb;
        feedbackEnd = svFbEnd;
        Store::cfg.saverSec = svSec;
        saverSecRt = svRt;
        lastAct = millis();
    }

    // 9c. I2C 灯控（静音时用灯光反馈）
    {
        bool on0 = Lights::available();
        ck(on0, "I2C 灯控 0x40 在线");
        Serial.printf("  灯控 0x40 %s（SCL=%d SDA=%d，静音反馈%s）\n",
                      on0 ? "在线" : "不在线", PIN_I2C_SCL, PIN_I2C_SDA,
                      on0 ? "可用" : "不可用");
        if (on0) {
            Lights::pulse(2, 80, 80);   // 非阻塞登记，接下来几秒内闪 2 下，肉眼可确认
            ck(Lights::off(), "灯控寄存器可写");
        }
    }

    // 10. 网络状态
    Serial.printf("  WiFi status=%d ssid=%s ip=%s 时间=%lu 同步=%d\n",
                  (int)Net::status(), Store::cfg.ssid, Net::ipString().c_str(),
                  (unsigned long)Clock::now(), (int)Clock::synced());

    Serial.printf("===== 自检完成：通过 %d 失败 %d 用时 %lu ms =====\n",
                  gPass, gFail, (unsigned long)(millis() - t0));
}

void selfTestSaver() {
    Serial.println("\n===== 屏保自检 =====");
    for (int m = 0; m < Saver::M_COUNT; m++) {
        Saver::enter();
        uint32_t t0 = millis();
        for (int f = 0; f < 10; f++) {
            gfx.clear(C_BG);
            Saver::draw(m, f * 120);
        }
        gfx.flush();
        Serial.printf("  [%d] %s 10 帧 %lu ms\n", m, Saver::name(m), (unsigned long)(millis() - t0));
    }
    Serial.println("===== 屏保自检结束 =====");
}

// ===========================================================================
//  对外接口
// ===========================================================================
void init() {
    sel = 0;
    scr = SCR_HOME;
    saverOn = false;
    lastAct = millis();
    Sound::setEnabled(Store::cfg.soundOn != 0);
    gfx.setOrientation(Store::cfg.orient);
    gfx.setBgr(Store::cfg.bgr != 0);
    markDraw();
}

void update(uint8_t k) {
    if (feedback && (int32_t)(millis() - feedbackEnd) >= 0) {
        if (scr == SCR_GPLAY) gameAdvance();   // 闯关用闯关的推进
        else advance();                        // 学习/测验/拼写用学习推进
        return;
    }
    if (saverOn) {
        if (k) { saverOn = false; lastAct = millis(); markDraw(); }
        return;
    }
    if (k == 0) return;
    switch (scr) {
        case SCR_HOME:     homeKeys(k); break;
        case SCR_GLEVELS:  gameKeysLevels(k); break;
        case SCR_GPLAY:    gameKeysPlay(k); break;
        case SCR_GRESULT:  gameKeysResult(k); break;
        case SCR_MATCH:    matchKeys(k); break;
        case SCR_MATCHRESULT: matchResultKeys(k); break;
        case SCR_SCRAMBLE: scrambleKeys(k); break;
        case SCR_SCRAMBLERESULT: scrambleResultKeys(k); break;
        case SCR_HANG:     hangKeys(k); break;
        case SCR_CHAIN:    chainKeys(k); break;
        case SCR_LEARN:    cardKeys(k); break;
        case SCR_QUIZ:     quizKeys(k); break;
        case SCR_SPELL:    spellKeys(k); break;
        case SCR_WRONG:    wrongKeys(k); break;
        case SCR_STATS:    if (k & (BTN_B | BTN_A)) { scr = SCR_HOME; sel = 10; } markDraw(); break;
        case SCR_SETTINGS: settingsKeys(k); break;
        case SCR_BOOKS:    booksKeys(k); break;
        case SCR_WIFI:     wifiKeys(k); break;
        case SCR_SCAN:     scanKeys(k); break;
        case SCR_PW:       pwKeys(k); break;
        case SCR_TIME:     timeKeys(k); break;
        case SCR_ABOUT:    if (k & (BTN_B | BTN_A)) { scr = SCR_SETTINGS; sel = 11; } markDraw(); break;
        case SCR_SUMMARY:
            if (k & BTN_A) { startSession(sessMode, false, sessMode == SM_QUIZ ? 20 : 15); }
            else if (k & BTN_B) { scr = SCR_HOME; }
            markDraw();
            break;
        case SCR_MSG:
            if (k & (BTN_B | BTN_A)) { scr = SCR_HOME; }
            markDraw();
            break;
        case SCR_HANGEND:
            if (k & BTN_A) hangStart();
            else if (k & BTN_B) { scr = SCR_HOME; sel = 7; }
            markDraw();
            break;
        case SCR_CHAINEND:
            if (k & BTN_A) chainStart();
            else if (k & BTN_B) { scr = SCR_HOME; sel = 8; }
            markDraw();
            break;
    }
}

void render() {
    gfx.clear(C_BG);
    if (saverOn) {
        Saver::draw(Store::cfg.saverMode % Saver::M_COUNT, millis());
        return;
    }
    switch (scr) {
        case SCR_HOME:     drawHome(); break;
        case SCR_GLEVELS:  drawGLevels(); break;
        case SCR_GPLAY:    drawGPlay(); break;
        case SCR_GRESULT:  drawGResult(); break;
        case SCR_MATCH:    drawMatch(); break;
        case SCR_MATCHRESULT: drawMatchResult(); break;
        case SCR_SCRAMBLE: drawScramble(); break;
        case SCR_SCRAMBLERESULT: drawScrambleResult(); break;
        case SCR_HANG:     drawHang(); break;
        case SCR_HANGEND:  drawHangEnd(); break;
        case SCR_CHAIN:    drawChain(); break;
        case SCR_CHAINEND: drawChainEnd(); break;
        case SCR_LEARN:    drawCard(); break;
        case SCR_QUIZ:     drawQuiz(); break;
        case SCR_SPELL:    drawSpell(); break;
        case SCR_WRONG:    drawWrongList(); break;
        case SCR_STATS:    drawStats(); break;
        case SCR_SETTINGS: drawSettings(); break;
        case SCR_BOOKS:    drawBooks(); break;
        case SCR_WIFI:     drawWifi(); break;
        case SCR_SCAN:     drawScan(); break;
        case SCR_PW:       drawPw(); break;
        case SCR_TIME:     drawTime(); break;
        case SCR_ABOUT:    drawAbout(); break;
        case SCR_SUMMARY:  drawSummary(); break;
        case SCR_MSG:      drawMsg(); break;
    }
}

bool consumeDirty() {
    bool d = needDraw;
    needDraw = false;
    return d;
}

}  // namespace App
