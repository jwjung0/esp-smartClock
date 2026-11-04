#include "light_sensor.h"

#include "board_pins.h"

#include <Adafruit_VEML7700.h>
#include <Arduino.h>
#include <Wire.h>

static Adafruit_VEML7700 veml;
static bool ready = false;

bool lightSensorBegin(void) {
  ready = false;

  // Shared climate Wire — call after bmeSensorBegin() so the bus is up.
  if (!veml.begin(&Wire)) {
    Serial.printf("VEML7700 FAIL  SDA=%d SCL=%d addr=0x%02X\n", PIN_VEML_SDA,
                  PIN_VEML_SCL, VEML7700_I2CADDR_DEFAULT);
    return false;
  }

  // Indoor range: gain 1x, 100 ms integration.
  veml.setGain(VEML7700_GAIN_1);
  veml.setIntegrationTime(VEML7700_IT_100MS);

  ready = true;
  Serial.printf("VEML7700 OK  SDA=%d SCL=%d addr=0x%02X\n", PIN_VEML_SDA,
                PIN_VEML_SCL, VEML7700_I2CADDR_DEFAULT);
  return true;
}

bool lightSensorOk(void) { return ready; }

bool lightSensorReadLux(float *lux) {
  if (!ready) {
    return false;
  }
  // NOWAIT: continuous ALS at fixed gain/IT from begin(). VEML_LUX_AUTO
  // blocks the UI for 100–800+ ms per sample (freeze ~every kLuxBlMs).
  const float v = veml.readLux(VEML_LUX_NORMAL_NOWAIT);
  if (isnan(v) || v < 0.0f) {
    return false;
  }
  if (lux) {
    *lux = v;
  }
  return true;
}

void lightSensorPrint(void) {
  float lux = 0;
  if (!lightSensorReadLux(&lux)) {
    Serial.println("VEML7700 read failed");
    return;
  }
  Serial.printf("VEML7700  lux=%.1f\n", lux);
}
