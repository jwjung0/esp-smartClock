#pragma once

#include <Arduino.h>
#include <time.h>

void timeSyncBegin();
bool timeSyncIsReady();
void timeSyncRequestResync();

// True when local wall clock is available (no 1 Hz filter).
bool timeSyncGet(struct tm *out);
// True when local time is valid and the second changed since last call.
bool timeSyncTick(struct tm *out);

void timeSyncFormatTime(const struct tm &t, char *buf, size_t len);
void timeSyncFormatDate(const struct tm &t, char *buf, size_t len);
