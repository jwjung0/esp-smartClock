#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Cap ≥ server rolling window (~20d×24h). Chart plots every hourly point.
#define MOISTURE_READINGS_MAX 500

void moistureReadingsFetchBegin(void);

// Blocking GET. Call only while STA is connected (ingest/boot task).
bool moistureReadingsFetchOnce(void);

bool moistureReadingsHasData(void);
bool moistureReadingsTakeUpdated(void); // true once per successful parse

uint16_t moistureReadingsCount(void);

// Calendar day span of the series (for period title).
uint16_t moistureReadingsDaySpan(void);

// Window from first/last hourly `at` (display TZ calendar dates).
bool moistureReadingsGetStart(int *yearOut, int *monthOut, int *dayOut);
bool moistureReadingsGetEnd(int *yearOut, int *monthOut, int *dayOut);

// Per-point calendar fields for X tick labels.
bool moistureReadingsDateAt(uint16_t index, int *monthOut, int *dayOut);
bool moistureReadingsHourAt(uint16_t index, int *hourOut);

// Latest non-null hourly m (for home banner).
bool moistureReadingsLatestPct(float *pctOut);

// Copy chart Y array (lv_coord). Unused slots are LV_CHART_POINT_NONE.
// countOut = number of hourly points written (≤ MOISTURE_READINGS_MAX).
bool moistureReadingsCopySeries(int16_t *pctOut, size_t n, uint16_t *countOut);

// Primary Y: floor(min) / ceil(max) of valid hourly m values.
bool moistureReadingsGetPctAxis(int16_t *yminOut, int16_t *ymaxOut);

#if defined(__cplusplus)
}
#endif
