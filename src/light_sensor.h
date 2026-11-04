#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init VEML7700 on PIN_VEML_SDA/SCL.
// s2mini: shared climate Wire (call after bmeSensorBegin).
// esp32dev: dedicated Wire1 on GPIO32/33.
bool lightSensorBegin(void);

bool lightSensorOk(void);

// Read ambient lux. Returns false if sensor not ready or read failed.
bool lightSensorReadLux(float *lux);

void lightSensorPrint(void);

#ifdef __cplusplus
}
#endif
