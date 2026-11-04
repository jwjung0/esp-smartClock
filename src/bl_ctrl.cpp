#include "bl_ctrl.h"

#include "board_pins.h"
#include "light_sensor.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

static const int kBlPwmFreqHz = 5000;
static const int kBlPwmBits = 8;
static const int kBlPwmMax = (1 << kBlPwmBits) - 1; // 255

// Lux→BL from measured VEML7700: dark ~20–50, room ~180–1100, sun ~6000.
// Log map; snap EMA on big jumps so cover/uncover isn't stuck mid-duty.
static const int kBlDutyMin = 38;        // ~15% floor (night)
static const float kLuxDark = 20.0f;     // at/below → min duty
static const float kLuxBright = 3500.0f; // at/above → full
static const float kLuxEmaAlpha = 0.55f;
static const float kLuxSnapRatio = 2.5f; // |lux/ema| beyond this → snap

static const char *kPrefsUiNs = "clockui";
static const char *kKeyBlAuto = "bl_auto";

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
static float luxEma = NAN;
static float luxRaw = NAN;
static int dutyCurrent = kBlPwmMax;

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
  if (lux <= kLuxDark)
    return kBlDutyMin;
  if (lux >= kLuxBright)
    return kBlPwmMax;
  const float t =
      (log10f(lux) - log10f(kLuxDark)) / (log10f(kLuxBright) - log10f(kLuxDark));
  const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return kBlDutyMin + (int)lroundf(tc * (float)(kBlPwmMax - kBlDutyMin));
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
    writeDuty(kBlPwmMax);
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
    prefs.end();
  } else {
    autoEnabled = true;
  }
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
