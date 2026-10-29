#pragma once

#include <Arduino.h>

typedef void (*WifiConfigIdleCb)(void);

void wifiConfigSetIdleCallback(WifiConfigIdleCb cb);
void wifiConfigBegin();
void wifiConfigLoop();
bool wifiConfigIsConnected();
bool wifiConfigIsRadioOn();
bool wifiConfigSoftApActive();
IPAddress wifiConfigLocalIp();
IPAddress wifiConfigSoftApIp();

// Power policy: radio off when idle; brief STA wake for NTP (no portal).
void wifiConfigRadioOff();
bool wifiConfigWakeStaForSync();

// RGB 0xRRGGBB (SquareLine defaults if unset)
uint32_t wifiConfigDateColor();
uint32_t wifiConfigClockColor();
// True once after colors saved via HTTP (apply on LVGL thread/loop).
bool wifiConfigTakeColorsChanged();
