#pragma once

#include <stddef.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

void weatherFetchBegin(void);

// HTTPS fetch Naver KMA + OpenWeather. Requires STA already connected.
// Returns true if at least one source updated the preferred cache.
bool weatherFetchOnce(void);

bool weatherFetchHasData(void);
bool weatherFetchTakeUpdated(void); // true once per successful preferred update

// Preferred outdoor snapshot (user source first, other as fallback).
bool weatherFetchGet(float *tempC, float *humidityPct, char *mainOut,
                     size_t mainOutLen);

// Condition id: OpenWeather weather[0].id, or Naver wetrCd when source is Naver.
bool weatherFetchGetId(int *idOut);

// true when active outdoor snapshot is from Naver (id is wetrCd).
bool weatherFetchIsNaver(void);

// Config portal / NVS: weather source preference (default Naver).
bool weatherConfigPreferNaver(void);
const char *weatherConfigSourceId(void); // "naver" or "openweather"
bool weatherConfigSetSource(const char *src); // "naver" | "openweather"

// UTC unix sunrise/sunset from preferred source (last successful fetch).
bool weatherFetchGetSunTimes(time_t *sunrise, time_t *sunset);

#if defined(__cplusplus)
}
#endif
