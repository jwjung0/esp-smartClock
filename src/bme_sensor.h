#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C on PIN_BME_SDA/SCL and probe BME280 (0x76, then 0x77).
bool bmeSensorBegin(void);

// True after a successful begin.
bool bmeSensorOk(void);

// Read latest sample. Returns false if sensor not ready or read failed.
bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa);

// One-line Serial printf of T / RH / P (or an error).
void bmeSensorPrint(void);

#ifdef __cplusplus
}
#endif
