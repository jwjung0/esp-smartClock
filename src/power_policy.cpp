#include "power_policy.h"

#include <Arduino.h>
#include <sys/time.h>

static bool radioActive = true;
static bool uiBoost = false;
static int appliedMhz = 0;

static void applyCpu(void) {
  const int mhz = (radioActive || uiBoost) ? 240 : 80;
  if (mhz == appliedMhz)
    return;
  appliedMhz = mhz;
  setCpuFrequencyMhz(mhz);
  Serial.printf("Power: CPU %d MHz (radio=%s ui=%s)\n", mhz,
                radioActive ? "on" : "off", uiBoost ? "boost" : "idle");
}

void powerSetRadioActive(bool radioOn) {
  radioActive = radioOn;
  applyCpu();
}

void powerSetUiBoost(bool boost) {
  uiBoost = boost;
  applyCpu();
}

bool powerRadioActive(void) { return radioActive; }

uint64_t powerUsUntilNextSecond(void) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t us = 1000000LL - (int64_t)tv.tv_usec;
  if (us < 50000)
    us = 50000;
  if (us > 1000000)
    us = 1000000;
  return (uint64_t)us;
}
