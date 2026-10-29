#include "bme_sensor.h"

#include "board_pins.h"

#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <Wire.h>

static Adafruit_BME280 bme;
// BMP280 takes TwoWire in the ctor (begin(addr) only — unlike BME280).
static Adafruit_BMP280 bmp(&Wire);
static bool ready = false;
static bool isBmp280 = false;
static uint8_t addr = BME280_I2C_ADDR;

static uint8_t readChipId(uint8_t i2cAddr) {
  Wire.beginTransmission(i2cAddr);
  Wire.write(0xD0); // chip-id register
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }
  if (Wire.requestFrom((int)i2cAddr, 1) != 1) {
    return 0;
  }
  return (uint8_t)Wire.read();
}

void bmeSensorScanBus(void) {
  Serial.printf("I2C scan  SDA=%d SCL=%d @ 100 kHz\n", PIN_BME_SDA,
                PIN_BME_SCL);
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    const uint8_t err = Wire.endTransmission();
    if (err != 0) {
      continue;
    }
    found++;
    const uint8_t id = readChipId(a);
    Serial.printf("  device 0x%02X", a);
    if (id == 0x60) {
      Serial.print("  chip-id=0x60 (BME280)");
    } else if (id == 0x58) {
      Serial.print("  chip-id=0x58 (BMP280, T+P only)");
    } else if (id != 0) {
      Serial.printf("  chip-id=0x%02X", id);
    }
    Serial.println();
  }
  if (found == 0) {
    Serial.println(
        "  (no I2C devices — check VCC/GND/SDA/SCL; CSB/SDO can stay default)");
  }
}

bool bmeSensorBegin(void) {
  Wire.begin(PIN_BME_SDA, PIN_BME_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  delay(50);

  bmeSensorScanBus();

  const uint8_t candidates[] = {BME280_I2C_ADDR, 0x76, 0x77};
  ready = false;
  isBmp280 = false;

  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    const uint8_t tryAddr = candidates[i];
    if (i > 0 && tryAddr == candidates[0]) {
      continue;
    }

    const uint8_t id = readChipId(tryAddr);
    if (id == 0x58) {
      if (bmp.begin(tryAddr)) {
        addr = tryAddr;
        isBmp280 = true;
        ready = true;
        Serial.printf("BMP280 OK  SDA=%d SCL=%d addr=0x%02X (no humidity)\n",
                      PIN_BME_SDA, PIN_BME_SCL, addr);
        return true;
      }
      continue;
    }

    if (bme.begin(tryAddr, &Wire)) {
      addr = tryAddr;
      isBmp280 = false;
      ready = true;
      Serial.printf("BME280 OK  SDA=%d SCL=%d addr=0x%02X\n", PIN_BME_SDA,
                    PIN_BME_SCL, addr);
      return true;
    }
  }

  Serial.printf("BMx280 FAIL  SDA=%d SCL=%d (tried 0x%02X / 0x76 / 0x77)\n",
                PIN_BME_SDA, PIN_BME_SCL, BME280_I2C_ADDR);
  return false;
}

bool bmeSensorOk(void) { return ready; }

bool bmeSensorHasHumidity(void) { return ready && !isBmp280; }

bool bmeSensorRead(float *tempC, float *humidityPct, float *pressureHpa) {
  if (!ready) {
    return false;
  }

  float t = NAN;
  float h = NAN;
  float p = NAN;

  if (isBmp280) {
    t = bmp.readTemperature();
    p = bmp.readPressure() / 100.0f;
  } else {
    t = bme.readTemperature();
    h = bme.readHumidity();
    p = bme.readPressure() / 100.0f;
  }

  if (isnan(t) || isnan(p) || (!isBmp280 && isnan(h))) {
    return false;
  }

  if (tempC) {
    *tempC = t;
  }
  if (humidityPct) {
    *humidityPct = h; // NAN on BMP280
  }
  if (pressureHpa) {
    *pressureHpa = p;
  }
  return true;
}

void bmeSensorPrint(void) {
  float t = 0, h = 0, p = 0;
  if (!bmeSensorRead(&t, &h, &p)) {
    Serial.println("BMx280 read failed");
    return;
  }
  if (isBmp280) {
    Serial.printf("BMP280  T=%.2f C  P=%.1f hPa  (no RH)\n", t, p);
  } else {
    Serial.printf("BME280  T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", t, h, p);
  }
}
