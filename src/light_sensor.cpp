#include "light_sensor.h"

#include "board_pins.h"

#include <Adafruit_VEML7700.h>
#include <Arduino.h>
#include <Wire.h>

static Adafruit_VEML7700 veml;
static bool ready = false;

#if defined(BOARD_ESP32DEV)
static TwoWire &lightWire = Wire1;
static const bool kOwnBus = true;
#else
static TwoWire &lightWire = Wire;
static const bool kOwnBus = false;
#endif

static void scanLightBus(void) {
  Serial.printf("I2C scan (VEML)  SDA=%d SCL=%d @ 100 kHz\n", PIN_VEML_SDA,
                PIN_VEML_SCL);
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    lightWire.beginTransmission(a);
    if (lightWire.endTransmission() != 0) {
      continue;
    }
    found++;
    Serial.printf("  device 0x%02X", a);
    if (a == VEML7700_I2CADDR_DEFAULT) {
      Serial.print("  (VEML7700 lux)");
    }
    Serial.println();
  }
  if (found == 0) {
    Serial.println("  (no I2C devices on VEML bus)");
  }
}

bool lightSensorBegin(void) {
  ready = false;

  if (kOwnBus) {
    lightWire.begin(PIN_VEML_SDA, PIN_VEML_SCL);
    lightWire.setClock(100000);
    lightWire.setTimeOut(50);
    delay(20);
    scanLightBus();
  }

  if (!veml.begin(&lightWire)) {
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
