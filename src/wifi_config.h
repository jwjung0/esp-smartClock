#pragma once

#include <Arduino.h>

typedef void (*WifiConfigIdleCb)(void);

void wifiConfigSetIdleCallback(WifiConfigIdleCb cb);
void wifiConfigBegin();
// No-op: DNS/HTTP run on the portal FreeRTOS task after wifiConfigBegin().
void wifiConfigLoop();
// Cached on the portal task. The clock loop must not call into the Wi‑Fi stack.
bool wifiConfigIsConnected();
bool wifiConfigIsRadioOn();
bool wifiConfigSoftApActive();
IPAddress wifiConfigLocalIp();
IPAddress wifiConfigSoftApIp();

// Queue only — return immediately. Portal task performs the radio change.
void wifiConfigRadioOff();
// False when no saved SSID (no Wi‑Fi call). Otherwise queues STA join.
bool wifiConfigRequestWake();
// True when no radio command is queued or running.
bool wifiConfigCmdIdle();

// RGB 0xRRGGBB (SquareLine defaults if unset)
uint32_t wifiConfigDateColor();
uint32_t wifiConfigClockColor();
// True once after colors saved via HTTP (apply on LVGL thread/loop).
bool wifiConfigTakeColorsChanged();
