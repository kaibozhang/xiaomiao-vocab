#include "clock.h"
#include "store.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace Clock {

static uint32_t sBase = 0;        // 上次已知时间的 unix
static uint32_t sBaseMs = 0;      // 对应的 millis
static bool     sSynced = false;  // 是否由 NTP/手动设置过（本次开机）
static bool     sEver = false;
static uint32_t sLastPersist = 0;

static const char* WD[7] = {"日", "一", "二", "三", "四", "五", "六"};

const char* weekName(int wd) { return (wd >= 0 && wd < 7) ? WD[wd] : "-"; }

int daysInMonth(int y, int m) {
    static const int T[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return T[m - 1];
}

bool validDate(int y, int m, int d) {
    if (y < 2000 || y > 2099 || m < 1 || m > 12) return false;
    return d >= 1 && d <= daysInMonth(y, m);
}

void begin() {
    setenv("TZ", "CST-8", 1);
    tzset();
    sBase = Store::cfg.lastUnix;
    sBaseMs = millis();
    sEver = (sBase > 1000000000u);
    sSynced = false;
}

void tick() {
    uint32_t t = (uint32_t)time(NULL);
    if (t > 1000000000u) {
        if (!sSynced) {
            sSynced = true;
            sEver = true;
            Store::cfg.syncDone = 1;
            Store::markDirty();
        }
        sBase = t;
        sBaseMs = millis();
    }
    uint32_t nowMs = millis();
    // 每 5 分钟把当前时间存一次，掉电后也能大致接上
    if (sSynced && (uint32_t)(nowMs - sLastPersist) > 300000) {
        sLastPersist = nowMs;
        Store::cfg.lastUnix = (uint32_t)time(NULL);
        Store::markDirty();
    }
}

uint32_t now() {
    uint32_t t = (uint32_t)time(NULL);
    if (t > 1000000000u) return t;
    if (sBase > 1000000000u) return sBase + (millis() - sBaseMs) / 1000u;
    return 0;
}

bool synced() { return sSynced || (uint32_t)time(NULL) > 1000000000u; }
bool everSynced() { return sEver; }

void setManual(int y, int mo, int d, int h, int mi, int s) {
    if (!validDate(y, mo, d)) return;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = s;
    time_t t = mktime(&tmv);
    if (t <= 0) return;
    struct timeval tv = {t, 0};
    settimeofday(&tv, NULL);
    sSynced = true;
    sEver = true;
    sBase = (uint32_t)t;
    sBaseMs = millis();
    Store::cfg.lastUnix = sBase;
    Store::cfg.syncDone = 1;
    Store::markDirty();
    Serial.printf("[clock] manual set -> %lu\n", (unsigned long)t);
}

void setUnix(uint32_t u) {
    if (u < 1000000000u) return;
    struct timeval tv = {(time_t)u, 0};
    settimeofday(&tv, NULL);
    sSynced = true;
    sEver = true;
    sBase = u;
    sBaseMs = millis();
    Store::cfg.lastUnix = u;
    Store::cfg.syncDone = 1;
    Store::markDirty();
    Serial.printf("[clock] 授时 -> %lu\n", (unsigned long)u);
}

static void localParts(uint32_t t, struct tm& out) {    time_t tt = (time_t)t;
    localtime_r(&tt, &out);
}

int hour() { struct tm p; localParts(now(), p); return p.tm_hour; }
int minute() { struct tm p; localParts(now(), p); return p.tm_min; }
int second() { struct tm p; localParts(now(), p); return p.tm_sec; }
int year() { struct tm p; localParts(now(), p); return p.tm_year + 1900; }
int month() { struct tm p; localParts(now(), p); return p.tm_mon + 1; }
int day() { struct tm p; localParts(now(), p); return p.tm_mday; }
int weekday() { struct tm p; localParts(now(), p); return p.tm_wday; }

void fmtTime(uint32_t t, char* out, size_t cap, bool h24) {
    if (t == 0) { snprintf(out, cap, "--:--"); return; }
    struct tm p;
    localParts(t, p);
    int h = p.tm_hour;
    if (!h24) {
        int ap = (h < 12) ? 0 : 1;
        int hh = h % 12;
        if (hh == 0) hh = 12;
        snprintf(out, cap, "%d:%02d%s", hh, p.tm_min, ap ? "pm" : "am");
        return;
    }
    snprintf(out, cap, "%02d:%02d", h, p.tm_min);
}

void fmtTimeS(uint32_t t, char* out, size_t cap) {
    if (t == 0) { snprintf(out, cap, "--:--:--"); return; }
    struct tm p;
    localParts(t, p);
    snprintf(out, cap, "%02d:%02d:%02d", p.tm_hour, p.tm_min, p.tm_sec);
}

void fmtDate(uint32_t t, char* out, size_t cap) {
    if (t == 0) { snprintf(out, cap, "----/--/--"); return; }
    struct tm p;
    localParts(t, p);
    snprintf(out, cap, "%04d-%02d-%02d", p.tm_year + 1900, p.tm_mon + 1, p.tm_mday);
}

void fmtDateCn(uint32_t t, char* out, size_t cap) {
    if (t == 0) { snprintf(out, cap, "尚未对时"); return; }
    struct tm p;
    localParts(t, p);
    snprintf(out, cap, "%d月%d日 周%s", p.tm_mon + 1, p.tm_mday, weekName(p.tm_wday));
}

}  // namespace Clock
