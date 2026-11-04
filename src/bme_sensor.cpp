#include "bme_sensor.h"

#include "board_pins.h"

#include <Adafruit_AHTX0.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <Wire.h>

static Adafruit_BME280 bme;
// BMP280 takes TwoWire in the ctor (begin(addr) only — unlike BME280).
static Adafruit_BMP280 bmp(&Wire);
static Adafruit_AHTX0 aht;
static bool ready = false;
static bool isBmp280 = false;
static bool isBme280 = false;
static bool hasAht20 = false;
static uint8_t addr = BME280_I2C_ADDR;
static uint8_t ahtAddr = AHTX0_I2CADDR_DEFAULT;

// Non-blocking poll: AHT trigger → wait ≥80 ms without delay() in loop.
enum class PollPhase : uint8_t { Idle, AhtWait };
static PollPhase pollPhase = PollPhase::Idle;
static uint32_t ahtTriggerMs = 0;
static float stageT = NAN;
static float stageH = NAN;
static float stageP = NAN;

static uint8_t readChipId(uint8_t i2cAddr) {
  Wire.beginTransmission(i2cAddr);
  Wire.write(0xD0); // chip-id register (BMx280)
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }
  if (Wire.requestFrom((int)i2cAddr, 1) != 1) {
    return 0;
  }
  return (uint8_t)Wire.read();
}

static bool tryBeginAht20(void) {
  const uint8_t candidates[] = {0x38, 0x39};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    if (aht.begin(&Wire, 0, candidates[i])) {
      ahtAddr = candidates[i];
      Serial.printf("AHT20 OK   SDA=%d SCL=%d addr=0x%02X (humidity)\n",
                    PIN_BME_SDA, PIN_BME_SCL, ahtAddr);
      return true;
    }
  }
  return false;
}

static bool readBmxIntoStage(void) {
  stageT = NAN;
  stageH = NAN;
  stageP = NAN;
  if (isBmp280) {
    stageT = bmp.readTemperature();
    stageP = bmp.readPressure() / 100.0f;
    return !isnan(stageT) && !isnan(stageP);
  }
  if (isBme280) {
    stageT = bme.readTemperature();
    stageH = bme.readHumidity();
    stageP = bme.readPressure() / 100.0f;
    return !isnan(stageT) && !isnan(stageH) && !isnan(stageP);
  }
  return true; // AHT-only
}

static bool ahtTrigger(void) {
  Wire.beginTransmission(ahtAddr);
  Wire.write(AHTX0_CMD_TRIGGER);
  Wire.write(0x33);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}

static bool ahtFetchIntoStage(void) {
  uint8_t data[6];
  if (Wire.requestFrom((int)ahtAddr, 6) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    data[i] = (uint8_t)Wire.read();
  }
  uint32_t h = data[1];
  h <<= 8;
  h |= data[2];
  h <<= 4;
  h |= data[3] >> 4;
  stageH = ((float)h * 100.0f) / 0x100000;

  uint32_t tdata = data[3] & 0x0F;
  tdata <<= 8;
  tdata |= data[4];
  tdata <<= 8;
  tdata |= data[5];
  const float ahtT = ((float)tdata * 200.0f / 0x100000) - 50.0f;
  if (isnan(stageT)) {
    stageT = ahtT;
  }
  return !isnan(stageH) && !isnan(stageT);
}

static void copyStage(float *tempC, float *humidityPct, float *pressureHpa) {
  if (tempC) {
    *tempC = stageT;
  }
  if (humidityPct) {
    *humidityPct = stageH;
  }
  if (pressureHpa) {
    *pressureHpa = stageP;
  }
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
    Serial.printf("  device 0x%02X", a);
    if (a == 0x10) {
      Serial.print("  (VEML7700 lux)");
    } else if (a == 0x38 || a == 0x39) {
      Serial.print("  (AHT20 T+RH)");
    } else {
      const uint8_t id = readChipId(a);
      if (id == 0x60) {
        Serial.print("  chip-id=0x60 (BME280)");
      } else if (id == 0x58) {
        Serial.print("  chip-id=0x58 (BMP280, T+P)");
      } else if (id != 0) {
        Serial.printf("  chip-id=0x%02X", id);
      }
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
  isBme280 = false;
  hasAht20 = false;

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
        Serial.printf("BMP280 OK  SDA=%d SCL=%d addr=0x%02X (T+P)\n",
                      PIN_BME_SDA, PIN_BME_SCL, addr);
        break;
      }
      continue;
    }

    if (bme.begin(tryAddr, &Wire)) {
      addr = tryAddr;
      isBme280 = true;
      ready = true;
      Serial.printf("BME280 OK  SDA=%d SCL=%d addr=0x%02X\n", PIN_BME_SDA,
                    PIN_BME_SCL, addr);
      break;
    }
  }

  // Combo boards (BMP280 + AHT20) put humidity on a separate chip @ 0x38.
  hasAht20 = tryBeginAht20();
  if (!ready && hasAht20) {
    ready = true;
    Serial.println("climate: AHT20 only (no BMx280 pressure)");
  }

  if (!ready) {
    Serial.printf("BMx280 FAIL  SDA=%d SCL=%d (tried 0x%02X / 0x76 / 0x77)\n",
                  PIN_BME_SDA, PIN_BME_SCL, BME280_I2C_ADDR);
    return false;
  }
  return true;
}

bool bmeSensorOk(void) { return ready; }

bool bmeSensorHasHumidity(void) { return ready && (isBme280 || hasAht20); }

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
    if (isnan(t) || isnan(p)) {
      return false;
    }
  } else if (isBme280) {
    t = bme.readTemperature();
    h = bme.readHumidity();
    p = bme.readPressure() / 100.0f;
    if (isnan(t) || isnan(h) || isnan(p)) {
      return false;
    }
  }

  if (hasAht20) {
    sensors_event_t humidity;
    sensors_event_t temp;
    if (!aht.getEvent(&humidity, &temp)) {
      return false;
    }
    h = humidity.relative_humidity;
    if (isnan(t)) {
      t = temp.temperature; // AHT-only
    }
    if (isnan(h)) {
      return false;
    }
  }

  if (isnan(t)) {
    return false;
  }
  if (bmeSensorHasHumidity() && isnan(h)) {
    return false;
  }

  if (tempC) {
    *tempC = t;
  }
  if (humidityPct) {
    *humidityPct = h; // NAN when no humidity sensor
  }
  if (pressureHpa) {
    *pressureHpa = p; // NAN when AHT-only
  }
  return true;
}

void bmeSensorRequest(void) {
  if (!ready || pollPhase != PollPhase::Idle) {
    return;
  }
  if (!readBmxIntoStage()) {
    return;
  }
  if (!hasAht20) {
    // No wait — Take will return the BMx sample immediately.
    pollPhase = PollPhase::AhtWait;
    ahtTriggerMs = 0;
    return;
  }
  if (!ahtTrigger()) {
    return;
  }
  ahtTriggerMs = millis();
  pollPhase = PollPhase::AhtWait;
}

bool bmeSensorTake(float *tempC, float *humidityPct, float *pressureHpa) {
  if (!ready || pollPhase != PollPhase::AhtWait) {
    return false;
  }

  if (hasAht20) {
    // Datasheet ~75–80 ms; skip status polls until then.
    if ((uint32_t)(millis() - ahtTriggerMs) < 80) {
      return false;
    }
    if (aht.getStatus() & AHTX0_STATUS_BUSY) {
      return false;
    }
    if (!ahtFetchIntoStage()) {
      pollPhase = PollPhase::Idle;
      return false;
    }
  }

  if (isnan(stageT) || (bmeSensorHasHumidity() && isnan(stageH))) {
    pollPhase = PollPhase::Idle;
    return false;
  }

  copyStage(tempC, humidityPct, pressureHpa);
  pollPhase = PollPhase::Idle;
  return true;
}

void bmeSensorPrint(void) {
  float t = 0, h = 0, p = 0;
  if (!bmeSensorRead(&t, &h, &p)) {
    Serial.println("climate read failed");
    return;
  }
  if (isBmp280 && hasAht20) {
    Serial.printf("BMP280+AHT20  T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", t, h, p);
  } else if (hasAht20 && !isBmp280 && !isBme280) {
    Serial.printf("AHT20  T=%.2f C  RH=%.1f %%\n", t, h);
  } else if (isBmp280) {
    Serial.printf("BMP280  T=%.2f C  P=%.1f hPa  (no RH)\n", t, p);
  } else {
    Serial.printf("BME280  T=%.2f C  RH=%.1f %%  P=%.1f hPa\n", t, h, p);
  }
}
