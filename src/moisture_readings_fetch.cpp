#include "moisture_readings_fetch.h"

#if defined(FIRMWARE_MODE_APP)

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(INGEST_ENV_LOCAL)
#include "ingest_config_local.h"
#elif defined(INGEST_ENV_PROD)
#include "ingest_config_prod.h"
#else
#error "Build app with INGEST_ENV_LOCAL or INGEST_ENV_PROD"
#endif

#ifndef LV_CHART_POINT_NONE
#define LV_CHART_POINT_NONE ((int16_t)0x7FFF)
#endif

struct Ymd {
  int y;
  int m; // 1..12
  int d; // 1..31
  bool ok;
};

static bool hasData = false;
static bool updatedFlag = false;
static bool hasLatest = false;
static float latestPct = NAN;
static uint16_t sampleCount = 0;
static uint16_t daySpan = 0;
static Ymd rangeStart = {};
static Ymd rangeEnd = {};
static int16_t samples[MOISTURE_READINGS_MAX];
static uint8_t sampleMon[MOISTURE_READINGS_MAX];
static uint8_t sampleDay[MOISTURE_READINGS_MAX];
static uint8_t sampleHour[MOISTURE_READINGS_MAX];
static float sampleMin = NAN;
static float sampleMax = NAN;

static const char *kPrefsUiNs = "clockui";
static const char *kKeyWarn = "moist_warn";
static const char *kKeyCrit = "moist_crit";
static const uint8_t kWarnDefault = 40;
static const uint8_t kCritDefault = 20;
static uint8_t warnPct = kWarnDefault;
static uint8_t critPct = kCritDefault;
static bool prefsLoaded = false;
static volatile bool warnChangedFlag = false;

static void clampWarnCrit(uint8_t *warnInOut, uint8_t *critInOut) {
  if (!warnInOut || !critInOut)
    return;
  uint8_t w = *warnInOut;
  uint8_t c = *critInOut;
  if (w < 1)
    w = 1;
  if (w > 100)
    w = 100;
  if (c < 1)
    c = 1;
  if (c > 100)
    c = 100;
  if (c > w)
    c = w;
  *warnInOut = w;
  *critInOut = c;
}

void moistureConfigLoadPrefs(void) {
  Preferences prefs;
  warnPct = kWarnDefault;
  critPct = kCritDefault;
  if (prefs.begin(kPrefsUiNs, true)) {
    warnPct = (uint8_t)prefs.getUChar(kKeyWarn, kWarnDefault);
    critPct = (uint8_t)prefs.getUChar(kKeyCrit, kCritDefault);
    prefs.end();
  }
  clampWarnCrit(&warnPct, &critPct);
  prefsLoaded = true;
}

static void ensureWarnPrefs(void) {
  if (!prefsLoaded)
    moistureConfigLoadPrefs();
}

uint8_t moistureConfigWarnPct(void) {
  ensureWarnPrefs();
  return warnPct;
}

uint8_t moistureConfigCritPct(void) {
  ensureWarnPrefs();
  return critPct;
}

bool moistureConfigSetWarnCritPct(uint8_t warnIn, uint8_t critIn) {
  clampWarnCrit(&warnIn, &critIn);
  Preferences prefs;
  if (!prefs.begin(kPrefsUiNs, false))
    return false;
  prefs.putUChar(kKeyWarn, warnIn);
  prefs.putUChar(kKeyCrit, critIn);
  prefs.end();
  warnPct = warnIn;
  critPct = critIn;
  prefsLoaded = true;
  warnChangedFlag = true;
  Serial.printf("moisture warn=%u%% crit=%u%%\n", (unsigned)warnPct,
                (unsigned)critPct);
  return true;
}

bool moistureConfigTakeChanged(void) {
  if (!warnChangedFlag)
    return false;
  warnChangedFlag = false;
  return true;
}

static void clearSeries(void) {
  for (size_t i = 0; i < MOISTURE_READINGS_MAX; ++i) {
    samples[i] = LV_CHART_POINT_NONE;
    sampleMon[i] = 0;
    sampleDay[i] = 0;
    sampleHour[i] = 0;
  }
  sampleCount = 0;
  daySpan = 0;
  rangeStart = {};
  rangeEnd = {};
  hasLatest = false;
  latestPct = NAN;
  sampleMin = NAN;
  sampleMax = NAN;
}

static int16_t toCoord(float v) {
  if (isnan(v))
    return LV_CHART_POINT_NONE;
  const long r = lroundf(v);
  if (r > 32766)
    return 32766;
  if (r < -32767)
    return -32767;
  return (int16_t)r;
}

static void notePctFloat(float v, float *mn, float *mx) {
  if (!mn || !mx || isnan(v))
    return;
  if (isnan(*mn) || v < *mn)
    *mn = v;
  if (isnan(*mx) || v > *mx)
    *mx = v;
}

static bool pctAxisFromFloats(float pmin, float pmax, int16_t *yminOut,
                              int16_t *ymaxOut) {
  if (!yminOut || !ymaxOut || isnan(pmin) || isnan(pmax))
    return false;
  // Axis ints: floor(min), ceil(max) — e.g. 45.2 → 45..46.
  long lo = (long)floorf(pmin);
  long hi = (long)ceilf(pmax);
  if (lo > 32766)
    lo = 32766;
  if (lo < -32767)
    lo = -32767;
  if (hi > 32766)
    hi = 32766;
  if (hi < -32767)
    hi = -32767;
  if (lo >= hi)
    hi = lo + 1;
  *yminOut = (int16_t)lo;
  *ymaxOut = (int16_t)hi;
  return true;
}

static bool parseAt(const char *s, Ymd *dayOut, int *hourOut) {
  if (dayOut)
    *dayOut = {};
  if (hourOut)
    *hourOut = 0;
  if (!s || !s[0])
    return false;
  int y = 0, m = 0, d = 0, h = 0;
  // "2026-11-20T10:00:00+09:00" — hour optional for start/end.
  if (sscanf(s, "%d-%d-%dT%d", &y, &m, &d, &h) < 3)
    return false;
  if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31)
    return false;
  if (h < 0 || h > 23)
    h = 0;
  if (dayOut) {
    dayOut->y = y;
    dayOut->m = m;
    dayOut->d = d;
    dayOut->ok = true;
  }
  if (hourOut)
    *hourOut = h;
  return true;
}

static bool sameYmd(const Ymd &a, const Ymd &b) {
  return a.ok && b.ok && a.y == b.y && a.m == b.m && a.d == b.d;
}

void moistureReadingsFetchBegin(void) {
  clearSeries();
  hasData = false;
  updatedFlag = false;
  moistureConfigLoadPrefs();
  Serial.println("moisture readings: ready");
}

bool moistureReadingsFetchOnce(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("moisture readings: not connected");
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(MOISTURE_HISTORY_URL)) {
    Serial.println("moisture readings: http.begin failed");
    return false;
  }
  http.addHeader("X-Device-Token", DEVICE_INGEST_TOKEN);

  yield();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("moisture readings: HTTP %d\n", code);
    http.end();
    yield();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  yield();
  if (err) {
    Serial.printf("moisture readings: JSON %s\n", err.c_str());
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    Serial.println("moisture readings: expected object");
    return false;
  }

  JsonArray rows = root["hourly"].as<JsonArray>();
  if (rows.isNull()) {
    Serial.println("moisture readings: expected hourly array");
    return false;
  }

  clearSeries();

  Ymd start = {};
  Ymd end = {};
  (void)parseAt(root["start"] | "", &start, nullptr);
  (void)parseAt(root["end"] | "", &end, nullptr);

  const size_t total = rows.size();
  size_t begin = 0;
  if (total > MOISTURE_READINGS_MAX)
    begin = total - MOISTURE_READINGS_MAX;

  float lastPct = NAN;
  Ymd prevDay = {};
  uint16_t span = 0;

  for (size_t i = begin; i < total; ++i) {
    JsonObject row = rows[i].as<JsonObject>();
    if (row.isNull())
      continue;
    if (sampleCount >= MOISTURE_READINGS_MAX)
      break;

    Ymd day = {};
    int hour = 0;
    const char *at = row["at"] | "";
    if (!parseAt(at, &day, &hour)) {
      samples[sampleCount++] = LV_CHART_POINT_NONE;
      continue;
    }

    sampleMon[sampleCount] = (uint8_t)day.m;
    sampleDay[sampleCount] = (uint8_t)day.d;
    sampleHour[sampleCount] = (uint8_t)hour;

    if (!prevDay.ok || !sameYmd(prevDay, day)) {
      span++;
      prevDay = day;
    }
    if (!rangeStart.ok)
      rangeStart = day;
    rangeEnd = day;

    if (row["m"].isNull() ||
        (!row["m"].is<float>() && !row["m"].is<int>() &&
         !row["m"].is<double>() && !row["m"].is<long>())) {
      samples[sampleCount++] = LV_CHART_POINT_NONE;
      continue;
    }
    const float pct = row["m"].as<float>();
    samples[sampleCount++] = toCoord(pct);
    notePctFloat(pct, &sampleMin, &sampleMax);
    lastPct = pct;
  }

  if (start.ok)
    rangeStart = start;
  if (end.ok)
    rangeEnd = end;
  daySpan = span;

  if (!isnan(lastPct)) {
    latestPct = lastPct;
    hasLatest = true;
  }

  hasData = sampleCount > 0;
  updatedFlag = true;
  Serial.printf("moisture readings ok  hourly=%u  days=%u", sampleCount,
                daySpan);
  if (hasLatest)
    Serial.printf("  latest=%.1f", latestPct);
  if (rangeStart.ok && rangeEnd.ok)
    Serial.printf("  %04d-%02d-%02d..%04d-%02d-%02d", rangeStart.y,
                  rangeStart.m, rangeStart.d, rangeEnd.y, rangeEnd.m,
                  rangeEnd.d);
  Serial.println();
  return true;
}

bool moistureReadingsHasData(void) { return hasData; }

bool moistureReadingsTakeUpdated(void) {
  if (!updatedFlag)
    return false;
  updatedFlag = false;
  return true;
}

uint16_t moistureReadingsCount(void) { return sampleCount; }

uint16_t moistureReadingsDaySpan(void) { return daySpan; }

bool moistureReadingsGetStart(int *yearOut, int *monthOut, int *dayOut) {
  if (!rangeStart.ok)
    return false;
  if (yearOut)
    *yearOut = rangeStart.y;
  if (monthOut)
    *monthOut = rangeStart.m;
  if (dayOut)
    *dayOut = rangeStart.d;
  return true;
}

bool moistureReadingsGetEnd(int *yearOut, int *monthOut, int *dayOut) {
  if (!rangeEnd.ok)
    return false;
  if (yearOut)
    *yearOut = rangeEnd.y;
  if (monthOut)
    *monthOut = rangeEnd.m;
  if (dayOut)
    *dayOut = rangeEnd.d;
  return true;
}

bool moistureReadingsDateAt(uint16_t index, int *monthOut, int *dayOut) {
  if (index >= sampleCount || sampleMon[index] < 1 || sampleDay[index] < 1)
    return false;
  if (monthOut)
    *monthOut = sampleMon[index];
  if (dayOut)
    *dayOut = sampleDay[index];
  return true;
}

bool moistureReadingsHourAt(uint16_t index, int *hourOut) {
  if (index >= sampleCount || !hourOut)
    return false;
  *hourOut = sampleHour[index];
  return true;
}

bool moistureReadingsLatestPct(float *pctOut) {
  if (!hasLatest || !pctOut || isnan(latestPct))
    return false;
  *pctOut = latestPct;
  return true;
}

bool moistureReadingsCopySeries(int16_t *pctOut, size_t n, uint16_t *countOut) {
  if (!hasData || !pctOut || n < MOISTURE_READINGS_MAX)
    return false;
  for (size_t i = 0; i < MOISTURE_READINGS_MAX; ++i)
    pctOut[i] = (i < sampleCount) ? samples[i] : LV_CHART_POINT_NONE;
  if (countOut)
    *countOut = sampleCount;
  return true;
}

bool moistureReadingsGetPctAxis(int16_t *yminOut, int16_t *ymaxOut) {
  if (!hasData)
    return false;
  return pctAxisFromFloats(sampleMin, sampleMax, yminOut, ymaxOut);
}

#else

void moistureReadingsFetchBegin(void) {}
bool moistureReadingsFetchOnce(void) { return false; }
bool moistureReadingsHasData(void) { return false; }
bool moistureReadingsTakeUpdated(void) { return false; }
uint16_t moistureReadingsCount(void) { return 0; }
uint16_t moistureReadingsDaySpan(void) { return 0; }
bool moistureReadingsGetStart(int *, int *, int *) { return false; }
bool moistureReadingsGetEnd(int *, int *, int *) { return false; }
bool moistureReadingsDateAt(uint16_t, int *, int *) { return false; }
bool moistureReadingsHourAt(uint16_t, int *) { return false; }
bool moistureReadingsLatestPct(float *) { return false; }
bool moistureReadingsCopySeries(int16_t *, size_t, uint16_t *) { return false; }
bool moistureReadingsGetPctAxis(int16_t *, int16_t *) { return false; }
void moistureConfigLoadPrefs(void) {}
uint8_t moistureConfigWarnPct(void) { return 40; }
uint8_t moistureConfigCritPct(void) { return 20; }
bool moistureConfigSetWarnCritPct(uint8_t, uint8_t) { return false; }
bool moistureConfigTakeChanged(void) { return false; }

#endif
