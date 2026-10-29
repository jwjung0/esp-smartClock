#include "bme_sensor.h"

#include "board_pins.h"

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <Wire.h>

static Adafruit_BME280 bme;
static bool ready = false;
static uint8_t addr = BME280_I2C_ADDR;

bool bmeSensorBegin(void) {
  Wire.begin(PIN_BME_SDA, PIN_BME_SCL);

  const uint8_t candidates[] = {BME280_I2C_ADDR, 0x76, 0x77};
  ready = false;

  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    const uint8_t tryAddr = candidates[i];
    if (i > 0 && tryAddr == candidates[0]) {
      continue;
    }
    if (bme.begin(tryAddr, &Wire)) {
      addr = tryAddr;
      ready = true;
      Serial.printf("BME280 OK  SDA=%d SCL=%d addr=0x%02X\n", PIN_BME_SDA,
                    PIN_BME_SCL, addr);
      return true;
    }
  }

  Serial.printf("BME280 FAIL  SDA=%d SCL=%d (tried 0x%02X / 0x76 / 0x77)\n",
                PIN_BME_SDA, PIN_BME_SCL, BME280_I2C_ADDR);
  return false;
}

bool bmeSensorOk(void) { return ready; }

bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa) {
  if (!ready) {
    return false;
  }

  const float t = bme.readTemperature();
  const float h = bme.readHumidity();
  const float p = bme.readPressure() / 100.0f;

  if (isnan(t) || isnan(h) || isnan(p)) {
    return false;
  }

  if (tempC) {
    *tempC = t;
  }
  if (humidityPct) {
    *humidityPct = h;
  }
  if (pressureHpa) {
    *pressureHpa = p;
  }
  return true;
}

void bmeSensorPrint(void) {
  float t = 0, h = 0, p = 0;
  if (!bmeSensorRead(&t, &h, &p)) {
    Serial.println("BME280 read failed");
    return;
  }
  Serial.printf("BME280  T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", t, h, p);
}
