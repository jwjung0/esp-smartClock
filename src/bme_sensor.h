#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C on PIN_BME_SDA/SCL; auto-detects BME280 (0x60) or BMP280 (0x58).
bool bmeSensorBegin(void);

// True after a successful begin.
bool bmeSensorOk(void);

// True only for real BME280 (BMP280 has no humidity).
bool bmeSensorHasHumidity(void);

// Read latest sample. Returns false if sensor not ready or read failed.
// On BMP280, *humidityPct is set to NAN when non-NULL.
bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa);

// One-line Serial printf of T / RH / P (or T / P for BMP280).
void bmeSensorPrint(void);

// Scan I2C bus and print responders + chip-id hints (checkup / debug).
void bmeSensorScanBus(void);

#ifdef __cplusplus
}
#endif
