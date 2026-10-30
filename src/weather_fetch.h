#pragma once

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

void weatherFetchBegin(void);

// HTTPS GET One Call 4.0 /current. Requires STA already connected.
// Returns true on parse success (updates cache).
bool weatherFetchOnce(void);

bool weatherFetchHasData(void);
bool weatherFetchTakeUpdated(void); // true once per successful fetch

bool weatherFetchGet(float *tempC, float *humidityPct, char *mainOut,
                     size_t mainOutLen);

#if defined(__cplusplus)
}
#endif
