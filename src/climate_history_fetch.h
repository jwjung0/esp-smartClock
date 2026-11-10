#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CLIMATE_HISTORY_DAILY_N 24
#define CLIMATE_HISTORY_MONTHLY_MAX 30

void climateHistoryFetchBegin(void);

// Blocking GET. Call only while STA is connected (ingest/boot task).
bool climateHistoryFetchOnce(void);

bool climateHistoryHasData(void);
bool climateHistoryTakeUpdated(void); // true once per successful parse

uint8_t climateHistoryMonthDays(void);

// Window from monthly_start / monthly_end (KST calendar dates).
bool climateHistoryGetMonthlyStart(int *yearOut, int *monthOut, int *dayOut);
bool climateHistoryGetMonthlyEnd(int *yearOut, int *monthOut, int *dayOut);
// Calendar month/day at series index (start + index).
bool climateHistoryMonthDateAt(uint8_t index, int *monthOut, int *dayOut);

// Copy chart Y arrays (lv_coord). Missing samples are LV_CHART_POINT_NONE.
// daily: always CLIMATE_HISTORY_DAILY_N entries.
// monthly: monthDays entries (caller should size buffer to MONTHLY_MAX).
bool climateHistoryCopyDaily(int16_t *tempOut, int16_t *rhOut, size_t n);
bool climateHistoryCopyMonthly(int16_t *tempOut, int16_t *rhOut, size_t n,
                               uint8_t *daysOut);

#if defined(__cplusplus)
}
#endif
