#include "net.h"
#include "store.h"
#include "clock.h"
#include "words.h"
#include "power.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#include <stdio.h>

namespace Net {

static Status st = OFF;
static uint32_t beginMs = 0;
static bool scanActive = false;
static String errMsg;
static WebServer* srv = nullptr;
static bool sntpStarted = false;

// ---------------------------------------------------------------- 状态
Status status() { return st; }
bool connected() { return WiFi.status() == WL_CONNECTED; }
String ipString() { return WiFi.localIP().toString(); }
String mdnsUrl() { return String("http://xiaomiao.local"); }
int rssi() { return WiFi.RSSI(); }
String lastError() { return errMsg; }

// ---------------------------------------------------------------- 连接
void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    if (Store::cfg.wifiOn && Store::cfg.ssid[0]) {
        st = CONNECTING;
        beginMs = millis();
        WiFi.begin(Store::cfg.ssid, Store::cfg.pass);
        Serial.printf("[net] connect to %s\n", Store::cfg.ssid);
    } else {
        st = Store::cfg.ssid[0] ? OFF : NOSAVED;
    }
}

void startServer() {
    if (srv) return;
    srv = new WebServer(80);

    srv->on("/", HTTP_GET, []() {
        char t[24], d[24];
        uint32_t u = Clock::now();
        Clock::fmtTimeS(u, t, sizeof(t));
        Clock::fmtDate(u, d, sizeof(d));
        int pct = Power::percent();
        String h = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                     "<title>小喵背单词</title><style>"
                     "body{font-family:system-ui;margin:16px;background:#f6f7fb;color:#222}"
                     "h1{font-size:18px}.c{background:#fff;border-radius:10px;padding:12px;margin:10px 0;box-shadow:0 1px 4px #0001}"
                     "input,button,textarea{font-size:14px;padding:6px;margin:4px 0}"
                     "button{background:#2f7cf6;color:#fff;border:0;border-radius:6px;padding:8px 14px;cursor:pointer}"
                     ".s{color:#666;font-size:13px}table{font-size:13px}"
                     "</style><h1>小喵掌机 · 背单词</h1>");
        h += "<div class=c><b>状态</b><br><span class=s>";
        h += "时间：" + String(t) + " " + String(d) + (Clock::synced() ? "（已同步）" : "（未同步）") + "<br>";
        h += "电量：" + String(pct >= 0 ? String(pct) + "%" : "隐藏") + "<br>";
        h += "WiFi：" + String(Store::cfg.ssid) + "  信号 " + String(WiFi.RSSI()) + "dBm<br>";
        h += "词库：" + String(ws_count()) + " 词（内置 " + String(BASE_WORD_COUNT) +
             " + 自定义 " + String(ws_userCount()) + "）<br>";
        h += "学习：已答 " + String(Store::cfg.totalAnswers) + " 题，正确 " +
             String(Store::cfg.totalAnswers ? Store::cfg.totalCorrect * 100 / Store::cfg.totalAnswers : 0) + "%";
        h += "</span></div>";

        h += F("<div class=c><b>1. 同步时间</b><br>"
               "<button onclick=\"fetch('/sync/time',{method:'POST'}).then(r=>r.text()).then(t=>alert(t))\">用 NTP 校准</button> "
               "<span class=s>或手动：</span><br>"
               "<input id=y size=4 placeholder=2026><input id=mo size=2 placeholder=10><input id=dd size=2 placeholder=17> "
               "<input id=hh size=2 placeholder=8><input id=mi size=2 placeholder=0><input id=ss size=2 placeholder=0> "
               "<button onclick=\"fetch('/set/time?y='+y.value+'&m='+mo.value+'&d='+dd.value+'&h='+hh.value+'&mi='+mi.value+'&s='+ss.value,{method:'POST'}).then(r=>r.text()).then(t=>alert(t))\">设置</button></div>");

        h += F("<div class=c><b>2. 上传词库（CSV）</b><br><span class=s>每行：英文,音标,中文　（音标可留空：英文,中文）</span><br>"
               "<textarea id=csv rows=6 cols=44 placeholder='apple,/ˈæpl/,苹果&#10;banana,/bəˈnɑːnə/,香蕉'></textarea><br>"
               "<button onclick=\"fetch('/words/upload',{method:'POST',body:csv.value}).then(r=>r.text()).then(t=>alert(t))\">追加</button> "
               "<button onclick=\"fetch('/words/upload?replace=1',{method:'POST',body:csv.value}).then(r=>r.text()).then(t=>alert(t))\">替换全部</button> "
               "<a href=/words.csv><button>导出自定义词</button></a></div>");

        h += F("<div class=c><b>3. 从网址拉取词库</b><br>"
               "<input id=u size=40 placeholder='https://example.com/words.csv'>"
               "<button onclick=\"fetch('/words/fetch?url='+encodeURIComponent(u.value),{method:'POST'}).then(r=>r.text()).then(t=>alert(t))\">拉取</button></div>");

        h += F("<div class=c><b>4. 学习进度</b><br>"
               "<a href=/progress.json><button>下载备份</button></a> "
               "<input type=file id=pf accept=.json> "
               "<button onclick=\"pf.files[0].text().then(x=>fetch('/progress/restore',{method:'POST',body:x}).then(r=>r.text()).then(t=>alert(t)))\">恢复</button></div>");

        h += F("<div class=c><b>5. WiFi</b><br><span class=s>改 WiFi 会短暂断网，请用新地址重新打开本页</span><br>"
               "<input id=ns placeholder='新 WiFi 名'><input id=np placeholder='密码'>"
               "<button onclick=\"fetch('/wifi?ssid='+encodeURIComponent(ns.value)+'&pass='+encodeURIComponent(np.value),{method:'POST'}).then(r=>r.text()).then(t=>alert(t))\">保存并连接</button></div>");

        h += "</body>";
        srv->send(200, "text/html; charset=utf-8", h);
    });

    srv->on("/status.json", HTTP_GET, []() {
        char t[24], d[24];
        Clock::fmtTimeS(Clock::now(), t, sizeof(t));
        Clock::fmtDate(Clock::now(), d, sizeof(d));
        char js[512];
        snprintf(js, sizeof(js),
                 "{\"time\":\"%s\",\"date\":\"%s\",\"synced\":%d,\"battery\":%d,\"words\":%d,"
                 "\"user\":%d,\"answers\":%lu,\"correct\":%lu,\"due\":%d,\"new\":%d,\"wrong\":%d,"
                 "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"uptime\":%lu}",
                 t, d, Clock::synced() ? 1 : 0, Power::percent(), ws_count(), ws_userCount(),
                 (unsigned long)Store::cfg.totalAnswers, (unsigned long)Store::cfg.totalCorrect,
                 Store::countDue(), Store::countNew(), Store::countWrong(),
                 Store::cfg.ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                 (unsigned long)(millis() / 1000));
        srv->send(200, "application/json; charset=utf-8", js);
    });

    srv->on("/words.csv", HTTP_GET, []() {
        static char* buf = nullptr;
        if (!buf) buf = (char*)malloc(24 * 1024);
        if (!buf) { srv->send(500, "text/plain", "oom"); return; }
        ws_dumpCsv(buf, 24 * 1024);
        srv->send(200, "text/csv; charset=utf-8", buf);
    });

    srv->on("/words/upload", HTTP_POST, []() {
        String body = srv->arg("plain");
        bool replace = srv->hasArg("replace");
        int n = ws_importCsv(body.c_str(), replace);
        ws_saveUser();
        Store::save();
        srv->send(200, "text/plain; charset=utf-8",
                  String("已导入 ") + String(n) + " 条，当前自定义词 " + String(ws_userCount()));
    });

    srv->on("/words/fetch", HTTP_POST, []() {
        if (!srv->hasArg("url")) { srv->send(400, "text/plain", "missing url"); return; }
        int added = 0;
        bool ok = fetchWords(srv->arg("url").c_str(), &added);
        srv->send(200, "text/plain; charset=utf-8",
                  ok ? (String("已拉取 ") + String(added) + " 条") : String("失败：") + lastError());
    });

    srv->on("/progress.json", HTTP_GET, []() {
        String out = "{\"v\":2,\"words\":[";
        int n = 0;
        for (int i = 0; i < ws_count(); i++) {
            if (Store::st[i].status == 0 && Store::st[i].flags == 0) continue;
            if (n) out += ",";
            char e[64];
            snprintf(e, sizeof(e), "[\"%s\",%d,%u,%u,%u]", ws_en(i), Store::st[i].status,
                     Store::st[i].interval, Store::st[i].due, Store::st[i].flags);
            out += e;
            n++;
            if (out.length() > 60000) break;
        }
        out += "]}";
        srv->send(200, "application/json; charset=utf-8", out);
    });

    srv->on("/progress/restore", HTTP_POST, []() {
        String body = srv->arg("plain");
        int restored = 0;
        // 极简解析：["word",status,interval,due,flags]
        int p = 0;
        while (true) {
            int a = body.indexOf('"', p);
            if (a < 0) break;
            int b = body.indexOf('"', a + 1);
            if (b < 0) break;
            String w = body.substring(a + 1, b);
            int s = body.indexOf('[', b);
            int e = body.indexOf(']', b);
            if (s < 0 || e < 0) break;
            String nums = body.substring(b + 2, e);
            int c1 = nums.indexOf(',');
            int c2 = nums.indexOf(',', c1 + 1);
            int c3 = nums.indexOf(',', c2 + 1);
            if (c1 < 0 || c2 < 0 || c3 < 0) { p = e + 1; continue; }
            int status = nums.substring(0, c1).toInt();
            int interval = nums.substring(c1 + 1, c2).toInt();
            int due = nums.substring(c2 + 1, c3).toInt();
            int flags = nums.substring(c3 + 1).toInt();
            // 找到对应词（先查内置，再查自定义）
            int idx = -1;
            for (int i = 0; i < ws_count(); i++) {
                if (strcmp(ws_en(i), w.c_str()) == 0) { idx = i; break; }
            }
            if (idx < 0) idx = ws_addUser(w.c_str(), "", "（恢复）");
            if (idx >= 0) {
                Store::st[idx].status = (uint8_t)status;
                Store::st[idx].interval = (uint16_t)interval;
                Store::st[idx].due = (uint16_t)due;
                Store::st[idx].flags = (uint8_t)flags;
                Store::st[idx].ease10 = 25;
                Store::markWordDirty(idx);   // 增量保存：只写这个词所在的块
                restored++;
            }
            p = e + 1;
        }
        Store::save();
        srv->send(200, "text/plain; charset=utf-8", String("已恢复 ") + String(restored) + " 个单词的进度");
    });

    srv->on("/sync/time", HTTP_POST, []() {
        bool ok = syncTime();
        srv->send(200, "text/plain; charset=utf-8", ok ? "NTP 已触发，稍后刷新查看" : "未连接网络");
    });

    srv->on("/set/time", HTTP_POST, []() {
        int y = srv->arg("y").toInt(), mo = srv->arg("m").toInt(), d = srv->arg("d").toInt();
        int h = srv->arg("h").toInt(), mi = srv->arg("mi").toInt(), s = srv->arg("s").toInt();
        Clock::setManual(y, mo, d, h, mi, s);
        srv->send(200, "text/plain; charset=utf-8", "已设置（刷新查看）");
    });

    srv->on("/wifi", HTTP_POST, []() {
        String ns = srv->arg("ssid"), np = srv->arg("pass");
        connectTo(ns.c_str(), np.c_str());
        srv->send(200, "text/plain; charset=utf-8", String("已保存 " ) + ns + "，正在连接");
    });

    srv->onNotFound([]() { srv->send(404, "text/plain", "not found"); });

    srv->begin();
    if (MDNS.begin("xiaomiao")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[net] mDNS: http://xiaomiao.local");
    }
}

void onGotIp() {
    Serial.printf("[net] connected ip=%s\n", WiFi.localIP().toString().c_str());
    st = CONNECTED;
    startServer();
    if (Store::cfg.autoSync) syncTime();
}

void tick() {
    static uint32_t lastMs = 0;
    if ((uint32_t)(millis() - lastMs) < 200) return;
    lastMs = millis();

    if (scanActive && WiFi.scanComplete() >= 0) scanActive = false;

    if (st == CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) onGotIp();
        else if ((uint32_t)(millis() - beginMs) > 15000) {
            st = FAILED;
            errMsg = "连接失败/超时";
            Serial.println("[net] connect timeout");
        }
    } else if (st == CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) st = FAILED;
    }
    if (st == CONNECTED && srv) srv->handleClient();
}

bool connectTo(const char* ssid, const char* pass) {
    if (!ssid || !ssid[0]) return false;
    snprintf(Store::cfg.ssid, sizeof(Store::cfg.ssid), "%s", ssid);
    snprintf(Store::cfg.pass, sizeof(Store::cfg.pass), "%s", pass ? pass : "");
    Store::cfg.wifiOn = 1;
    Store::save();
    st = CONNECTING;
    beginMs = millis();
    WiFi.disconnect(false, true);
    delay(50);
    WiFi.begin(ssid, pass ? pass : "");
    return true;
}

void disconnect() {
    WiFi.disconnect(false, false);
    st = OFF;
}

void forget() {
    WiFi.disconnect(false, true);
    Store::cfg.ssid[0] = 0;
    Store::cfg.pass[0] = 0;
    Store::save();
    st = NOSAVED;
}

// ---------------------------------------------------------------- 扫描
bool startScan() {
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(true);   // async
    scanActive = (n >= 0) || (n == WIFI_SCAN_RUNNING);
    beginMs = millis();
    return scanActive;
}

bool scanDone() {
    int n = WiFi.scanComplete();
    return n >= 0;
}

int scanCount() {
    int n = WiFi.scanComplete();
    return n < 0 ? 0 : n;
}

String scanSsid(int i) { return WiFi.SSID(i); }
int scanRssi(int i) { return WiFi.RSSI(i); }
int scanEnc(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1; }
void clearScan() { WiFi.scanDelete(); scanActive = false; }

// ---------------------------------------------------------------- 时间
bool syncTime() {
    if (!connected()) return false;
    if (!sntpStarted) {
        sntpStarted = true;
        configTzTime("CST-8", "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");
    }
    return true;
}

bool timeSynced() { return Clock::synced(); }

// ---------------------------------------------------------------- 远端词库
bool fetchWords(const char* url, int* added) {
    if (!connected()) { errMsg = "未连接 WiFi"; return false; }
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(url)) { errMsg = "URL 无效"; return false; }
    int code = http.GET();
    if (code != 200) {
        errMsg = String("HTTP ") + String(code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();
    int n = ws_importCsv(body.c_str(), false);
    ws_saveUser();
    if (added) *added = n;
    return true;
}

}  // namespace Net
