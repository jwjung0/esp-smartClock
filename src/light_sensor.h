#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init VEML7700 on shared climate Wire (call after bmeSensorBegin).
bool lightSensorBegin(void);

bool lightSensorOk(void);

// Read ambient lux. Returns false if sensor not ready or read failed.
bool lightSensorReadLux(float *lux);

void lightSensorPrint(void);

#ifdef __cplusplus
}
#endif
