#include "moisture_readings_fetch.h"

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
static bool hasLatest = false;
static float latestPct = NAN;
static uint8_t dayCount = 0;
static Ymd rangeStart = {};
static Ymd rangeEnd = {};
static int16_t dailyPct[MOISTURE_READINGS_DAILY_MAX];
static Ymd dailyDate[MOISTURE_READINGS_DAILY_MAX];

static void clearSeries(void) {
  for (size_t i = 0; i < MOISTURE_READINGS_DAILY_MAX; ++i) {
    dailyPct[i] = LV_CHART_POINT_NONE;
    dailyDate[i] = {};
  }
  dayCount = 0;
  rangeStart = {};
  rangeEnd = {};
  hasLatest = false;
  latestPct = NAN;
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

static bool parseYmdPrefix(const char *s, Ymd *out) {
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

static bool sameYmd(const Ymd &a, const Ymd &b) {
  return a.ok && b.ok && a.y == b.y && a.m == b.m && a.d == b.d;
}

static void pushDaily(const Ymd &day, float mean) {
  if (!day.ok || isnan(mean))
    return;
  if (dayCount >= MOISTURE_READINGS_DAILY_MAX) {
    memmove(&dailyPct[0], &dailyPct[1],
            sizeof(dailyPct[0]) * (MOISTURE_READINGS_DAILY_MAX - 1));
    memmove(&dailyDate[0], &dailyDate[1],
            sizeof(dailyDate[0]) * (MOISTURE_READINGS_DAILY_MAX - 1));
    dayCount = MOISTURE_READINGS_DAILY_MAX - 1;
  }
  dailyPct[dayCount] = toCoord(mean);
  dailyDate[dayCount] = day;
  dayCount++;
}

void moistureReadingsFetchBegin(void) {
  clearSeries();
  hasData = false;
  updatedFlag = false;
  Serial.println("moisture readings: ready");
}

bool moistureReadingsFetchOnce(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("moisture readings: not connected");
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(MOISTURE_READINGS_URL)) {
    Serial.println("moisture readings: http.begin failed");
    return false;
  }
  char authHdr[192];
  snprintf(authHdr, sizeof(authHdr), "Bearer %s", API_ACCESS_TOKEN);
  http.addHeader("Authorization", authHdr);

  yield();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("moisture readings: HTTP %d\n", code);
    http.end();
    yield();
    return false;
  }

  // Raw list can be hundreds of samples; keep room for day buckets.
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  yield();
  if (err) {
    Serial.printf("moisture readings: JSON %s\n", err.c_str());
    return false;
  }

  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull()) {
    Serial.println("moisture readings: expected array");
    return false;
  }

  clearSeries();

  Ymd curDay = {};
  float sum = 0;
  int n = 0;
  float lastPct = NAN;

  for (JsonObject row : rows) {
    if (row.isNull())
      continue;
    if (!row["moisture_pct"].is<float>() && !row["moisture_pct"].is<int>() &&
        !row["moisture_pct"].is<double>() && !row["moisture_pct"].is<long>())
      continue;
    const float pct = row["moisture_pct"].as<float>();
    const char *at = row["measured_at"] | "";
    Ymd day = {};
    if (!parseYmdPrefix(at, &day))
      continue;

    lastPct = pct;

    if (!curDay.ok) {
      curDay = day;
      sum = pct;
      n = 1;
      continue;
    }
    if (sameYmd(curDay, day)) {
      sum += pct;
      n++;
      continue;
    }
    if (n > 0)
      pushDaily(curDay, sum / (float)n);
    curDay = day;
    sum = pct;
    n = 1;
  }
  if (n > 0)
    pushDaily(curDay, sum / (float)n);

  if (!isnan(lastPct)) {
    latestPct = lastPct;
    hasLatest = true;
  }

  if (dayCount > 0) {
    rangeStart = dailyDate[0];
    rangeEnd = dailyDate[dayCount - 1];
  }

  hasData = hasLatest || dayCount > 0;
  updatedFlag = true;
  Serial.printf("moisture readings ok  days=%u", dayCount);
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

uint8_t moistureReadingsDayCount(void) { return dayCount; }

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

bool moistureReadingsDateAt(uint8_t index, int *monthOut, int *dayOut) {
  if (index >= dayCount || !dailyDate[index].ok)
    return false;
  if (monthOut)
    *monthOut = dailyDate[index].m;
  if (dayOut)
    *dayOut = dailyDate[index].d;
  return true;
}

bool moistureReadingsLatestPct(float *pctOut) {
  if (!hasLatest || !pctOut || isnan(latestPct))
    return false;
  *pctOut = latestPct;
  return true;
}

bool moistureReadingsCopyDaily(int16_t *pctOut, size_t n, uint8_t *daysOut) {
  if (!hasData || !pctOut || n < MOISTURE_READINGS_DAILY_MAX)
    return false;
  for (size_t i = 0; i < MOISTURE_READINGS_DAILY_MAX; ++i)
    pctOut[i] = (i < dayCount) ? dailyPct[i] : LV_CHART_POINT_NONE;
  if (daysOut)
    *daysOut = dayCount;
  return true;
}

#else

void moistureReadingsFetchBegin(void) {}
bool moistureReadingsFetchOnce(void) { return false; }
bool moistureReadingsHasData(void) { return false; }
bool moistureReadingsTakeUpdated(void) { return false; }
uint8_t moistureReadingsDayCount(void) { return 0; }
bool moistureReadingsGetStart(int *, int *, int *) { return false; }
bool moistureReadingsGetEnd(int *, int *, int *) { return false; }
bool moistureReadingsDateAt(uint8_t, int *, int *) { return false; }
bool moistureReadingsLatestPct(float *) { return false; }
bool moistureReadingsCopyDaily(int16_t *, size_t, uint8_t *) { return false; }

#endif
