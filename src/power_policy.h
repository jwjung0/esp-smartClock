#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Scale CPU for Wi‑Fi: on → 240 MHz, off → 80 MHz.
void powerSetRadioActive(bool radioOn);

// Optional UI boost to 240 MHz (normally left off — market runs at 80).
void powerSetUiBoost(bool boost);

bool powerRadioActive(void);

// µs until the next whole second (clamped 50ms–1000ms).
uint64_t powerUsUntilNextSecond(void);

#ifdef __cplusplus
}
#endif
