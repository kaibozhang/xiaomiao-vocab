#include <Arduino.h>
#include <stdio.h>
#include "hal.h"
#include "store.h"
#include "app.h"
#include "config.h"
#include "words.h"
#include "clock.h"
#include "power.h"
#include "net.h"

static void bootScreen() {
    gfx.clear(C_BG);
    gfx.textCenter("小喵背单词", 24, C_ACCENT, FONT_16);
    gfx.textCenter("人教版 PEP 小学→高中", 56, C_FG, FONT_12);
    char b[48];
    snprintf(b, sizeof(b), "%d 词 / %d 册", ws_count(), BOOK_COUNT);
    gfx.textCenter(b, 74, C_DIM, FONT_12);
    if (Store::cfg.ssid[0]) {
        snprintf(b, sizeof(b), "WiFi %s", Store::cfg.ssid);
        gfx.textCenter(b, 92, C_DIM, FONT_12);
    } else {
        gfx.textCenter("A 开始", 92, C_DIM, FONT_12);
    }
    gfx.flush();
}

static void serialCmd() {
    static char line[64];
    static size_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            line[n] = 0;
            n = 0;
            if (!line[0]) return;
            switch (line[0]) {
                case 't': App::selfTest(); break;
                case 'e': App::selfTestSaver(); break;
                case 'i':
                    Serial.printf("[info] words=%d user=%d fs=%d wifi=%d ip=%s time=%lu synced=%d batt=%d due=%d new=%d wrong=%d\n",
                                  ws_count(), ws_userCount(), (int)Store::fsReady(),
                                  (int)Net::status(), Net::ipString().c_str(),
                                  (unsigned long)Clock::now(), (int)Clock::synced(),
                                  Power::percent(), Store::countDue(), Store::countNew(),
                                  Store::countWrong());
                    break;
                case 's': Store::save(); Serial.println("[cmd] saved"); break;
                case 'w':
                    Serial.printf("[wifi] status=%d ssid=%s ip=%s rssi=%d\n",
                                  (int)Net::status(), Store::cfg.ssid,
                                  Net::ipString().c_str(), Net::rssi());
                    break;
                case 'n': Net::syncTime(); Serial.println("[cmd] ntp triggered"); break;
                case 'S': {          // 设置并保存 WiFi：S<ssid>,<pass>
                    char* comma = strchr(line + 1, ',');
                    if (comma) {
                        *comma = 0;
                        const char* ssid = line + 1;
                        const char* pass = comma + 1;
                        Net::connectTo(ssid, pass);
                        Serial.printf("[wifi] 已设置 %s，开始连接…\n", ssid);
                    } else {
                        Serial.println("[wifi] 用法: S<ssid>,<pass>");
                    }
                    break;
                }
                case 'W': {          // WiFi 扫描（验证射频硬件）
                    if (!Net::startScan()) { Serial.println("[scan] 无法启动扫描"); break; }
                    uint32_t s0 = millis();
                    while (!Net::scanDone() && (uint32_t)(millis() - s0) < 12000) delay(80);
                    int n2 = Net::scanCount();
                    Serial.printf("[scan] 发现 %d 个热点，用时 %lu ms\n", n2,
                                  (unsigned long)(millis() - s0));
                    for (int i = 0; i < n2 && i < 10; i++) {
                        Serial.printf("   %2d) %-24s %4d dBm  %s\n", i,
                                      Net::scanSsid(i).c_str(), Net::scanRssi(i),
                                      Net::scanEnc(i) ? "加密" : "开放");
                    }
                    Net::clearScan();
                    break;
                }
                case 'T': {          // 授时：T<unix秒>
                    uint32_t u = (uint32_t)strtoul(line + 1, nullptr, 10);
                    if (u > 1000000000u) Clock::setUnix(u);
                    else Serial.println("[clock] 用法: T<unix秒>，例如 T1760000000");
                    break;
                }
                case 'r': Serial.println("[cmd] rebooting"); delay(50); ESP.restart(); break;
                case 'p': Serial.printf("[scr] id=%d saver=%d\n", App::screenId(), (int)App::inScreensaver()); break;
                case 'k': App::debugKey((uint8_t)atoi(line + 1)); break;
                case 'z': {          // 随机按键压力测试
                    int n2 = atoi(line + 1);
                    if (n2 <= 0 || n2 > 20000) n2 = 2000;
                    uint32_t s0 = millis();
                    for (int i = 0; i < n2; i++) {
                        uint8_t m = (uint8_t)(1u << (esp_random() % 6));
                        App::debugKey(m);
                        if (App::consumeDirty()) { App::render(); }
                    }
                    Serial.printf("[soak] %d 次按键 %lu ms 剩余堆 %u 当前屏 %d\n",
                                  n2, (unsigned long)(millis() - s0),
                                  (unsigned)esp_get_free_heap_size(), App::screenId());
                    break;
                }
                default:
                    Serial.println("[cmd] t=自检 e=屏保自检 i=信息 s=保存 w=WiFi W=扫热点 S<ssid>,<pass>=设WiFi n=对时 T<unix>=授时 p=屏号 k<掩码>=按键 z<n>=压测 r=重启");
            }
            return;
        }
        if (n < sizeof(line) - 1) line[n++] = c;
    }
}

void setup() {
    Serial.begin(115200);
    delay(60);

    Store::begin();
    ws_init();                 // 载入 LittleFS 里的自定义词
    Clock::begin();
    Power::begin();
    Input::init();
    Sound::init();
    gfx.init();

    Serial.printf("[XiaoMiao Vocab] words=%d(+%d user) fs=%d prog=%u bytes\n",
                  ws_count(), ws_userCount(), (int)Store::fsReady(),
                  (unsigned)(sizeof(WordState) * MAX_WORDS));

    bootScreen();
    App::init();
    Net::begin();              // 后台异步连 WiFi，不阻塞开机
    delay(900);
}

void loop() {
    uint32_t now = millis();
    uint8_t k = Input::poll();
    if (k) App::noteActivity();

    Clock::tick();
    Net::tick();
    Power::tick();

    App::update(k);
    App::tick(now);
    Store::saveIfDirty();

    if (App::consumeDirty()) {
        App::render();
        gfx.flush();
    }
    serialCmd();
    delay(8);
}
