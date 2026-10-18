#include <Arduino.h>
#include <stdio.h>
#include <Wire.h>
#include "hal.h"
#include "store.h"
#include "app.h"
#include "config.h"
#include "words.h"
#include "clock.h"
#include "power.h"
#include "net.h"
#include "lights.h"

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
            if (!line[0]) continue;      // 空行（桥接器发的前导 \n）直接跳过，别把剩余字节留到下一轮
            switch (line[0]) {
                case 't': App::selfTest(); break;
                case 'e': App::selfTestSaver(); break;
                case 'i':
                    Serial.printf("[info] words=%d user=%d fs=%d wifi=%d ip=%s time=%lu synced=%d batt=%d due=%d new=%d wrong=%d light=%d\n",
                                  ws_count(), ws_userCount(), (int)Store::fsReady(),
                                  (int)Net::status(), Net::ipString().c_str(),
                                  (unsigned long)Clock::now(), (int)Clock::synced(),
                                  Power::percent(), Store::countDue(), Store::countNew(),
                                  Store::countWrong(), (int)Lights::available());
                    Serial.printf("[cfg] dailyNew=%d sound=%d orient=%d bgr=%d saverMode=%d saverSec=%d battMode=%d books=%d/%d enabled=%d\n",
                                  Store::cfg.dailyNew, Store::cfg.soundOn, Store::cfg.orient,
                                  Store::cfg.bgr, Store::cfg.saverMode, Store::cfg.saverSec,
                                  Store::cfg.battMode,
                                  __builtin_popcount(Store::cfg.bookMask & 0x1FFFFF),
                                  Store::bookCount(), Store::enabledWordCount());
                    break;
                case 's': Store::save(); Serial.println("[cmd] saved"); break;
                case 'P':        // 清空学习进度（保留设置）
                    Store::clearProgress();
                    Serial.println("[cmd] 学习进度已清空");
                    break;
                case 'R':        // 恢复默认设置（保留 WiFi 凭据），自动化测试用来固定起点
                    Store::resetConfig();
                    Serial.println("[cmd] 已恢复默认设置（保留 WiFi）");
                    break;
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
                case 'g': App::gameDump(); break;
                case 'k': App::debugKey((uint8_t)atoi(line + 1)); break;
                case 'm':                   // 蜂鸣器运行时开关（测试静音用，不写入设置）
                                            // m=切换  m0=强制关  m1=强制开
                    if (line[1] == '0') Sound::setEnabled(false);
                    else if (line[1] == '1') Sound::setEnabled(true);
                    else Sound::setEnabled(!Sound::enabled());
                    Serial.printf("[sound] %s\n", Sound::enabled() ? "开" : "关");
                    break;
                case 'I': {                 // I2C 扫描（验证灯控总线）
                    int found = 0;
                    for (uint8_t a = 1; a < 0x7F; a++) {
                        Wire.beginTransmission(a);
                        if (Wire.endTransmission() == 0) {
                            Serial.printf("  [i2c] 0x%02X 应答%s\n", a, a == 0x40 ? "  <-- 灯控" : "");
                            found++;
                        }
                    }
                    Serial.printf("[i2c] 扫描完成，%d 个设备；灯控 0x40 %s\n",
                                  found, Lights::available() ? "在线" : "不在线");
                    break;
                }
                case 'l':                   // 灯光反馈测试（click/ok/err 各来一遍）
                    Serial.printf("[light] 0x40 %s\n", Lights::available() ? "在线" : "不在线（无反馈可看）");
                    Lights::click(); delay(500);
                    Lights::ok();    delay(900);
                    Lights::err();
                    Serial.println("[light] 已播放 1短闪/2闪/3急闪");
                    break;
                case 'L': {                 // 换引脚重连 I2C：L<scl>,<sda>
                    char* comma = strchr(line + 1, ',');
                    if (comma) {
                        *comma = 0;
                        int scl = atoi(line + 1), sda = atoi(comma + 1);
                        bool okc = Lights::beginBus((uint8_t)scl, (uint8_t)sda);
                        Serial.printf("[light] I2C 改到 SCL=%d SDA=%d，0x40 %s\n",
                                      scl, sda, okc ? "在线" : "不在线");
                    } else {
                        Serial.println("[light] 用法: L<scl>,<sda>，例如 L15,21");
                    }
                    break;
                }
                case 'M': {                 // 运行时屏保延时覆盖（测试用，不写入设置）
                    int sec = atoi(line + 1);
                    App::setSaverSecRuntime(sec);
                    if (sec < 0) Serial.println("[saver] 延时恢复为设置值");
                    else Serial.printf("[saver] 运行时延时=%d 秒\n", sec);
                    break;
                }
                case 'z': {          // 随机按键压力测试
                    int n2 = atoi(line + 1);
                    if (n2 <= 0 || n2 > 20000) n2 = 2000;
                    uint32_t s0 = millis();
                    for (int i = 0; i < n2; i++) {
                        int scrBefore = App::screenId();
                        uint32_t k0 = millis();
                        uint8_t m = (uint8_t)(1u << (esp_random() % 6));
                        App::debugKey(m);
                        if (App::consumeDirty()) { App::render(); }
                        uint32_t dt = (uint32_t)(millis() - k0);
                        if (dt > 200)      // 慢键探针：定位哪一步会卡住 UI
                            Serial.printf("[soak] 慢键 i=%d scr=%d->%d 耗时 %lu ms\n",
                                          i, scrBefore, App::screenId(), (unsigned long)dt);
                    }
                    Serial.printf("[soak] %d 次按键 %lu ms 剩余堆 %u 当前屏 %d\n",
                                  n2, (unsigned long)(millis() - s0),
                                  (unsigned)esp_get_free_heap_size(), App::screenId());
                    break;
                }
                default:
                    Serial.println("[cmd] t=自检 e=屏保自检 i=信息 s=保存 R=恢复默认设置 P=清空进度 w=WiFi W=扫热点 S<ssid>,<pass>=设WiFi n=对时 T<unix>=授时 p=屏号 k<掩码>=按键 m=蜂鸣器开关(m0关/m1开) M<秒>=屏保延时(临时) z<n>=压测 r=重启 I=I2C扫描 l=灯光测试 L<scl>,<sda>=换I2C脚");
            }
            return;
        }
        // 只收可打印 ASCII：串口在开端口/受干扰时会产生 0x00 等毛刺字节，
        // 拼进命令行会让整条命令被当成空行静默丢弃（表现为「命令发出去毫无反应」）。
        if (c < 0x20 || c > 0x7E) {
            Serial.printf("[rx] 丢弃非打印字节 0x%02X\n", (unsigned)(uint8_t)c);
            continue;
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
    Lights::init();           // I2C 灯控（静音反馈）；不在线也不影响启动
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
    uint8_t k = Input::poll();
    if (k && App::noteActivity()) k = 0;   // 退出屏保的按键被消费，不穿透到界面
    // now 必须在 noteActivity() 之后取：保证 nowMs >= lastAct，
    // 避免 tick() 里 nowMs - lastAct 无符号下溢误触发屏保
    uint32_t now = millis();

    Clock::tick();
    Net::tick();
    Power::tick();
    Lights::tick();           // 推进非阻塞灯光闪烁

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
