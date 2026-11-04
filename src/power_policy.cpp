#include "power_policy.h"

#include <Arduino.h>
#include <sys/time.h>

static bool radioActive = true;
static bool scaledOnce = false;

void powerSetRadioActive(bool radioOn) {
  if (scaledOnce && radioActive == radioOn)
    return;
  radioActive = radioOn;
  scaledOnce = true;
  if (radioOn) {
    setCpuFrequencyMhz(240);
    Serial.println("Power: CPU 240 MHz (radio on)");
  } else {
    setCpuFrequencyMhz(80);
    Serial.println("Power: CPU 80 MHz (radio off)");
  }
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
