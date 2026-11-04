#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init I2C on PIN_BME_SDA/SCL.
// Auto-detects BME280 (0x60), BMP280 (0x58), and/or AHT20 (0x38/0x39).
// Common combo board: BMP280 (T+P) + AHT20 (RH).
bool bmeSensorBegin(void);

// True after a successful begin.
bool bmeSensorOk(void);

// True for BME280 or when AHT20 is present (BMP280 alone has no humidity).
bool bmeSensorHasHumidity(void);

// Read latest sample. Returns false if sensor not ready or read failed.
// *humidityPct / *pressureHpa may be NAN when that sensor is absent.
bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa);

// One-line Serial printf of T / RH / P for the detected combo.
void bmeSensorPrint(void);

// Scan I2C bus and print responders + chip hints (checkup / debug).
void bmeSensorScanBus(void);

#ifdef __cplusplus
}
#endif
