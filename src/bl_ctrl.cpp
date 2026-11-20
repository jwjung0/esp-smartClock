#include "bl_ctrl.h"

#include "board_pins.h"
#include "light_sensor.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

static const int kBlPwmFreqHz = 5000;
static const int kBlPwmBits = 8;
static const int kBlPwmMax = (1 << kBlPwmBits) - 1; // 255

// Lux→BL for indoor rooms (duty from portal night/day %). Snap EMA on big jumps.
// luxBright must be reachable indoors so Day 100% can hit full PWM.
static const uint8_t kBlNightPctDefault = 20; // floor in dark
static const uint8_t kBlDayPctDefault = 80;   // peak in bright room
static const float kLuxDark = 20.0f;          // at/below → night duty
static const float kLuxBright = 450.0f;       // at/above → day duty (indoor)
static const float kLuxEmaAlpha = 0.55f;
static const float kLuxSnapRatio = 2.5f; // |lux/ema| beyond this → snap

static const char *kPrefsUiNs = "clockui";
static const char *kKeyBlAuto = "bl_auto";
static const char *kKeyBlNight = "bl_night";
static const char *kKeyBlDay = "bl_day";

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif
#if ESP_ARDUINO_VERSION_MAJOR < 3
static const int kBlLedcChannel = 0;
#endif

static bool pwmReady = false;
static bool wantOn = false;
static bool autoEnabled = true;
static bool prefsLoaded = false;
static uint8_t nightPct = kBlNightPctDefault;
static uint8_t dayPct = kBlDayPctDefault;
static float luxEma = NAN;
static float luxRaw = NAN;
static int dutyCurrent = kBlPwmMax;

static int pctToDuty(uint8_t pct) {
  if (pct < 1)
    pct = 1;
  if (pct > 100)
    pct = 100;
  return (int)lroundf((float)pct * (float)kBlPwmMax / 100.0f);
}

static void clampNightDay(uint8_t *nightInOut, uint8_t *dayInOut) {
  uint8_t n = nightInOut ? *nightInOut : kBlNightPctDefault;
  uint8_t d = dayInOut ? *dayInOut : kBlDayPctDefault;
  if (n < 1)
    n = 1;
  if (n > 100)
    n = 100;
  if (d < 1)
    d = 1;
  if (d > 100)
    d = 100;
  if (n > d)
    n = d;
  if (nightInOut)
    *nightInOut = n;
  if (dayInOut)
    *dayInOut = d;
}

static void writeDuty(int duty) {
  if (duty < 0)
    duty = 0;
  if (duty > kBlPwmMax)
    duty = kBlPwmMax;
  dutyCurrent = duty;
  if (!pwmReady)
    return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BL, (uint32_t)duty);
#else
  ledcWrite(kBlLedcChannel, (uint32_t)duty);
#endif
}

static int luxToDuty(float lux) {
  const int dutyMin = pctToDuty(nightPct);
  const int dutyCap = pctToDuty(dayPct);
  if (lux <= kLuxDark)
    return dutyMin;
  if (lux >= kLuxBright)
    return dutyCap;
  const float t =
      (log10f(lux) - log10f(kLuxDark)) / (log10f(kLuxBright) - log10f(kLuxDark));
  const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return dutyMin + (int)lroundf(tc * (float)(dutyCap - dutyMin));
}

static void applyBrightness(void) {
  if (!wantOn) {
    writeDuty(0);
    return;
  }
  if (!autoEnabled) {
    writeDuty(kBlPwmMax);
    return;
  }

  float lux = 0;
  if (lightSensorOk() && lightSensorReadLux(&lux)) {
    luxRaw = lux;
    if (isnan(luxEma)) {
      luxEma = lux;
    } else {
      const float hi = lux > luxEma ? lux : luxEma;
      const float lo = lux < luxEma ? lux : luxEma;
      // Cover/uncover: don't crawl toward the new level over many samples.
      if (lo > 1.0f && hi / lo >= kLuxSnapRatio)
        luxEma = lux;
      else
        luxEma = kLuxEmaAlpha * lux + (1.0f - kLuxEmaAlpha) * luxEma;
    }
    writeDuty(luxToDuty(luxEma));
  } else {
    luxRaw = NAN;
    writeDuty(pctToDuty(dayPct)); // auto-on, no sensor → day peak, not full
  }
}

void blCtrlBegin(void) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  if (pwmReady)
    return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_TFT_BL, kBlPwmFreqHz, kBlPwmBits);
#else
  ledcSetup(kBlLedcChannel, kBlPwmFreqHz, kBlPwmBits);
  ledcAttachPin(PIN_TFT_BL, kBlLedcChannel);
#endif
  pwmReady = true;
  writeDuty(0);
}

void blCtrlLoadPrefs(void) {
  Preferences prefs;
  if (prefs.begin(kPrefsUiNs, true)) {
    autoEnabled = prefs.getBool(kKeyBlAuto, true);
    nightPct = (uint8_t)prefs.getUChar(kKeyBlNight, kBlNightPctDefault);
    dayPct = (uint8_t)prefs.getUChar(kKeyBlDay, kBlDayPctDefault);
    prefs.end();
  } else {
    autoEnabled = true;
    nightPct = kBlNightPctDefault;
    dayPct = kBlDayPctDefault;
  }
  clampNightDay(&nightPct, &dayPct);
  prefsLoaded = true;
}

void blCtrlUpdateFromLux(void) {
  if (!wantOn || !autoEnabled)
    return;
  applyBrightness();
}

void blCtrlSetOn(bool on) {
  blCtrlBegin();
  wantOn = on;
  applyBrightness();
}

int blCtrlDuty(void) { return dutyCurrent; }

float blCtrlLuxRaw(void) { return luxRaw; }

float blCtrlLuxEma(void) { return luxEma; }

bool blCtrlAutoEnabled(void) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  return autoEnabled;
}

void blCtrlSetAutoEnabled(bool enabled) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  autoEnabled = enabled;
  Preferences prefs;
  if (prefs.begin(kPrefsUiNs, false)) {
    prefs.putBool(kKeyBlAuto, autoEnabled);
    prefs.end();
  }
  if (wantOn)
    applyBrightness();
  Serial.printf("BL auto brightness %s\n", autoEnabled ? "ON" : "OFF");
}

uint8_t blCtrlNightPct(void) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  return nightPct;
}

uint8_t blCtrlDayPct(void) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  return dayPct;
}

bool blCtrlSetNightDayPct(uint8_t nightIn, uint8_t dayIn) {
  if (!prefsLoaded)
    blCtrlLoadPrefs();
  clampNightDay(&nightIn, &dayIn);
  nightPct = nightIn;
  dayPct = dayIn;
  Preferences prefs;
  if (prefs.begin(kPrefsUiNs, false)) {
    prefs.putUChar(kKeyBlNight, nightPct);
    prefs.putUChar(kKeyBlDay, dayPct);
    prefs.end();
  }
  if (wantOn)
    applyBrightness();
  Serial.printf("BL night=%u%% day=%u%%\n", (unsigned)nightPct,
                (unsigned)dayPct);
  return true;
}

bool blCtrlWantOn(void) { return wantOn; }

void blCtrlReapplyAfterSleep(void) {
  // Light sleep gates the LEDC peripheral — re-attach and restore duty.
  pwmReady = false;
  blCtrlBegin();
  if (!wantOn) {
    writeDuty(0);
    return;
  }
  writeDuty(dutyCurrent > 0 ? dutyCurrent
                            : (autoEnabled ? pctToDuty(dayPct) : kBlPwmMax));
 }
