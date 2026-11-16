#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MOISTURE_READINGS_DAILY_MAX 30

void moistureReadingsFetchBegin(void);

// Blocking GET. Call only while STA is connected (ingest/boot task).
bool moistureReadingsFetchOnce(void);

bool moistureReadingsHasData(void);
bool moistureReadingsTakeUpdated(void); // true once per successful parse

uint8_t moistureReadingsDayCount(void);

// Window from first/last daily bucket (KST calendar dates).
bool moistureReadingsGetStart(int *yearOut, int *monthOut, int *dayOut);
bool moistureReadingsGetEnd(int *yearOut, int *monthOut, int *dayOut);
bool moistureReadingsDateAt(uint8_t index, int *monthOut, int *dayOut);

// Latest sample moisture_pct (for home banner).
bool moistureReadingsLatestPct(float *pctOut);

// Copy chart Y array (lv_coord). Missing samples are LV_CHART_POINT_NONE.
// daysOut = number of daily points written (≤ MOISTURE_READINGS_DAILY_MAX).
bool moistureReadingsCopyDaily(int16_t *pctOut, size_t n, uint8_t *daysOut);

#if defined(__cplusplus)
}
#endif
