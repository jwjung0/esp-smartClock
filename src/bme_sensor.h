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

// Blocking read (checkup / boot). Prefer bmeSensorRequest + Take in the UI loop.
// *humidityPct / *pressureHpa may be NAN when that sensor is absent.
bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa);

// Non-blocking sample for the UI loop. AHT20 needs ~80 ms; Request starts it,
// Take returns true once when a full reading is ready (no delay() waits).
void bmeSensorRequest(void);
bool bmeSensorTake(float *tempC, float *humidityPct, float *pressureHpa);

// One-line Serial printf of T / RH / P for the detected combo.
void bmeSensorPrint(void);

// Scan I2C bus and print responders + chip hints (checkup / debug).
void bmeSensorScanBus(void);

#ifdef __cplusplus
}
#endif
