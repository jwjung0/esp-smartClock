#include <Arduino.h>

#if defined(FIRMWARE_MODE_CHECKUP)
#include "board_check.h"
#elif defined(FIRMWARE_MODE_APP)
#include "clock_app.h"
#else
#error "Build with -e checkup or -e app (FIRMWARE_MODE_* not set)"
#endif

void setup() {
#if defined(FIRMWARE_MODE_CHECKUP)
  boardCheckSetup();
#elif defined(FIRMWARE_MODE_APP)
  clockAppSetup();
#endif
}

void loop() {
#if defined(FIRMWARE_MODE_CHECKUP)
  boardCheckLoop();
#elif defined(FIRMWARE_MODE_APP)
  clockAppLoop();
#endif
}
