#include "climate_history_fetch.h"

#if defined(FIRMWARE_MODE_APP)

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
static uint8_t monthDays = 0;
static Ymd monthStart = {};
static Ymd monthEnd = {};
static int16_t dailyT[CLIMATE_HISTORY_DAILY_N];
static int16_t dailyRh[CLIMATE_HISTORY_DAILY_N];
static int16_t monthlyT[CLIMATE_HISTORY_MONTHLY_MAX];
static int16_t monthlyRh[CLIMATE_HISTORY_MONTHLY_MAX];

static void clearSeries(void) {
  for (size_t i = 0; i < CLIMATE_HISTORY_DAILY_N; ++i) {
    dailyT[i] = LV_CHART_POINT_NONE;
    dailyRh[i] = LV_CHART_POINT_NONE;
  }
  for (size_t i = 0; i < CLIMATE_HISTORY_MONTHLY_MAX; ++i) {
    monthlyT[i] = LV_CHART_POINT_NONE;
    monthlyRh[i] = LV_CHART_POINT_NONE;
  }
  monthDays = 0;
  monthStart = {};
  monthEnd = {};
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

static bool parseFloatOrNull(JsonVariantConst v, int16_t *out) {
  if (!out)
    return false;
  if (v.isNull()) {
    *out = LV_CHART_POINT_NONE;
    return true;
  }
  if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>()) {
    *out = toCoord(v.as<float>());
    return true;
  }
  // Missing key → treat as null gap.
  *out = LV_CHART_POINT_NONE;
  return true;
}

static bool parseYmd(const char *s, Ymd *out) {
  if (!out)
    return false;
  *out = {};
  if (!s || !s[0])
    return false;
  int y = 0, m = 0, d = 0;
  if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3)
    return false;
  if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31)
    return false;
  out->y = y;
  out->m = m;
  out->d = d;
  out->ok = true;
  return true;
}

static bool ymdToTm(const Ymd &in, struct tm *out) {
  if (!out || !in.ok)
    return false;
  memset(out, 0, sizeof(*out));
  out->tm_year = in.y - 1900;
  out->tm_mon = in.m - 1;
  out->tm_mday = in.d;
  out->tm_isdst = -1;
  return mktime(out) != (time_t)-1;
}

static bool addDaysYmd(const Ymd &start, int days, Ymd *out) {
  if (!out || !start.ok || days < 0)
    return false;
  struct tm t;
  if (!ymdToTm(start, &t))
    return false;
  t.tm_mday += days;
  t.tm_isdst = -1;
  if (mktime(&t) == (time_t)-1)
    return false;
  out->y = t.tm_year + 1900;
  out->m = t.tm_mon + 1;
  out->d = t.tm_mday;
  out->ok = true;
  return true;
}

void climateHistoryFetchBegin(void) {
  clearSeries();
  hasData = false;
  updatedFlag = false;
  Serial.println("climate history: ready");
}

bool climateHistoryFetchOnce(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("climate history: not connected");
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(HISTORY_URL)) {
    Serial.println("climate history: http.begin failed");
    return false;
  }
  http.addHeader("X-Device-Token", DEVICE_INGEST_TOKEN);

  yield();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("climate history: HTTP %d\n", code);
    http.end();
    yield();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  yield();
  if (err) {
    Serial.printf("climate history: JSON %s\n", err.c_str());
    return false;
  }

  clearSeries();

  JsonArray daily = doc["daily"].as<JsonArray>();
  if (!daily.isNull()) {
    for (JsonObject row : daily) {
      if (row.isNull() || !row["h"].is<int>())
        continue;
      const int h = row["h"].as<int>();
      if (h < 0 || h >= CLIMATE_HISTORY_DAILY_N)
        continue;
      int16_t t = LV_CHART_POINT_NONE;
      int16_t rh = LV_CHART_POINT_NONE;
      (void)parseFloatOrNull(row["t"], &t);
      (void)parseFloatOrNull(row["rh"], &rh);
      dailyT[h] = t;
      dailyRh[h] = rh;
    }
  }

  Ymd start = {};
  Ymd end = {};
  const char *startStr = doc["monthly_start"] | "";
  const char *endStr = doc["monthly_end"] | "";
  (void)parseYmd(startStr, &start);
  (void)parseYmd(endStr, &end);

  JsonArray monthly = doc["monthly"].as<JsonArray>();
  size_t n = 0;
  if (!monthly.isNull()) {
    n = monthly.size();
    if (n > CLIMATE_HISTORY_MONTHLY_MAX)
      n = CLIMATE_HISTORY_MONTHLY_MAX;
    for (size_t i = 0; i < n; ++i) {
      JsonVariant rowVar = monthly[i];
      int16_t t = LV_CHART_POINT_NONE;
      int16_t rh = LV_CHART_POINT_NONE;
      if (rowVar.isNull()) {
        monthlyT[i] = t;
        monthlyRh[i] = rh;
        continue;
      }
      JsonObject row = rowVar.as<JsonObject>();
      if (row.isNull()) {
        monthlyT[i] = t;
        monthlyRh[i] = rh;
        continue;
      }
      (void)parseFloatOrNull(row["t"], &t);
      (void)parseFloatOrNull(row["rh"], &rh);
      monthlyT[i] = t;
      monthlyRh[i] = rh;
    }
  }

  monthDays = (uint8_t)n;
  monthStart = start;
  monthEnd = end;
  // If end missing but start+len known, derive end for title/labels.
  if (monthStart.ok && !monthEnd.ok && monthDays > 0)
    (void)addDaysYmd(monthStart, (int)monthDays - 1, &monthEnd);

  hasData = true;
  updatedFlag = true;
  Serial.printf("climate history ok  daily=24 monthly_days=%u", monthDays);
  if (monthStart.ok && monthEnd.ok)
    Serial.printf("  %04d-%02d-%02d..%04d-%02d-%02d", monthStart.y,
                  monthStart.m, monthStart.d, monthEnd.y, monthEnd.m,
                  monthEnd.d);
  Serial.println();
  return true;
}

bool climateHistoryHasData(void) { return hasData; }

bool climateHistoryTakeUpdated(void) {
  if (!updatedFlag)
    return false;
  updatedFlag = false;
  return true;
}

uint8_t climateHistoryMonthDays(void) { return monthDays; }

bool climateHistoryGetMonthlyStart(int *yearOut, int *monthOut, int *dayOut) {
  if (!monthStart.ok)
    return false;
  if (yearOut)
    *yearOut = monthStart.y;
  if (monthOut)
    *monthOut = monthStart.m;
  if (dayOut)
    *dayOut = monthStart.d;
  return true;
}

bool climateHistoryGetMonthlyEnd(int *yearOut, int *monthOut, int *dayOut) {
  if (!monthEnd.ok)
    return false;
  if (yearOut)
    *yearOut = monthEnd.y;
  if (monthOut)
    *monthOut = monthEnd.m;
  if (dayOut)
    *dayOut = monthEnd.d;
  return true;
}

bool climateHistoryMonthDateAt(uint8_t index, int *monthOut, int *dayOut) {
  if (!monthStart.ok || index >= monthDays)
    return false;
  Ymd at = {};
  if (!addDaysYmd(monthStart, (int)index, &at))
    return false;
  if (monthOut)
    *monthOut = at.m;
  if (dayOut)
    *dayOut = at.d;
  return true;
}

bool climateHistoryCopyDaily(int16_t *tempOut, int16_t *rhOut, size_t n) {
  if (!hasData || !tempOut || !rhOut || n < CLIMATE_HISTORY_DAILY_N)
    return false;
  memcpy(tempOut, dailyT, sizeof(dailyT));
  memcpy(rhOut, dailyRh, sizeof(dailyRh));
  return true;
}

bool climateHistoryCopyMonthly(int16_t *tempOut, int16_t *rhOut, size_t n,
                               uint8_t *daysOut) {
  if (!hasData || !tempOut || !rhOut || n < CLIMATE_HISTORY_MONTHLY_MAX)
    return false;
  memcpy(tempOut, monthlyT, sizeof(monthlyT));
  memcpy(rhOut, monthlyRh, sizeof(monthlyRh));
  if (daysOut)
    *daysOut = monthDays;
  return true;
}

#else

void climateHistoryFetchBegin(void) {}
bool climateHistoryFetchOnce(void) { return false; }
bool climateHistoryHasData(void) { return false; }
bool climateHistoryTakeUpdated(void) { return false; }
uint8_t climateHistoryMonthDays(void) { return 0; }
bool climateHistoryGetMonthlyStart(int *, int *, int *) { return false; }
bool climateHistoryGetMonthlyEnd(int *, int *, int *) { return false; }
bool climateHistoryMonthDateAt(uint8_t, int *, int *) { return false; }
bool climateHistoryCopyDaily(int16_t *, int16_t *, size_t) { return false; }
bool climateHistoryCopyMonthly(int16_t *, int16_t *, size_t, uint8_t *) {
  return false;
}

#endif
