#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <WString.h>

namespace Net {

enum Status { OFF = 0, CONNECTING, CONNECTED, FAILED, NOSAVED };

void begin();
void tick();

Status status();
bool   connected();
String ipString();
String mdnsUrl();
int    rssi();

bool startScan();                 // 异步扫描
bool scanDone();
int  scanCount();
String scanSsid(int i);
int    scanRssi(int i);
int    scanEnc(int i);            // 0 开放 / 1 加密
void   clearScan();

bool connectTo(const char* ssid, const char* pass);   // 保存并异步连接
void disconnect();
void forget();

bool syncTime();                  // 触发 SNTP（需已连网）
bool timeSynced();
bool fetchWords(const char* url, int* added);   // 从远端拉 CSV 词库

String lastError();
}
