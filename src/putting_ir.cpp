#include "putting_ir.h"

#if defined(FIRMWARE_MODE_APP)

#include "board_pins.h"

#include <Preferences.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

static const float kGateSpacingM = 0.025f;
static const float kG = 9.80665f;
static const float kStimpExitMps = 1.83f;
static const float kDefaultMatMu = 0.10f;
static const float kDefaultGreenSlowM = 2.44f;
static const float kDefaultGreenNormalM = 3.05f;
static const float kDefaultGreenFastM = 3.66f;
static const int64_t kShotTimeoutUs = 200000; // 200 ms
static const int64_t kDebounceUs = 300;

static const char *kPrefsNs = "putting";
static const char *kKeyEnabled = "en";
static const char *kKeyMu = "mu";
static const char *kKeyGs = "gs";
static const char *kKeyGn = "gn";
static const char *kKeyGf = "gf";
static const char *kKeyCalN = "calN";
static const char *kKeyCalV = "calV"; // float[8]
static const char *kKeyCalD = "calD"; // float[8]

static const int kIrPins[PUTTING_IR_GATES] = {
    PIN_PUT_IR0, PIN_PUT_IR1, PIN_PUT_IR2, PIN_PUT_IR3, PIN_PUT_IR4};

static Preferences prefs;
static bool enabled = false;
static bool modeChanged = false;
static float matMu = kDefaultMatMu;
static float greenSlowM = kDefaultGreenSlowM;
static float greenNormalM = kDefaultGreenNormalM;
static float greenFastM = kDefaultGreenFastM;
static float calV[PUTTING_CAL_MAX];
static float calD[PUTTING_CAL_MAX];
static uint8_t calN = 0;

static volatile bool capturing = false;
static volatile bool busyChanged = false;
static volatile bool resultReady = false;
static volatile int64_t gateTs[PUTTING_IR_GATES];
static volatile uint8_t gateMask = 0;
static volatile int64_t captureStartUs = 0;
static volatile int64_t lastEdgeUs[PUTTING_IR_GATES];

static PuttingShot lastShot = {};
static float lastSpeed = 0.f;
static bool hasLastShot = false;

static float muFromGreenLength(float length_m) {
  if (length_m < 0.01f)
    return kDefaultMatMu;
  return (kStimpExitMps * kStimpExitMps) / (2.f * kG * length_m);
}

static float distFromV(float v, float mu) {
  if (!(v > 0.f) || !(mu > 1e-6f))
    return 0.f;
  return (v * v) / (2.f * mu * kG);
}

static void recomputeShotFromSpeed(float v, PuttingShot *out) {
  out->speed_mps = v;
  out->mat_m = distFromV(v, matMu);
  out->slow_m = distFromV(v, muFromGreenLength(greenSlowM));
  out->normal_m = distFromV(v, muFromGreenLength(greenNormalM));
  out->fast_m = distFromV(v, muFromGreenLength(greenFastM));
  out->valid = (v > 0.f);
}

static void refitMatMu(void) {
  if (calN == 0) {
    matMu = kDefaultMatMu;
    return;
  }
  double sumDv2 = 0.0;
  double sumV4 = 0.0;
  for (uint8_t i = 0; i < calN; ++i) {
    const double v2 = (double)calV[i] * (double)calV[i];
    sumDv2 += (double)calD[i] * v2;
    sumV4 += v2 * v2;
  }
  if (sumV4 < 1e-12) {
    matMu = kDefaultMatMu;
    return;
  }
  const double k = sumDv2 / sumV4;
  if (k < 1e-6) {
    matMu = kDefaultMatMu;
    return;
  }
  matMu = (float)(1.0 / (2.0 * (double)kG * k));
  if (!(matMu > 0.001f) || matMu > 2.f)
    matMu = kDefaultMatMu;
}

static void savePrefs(void) {
  if (!prefs.begin(kPrefsNs, false))
    return;
  prefs.putBool(kKeyEnabled, enabled);
  prefs.putFloat(kKeyMu, matMu);
  prefs.putFloat(kKeyGs, greenSlowM);
  prefs.putFloat(kKeyGn, greenNormalM);
  prefs.putFloat(kKeyGf, greenFastM);
  prefs.putUChar(kKeyCalN, calN);
  prefs.putBytes(kKeyCalV, calV, sizeof(calV));
  prefs.putBytes(kKeyCalD, calD, sizeof(calD));
  prefs.end();
}

static void loadPrefs(void) {
  if (!prefs.begin(kPrefsNs, true))
    return;
  enabled = prefs.getBool(kKeyEnabled, false);
  matMu = prefs.getFloat(kKeyMu, kDefaultMatMu);
  greenSlowM = prefs.getFloat(kKeyGs, kDefaultGreenSlowM);
  greenNormalM = prefs.getFloat(kKeyGn, kDefaultGreenNormalM);
  greenFastM = prefs.getFloat(kKeyGf, kDefaultGreenFastM);
  calN = prefs.getUChar(kKeyCalN, 0);
  if (calN > PUTTING_CAL_MAX)
    calN = 0;
  memset(calV, 0, sizeof(calV));
  memset(calD, 0, sizeof(calD));
  prefs.getBytes(kKeyCalV, calV, sizeof(calV));
  prefs.getBytes(kKeyCalD, calD, sizeof(calD));
  prefs.end();
  if (!(matMu > 0.001f) || matMu > 2.f)
    matMu = kDefaultMatMu;
  if (!(greenSlowM >= 1.5f && greenSlowM <= 5.f))
    greenSlowM = kDefaultGreenSlowM;
  if (!(greenNormalM >= 1.5f && greenNormalM <= 5.f))
    greenNormalM = kDefaultGreenNormalM;
  if (!(greenFastM >= 1.5f && greenFastM <= 5.f))
    greenFastM = kDefaultGreenFastM;
}

static float medianSpeeds(float *vals, int n) {
  if (n <= 0)
    return 0.f;
  // Insertion sort — n ≤ 4.
  for (int i = 1; i < n; ++i) {
    float key = vals[i];
    int j = i - 1;
    while (j >= 0 && vals[j] > key) {
      vals[j + 1] = vals[j];
      --j;
    }
    vals[j + 1] = key;
  }
  if (n & 1)
    return vals[n / 2];
  return 0.5f * (vals[n / 2 - 1] + vals[n / 2]);
}

static bool computeSpeedFromGates(float *outSpeed) {
  int64_t ts[PUTTING_IR_GATES];
  uint8_t mask;
  noInterrupts();
  mask = gateMask;
  for (int i = 0; i < PUTTING_IR_GATES; ++i)
    ts[i] = gateTs[i];
  interrupts();

  float speeds[PUTTING_IR_GATES - 1];
  int n = 0;
  int prev = -1;
  for (int i = 0; i < PUTTING_IR_GATES; ++i) {
    if (!(mask & (1u << i)))
      continue;
    if (prev >= 0) {
      const int64_t dt = ts[i] - ts[prev];
      const int spanGates = i - prev;
      if (dt > 50 && spanGates > 0) {
        const float dx = kGateSpacingM * (float)spanGates;
        const float v = dx / ((float)dt * 1e-6f);
        if (v > 0.05f && v < 40.f)
          speeds[n++] = v;
      }
    }
    prev = i;
  }
  if (n <= 0)
    return false;
  *outSpeed = medianSpeeds(speeds, n);
  return *outSpeed > 0.f;
}

static void finalizeCapture(bool fromTimeout) {
  float v = 0.f;
  const bool ok = computeSpeedFromGates(&v);
  capturing = false;
  busyChanged = true;
  if (!ok) {
    Serial.printf("putting: capture failed (timeout=%d mask=0x%02x)\n",
                  (int)fromTimeout, (unsigned)gateMask);
    return;
  }
  lastSpeed = v;
  recomputeShotFromSpeed(v, &lastShot);
  hasLastShot = true;
  resultReady = true;
  Serial.printf("putting: v=%.2f mat=%.2f S=%.2f N=%.2f F=%.2f\n",
                lastShot.speed_mps, lastShot.mat_m, lastShot.slow_m,
                lastShot.normal_m, lastShot.fast_m);
}

static void IRAM_ATTR onIrEdge(void *arg) {
  const int idx = (int)(intptr_t)arg;
  if (idx < 0 || idx >= PUTTING_IR_GATES)
    return;
  const int64_t now = esp_timer_get_time();
  if ((now - lastEdgeUs[idx]) < kDebounceUs)
    return;
  lastEdgeUs[idx] = now;

  if (!capturing) {
    if (idx != 0)
      return;
    for (int i = 0; i < PUTTING_IR_GATES; ++i) {
      gateTs[i] = 0;
    }
    gateMask = 0;
    gateTs[0] = now;
    gateMask = 1u;
    captureStartUs = now;
    capturing = true;
    busyChanged = true;
    return;
  }

  if (!(gateMask & (1u << idx))) {
    gateTs[idx] = now;
    gateMask = (uint8_t)(gateMask | (1u << idx));
  }

  // All five hit — finalize from loop (avoid heavy work in ISR).
}

void puttingIrBegin(void) {
  loadPrefs();
  for (int i = 0; i < PUTTING_IR_GATES; ++i) {
    pinMode(kIrPins[i], INPUT_PULLUP);
    lastEdgeUs[i] = 0;
    gateTs[i] = 0;
    attachInterruptArg(digitalPinToInterrupt(kIrPins[i]), onIrEdge,
                       (void *)(intptr_t)i, FALLING);
  }
  Serial.printf("putting: IR pins %d %d %d %d %d  enabled=%d mu=%.4f cal=%u\n",
                kIrPins[0], kIrPins[1], kIrPins[2], kIrPins[3], kIrPins[4],
                (int)enabled, matMu, (unsigned)calN);
}

void puttingIrLoop(void) {
  if (!capturing)
    return;
  const int64_t now = esp_timer_get_time();
  const uint8_t mask = gateMask;
  const bool allHit = (mask == 0x1f);
  const bool timedOut = (now - captureStartUs) >= kShotTimeoutUs;
  if (allHit || timedOut)
    finalizeCapture(timedOut && !allHit);
}

bool puttingIsEnabled(void) { return enabled; }

void puttingSetEnabled(bool on) {
  if (enabled == on)
    return;
  enabled = on;
  modeChanged = true;
  savePrefs();
  Serial.printf("putting: mode %s\n", enabled ? "ON" : "OFF");
}

bool puttingTakeModeChanged(void) {
  if (!modeChanged)
    return false;
  modeChanged = false;
  return true;
}

bool puttingIsBusy(void) { return capturing; }

bool puttingTakeBusyChanged(void) {
  if (!busyChanged)
    return false;
  busyChanged = false;
  return true;
}

bool puttingTakeResult(PuttingShot *out) {
  if (!resultReady || !out)
    return false;
  resultReady = false;
  *out = lastShot;
  return true;
}

bool puttingHasLastShot(void) { return hasLastShot; }

void puttingGetLastShot(PuttingShot *out) {
  if (!out)
    return;
  *out = lastShot;
}

float puttingMatMu(void) { return matMu; }
int puttingCalCount(void) { return (int)calN; }
float puttingGreenSlowM(void) { return greenSlowM; }
float puttingGreenNormalM(void) { return greenNormalM; }
float puttingGreenFastM(void) { return greenFastM; }

void puttingReadGateLevels(bool levels[PUTTING_IR_GATES]) {
  if (!levels)
    return;
  for (int i = 0; i < PUTTING_IR_GATES; ++i)
    levels[i] = digitalRead(kIrPins[i]) == LOW;
}

bool puttingCalibrate(float measured_m, String *errMsg) {
  if (!hasLastShot || !(lastSpeed > 0.f)) {
    if (errMsg)
      *errMsg = F("no last shot");
    return false;
  }
  if (!(measured_m >= 0.2f && measured_m <= 20.f)) {
    if (errMsg)
      *errMsg = F("distance out of range");
    return false;
  }
  if (calN >= PUTTING_CAL_MAX) {
    memmove(&calV[0], &calV[1], sizeof(float) * (PUTTING_CAL_MAX - 1));
    memmove(&calD[0], &calD[1], sizeof(float) * (PUTTING_CAL_MAX - 1));
    calN = PUTTING_CAL_MAX - 1;
  }
  calV[calN] = lastSpeed;
  calD[calN] = measured_m;
  ++calN;
  refitMatMu();
  recomputeShotFromSpeed(lastSpeed, &lastShot);
  resultReady = true; // refresh LCD/UI with new mat + greens
  savePrefs();
  return true;
}

void puttingCalClear(void) {
  calN = 0;
  memset(calV, 0, sizeof(calV));
  memset(calD, 0, sizeof(calD));
  matMu = kDefaultMatMu;
  if (hasLastShot) {
    recomputeShotFromSpeed(lastSpeed, &lastShot);
    resultReady = true;
  }
  savePrefs();
}

bool puttingSetGreens(float slow_m, float normal_m, float fast_m,
                      String *errMsg) {
  if (!(slow_m >= 1.5f && slow_m <= 5.f) ||
      !(normal_m >= 1.5f && normal_m <= 5.f) ||
      !(fast_m >= 1.5f && fast_m <= 5.f)) {
    if (errMsg)
      *errMsg = F("each green speed must be 1.5–5.0 m");
    return false;
  }
  if (!(slow_m < normal_m && normal_m < fast_m)) {
    if (errMsg)
      *errMsg = F("need slow < normal < fast");
    return false;
  }
  greenSlowM = slow_m;
  greenNormalM = normal_m;
  greenFastM = fast_m;
  if (hasLastShot) {
    recomputeShotFromSpeed(lastSpeed, &lastShot);
    resultReady = true;
  }
  savePrefs();
  return true;
}

void puttingLiveJson(String &out) {
  char buf[384];
  const PuttingShot *s = &lastShot;
  snprintf(buf, sizeof(buf),
           "{\"ok\":true,\"enabled\":%s,\"busy\":%s,\"hasShot\":%s,"
           "\"speed\":%.2f,\"mat\":%.2f,\"slow\":%.2f,\"normal\":%.2f,"
           "\"fast\":%.2f,\"mu\":%.5f,\"calN\":%u,\"calReady\":%s,"
           "\"greenSlowM\":%.2f,\"greenNormalM\":%.2f,\"greenFastM\":%.2f}",
           enabled ? "true" : "false", capturing ? "true" : "false",
           hasLastShot ? "true" : "false",
           hasLastShot ? s->speed_mps : 0.f, hasLastShot ? s->mat_m : 0.f,
           hasLastShot ? s->slow_m : 0.f, hasLastShot ? s->normal_m : 0.f,
           hasLastShot ? s->fast_m : 0.f, matMu, (unsigned)calN,
           (calN >= 3) ? "true" : "false", greenSlowM, greenNormalM,
           greenFastM);
  out = buf;
}

#else

void puttingIrBegin(void) {}
void puttingIrLoop(void) {}
bool puttingIsEnabled(void) { return false; }
void puttingSetEnabled(bool) {}
bool puttingTakeModeChanged(void) { return false; }
bool puttingIsBusy(void) { return false; }
bool puttingTakeBusyChanged(void) { return false; }
bool puttingTakeResult(PuttingShot *) { return false; }
bool puttingHasLastShot(void) { return false; }
void puttingGetLastShot(PuttingShot *) {}
float puttingMatMu(void) { return 0.1f; }
int puttingCalCount(void) { return 0; }
float puttingGreenSlowM(void) { return 2.44f; }
float puttingGreenNormalM(void) { return 3.05f; }
float puttingGreenFastM(void) { return 3.66f; }
void puttingReadGateLevels(bool *) {}

#if defined(__cplusplus)
bool puttingCalibrate(float, String *) { return false; }
void puttingCalClear(void) {}
bool puttingSetGreens(float, float, float, String *) { return false; }
void puttingLiveJson(String &out) { out = F("{\"ok\":false}"); }
#endif

#endif
