#include "board_check.h"

#if defined(FIRMWARE_MODE_CHECKUP)

#include "bme_sensor.h"
#include "board_pins.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SPI.h>

static const int LED_PIN = BOARD_LED_PIN;
static const unsigned long STEP_MS = 1200;
static const uint16_t kScreenW = 240;
static const uint16_t kScreenH = 240;
static const uint32_t kSpiHz = 8000000;

static Adafruit_ST7789 tft =
    Adafruit_ST7789(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

static const uint16_t kColors[] = {
    ST77XX_RED,    ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE,
    ST77XX_YELLOW, ST77XX_CYAN,  ST77XX_BLACK};
static const char *kColorNames[] = {"RED",  "GREEN", "BLUE", "WHITE",
                                    "YELLOW", "CYAN",  "BLACK"};
static const size_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);
static size_t colorIndex = 0;
static bool lcdReady = false;

static void printLcdPins() {
  Serial.println("LCD expected wiring (SPI — panel SDA=MOSI, SCL=SCLK):");
  Serial.printf("  VCC=3V3  GND=GND\n");
  Serial.printf("  MOSI/SDA=%d  SCLK/SCL=%d  CS=%d  DC=%d  RST=%d  BL=%d\n",
                PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST,
                PIN_TFT_BL);
}

static void printBmePins() {
  Serial.println("BMx280 expected wiring (I2C):");
  Serial.printf("  VCC=3V3  GND=GND  SDA=%d  SCL=%d\n", PIN_BME_SDA,
                PIN_BME_SCL);
  Serial.println("  CSB/SDO: leave board default (usually unconnected)");
}

static void bmeCheckOnce() {
  if (!bmeSensorOk()) {
    Serial.println("BMx280 not ready — check VCC/GND/SDA/SCL only");
    return;
  }
  bmeSensorPrint();
}

static void blinkBacklight() {
  Serial.println("BL blink test — backlight should pulse 6 times");
  pinMode(PIN_TFT_BL, OUTPUT);
  for (int i = 0; i < 6; i++) {
    digitalWrite(PIN_TFT_BL, LOW);
    Serial.printf("  BL OFF %d/6\n", i + 1);
    delay(350);
    digitalWrite(PIN_TFT_BL, HIGH);
    Serial.printf("  BL ON  %d/6\n", i + 1);
    delay(350);
  }
  digitalWrite(PIN_TFT_BL, HIGH);
  Serial.println("If BL did not blink, move LCD BL wire to GPIO22 (D22), not 3V3");
}

static void lcdShowColor(size_t idx) {
  if (!lcdReady)
    return;
  const uint16_t color = kColors[idx];
  const char *name = kColorNames[idx];
  const uint16_t text =
      (color == ST77XX_BLACK || color == ST77XX_BLUE) ? ST77XX_WHITE
                                                      : ST77XX_BLACK;

  tft.fillScreen(color);
  tft.setTextColor(text);
  tft.setTextSize(3);
  tft.setCursor(40, 100);
  tft.print(name);
  tft.drawRect(0, 0, kScreenW, kScreenH, ST77XX_WHITE);
  Serial.printf("LCD fill %s\n", name);
}

static bool lcdBegin() {
  Serial.println("LCD init (Adafruit ST7789 @ 8 MHz, invert on)...");
  SPI.begin(PIN_TFT_SCLK, -1 /* MISO */, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(kScreenW, kScreenH);
  tft.setSPISpeed(kSpiHz);
  tft.setRotation(0);
  tft.invertDisplay(true);
  digitalWrite(PIN_TFT_BL, HIGH);
  lcdShowColor(0);
  Serial.println("LCD init done — expect color names on glass every ~1.2s");
  return true;
}

void boardCheckSetup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.printf("smartClock board check OK (LED GPIO %d)\n", LED_PIN);

  // Probe BME before LCD SPI so the I2C scan is clean.
  Serial.println();
  printBmePins();
  Serial.println("BMx280 probe...");
  if (bmeSensorBegin()) {
    Serial.println("BMx280 test OK — expect readings every ~1.2s");
    bmeCheckOnce();
  } else {
    Serial.println("BMx280 test FAIL — see I2C scan above");
  }

  Serial.println();
  printLcdPins();
  blinkBacklight();
  lcdReady = lcdBegin();
  Serial.println();
}

void boardCheckLoop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.printf("LED ON, uptime=%lu ms\n", millis());

  colorIndex = (colorIndex + 1) % kColorCount;
  lcdShowColor(colorIndex);
  bmeCheckOnce();
  delay(STEP_MS);

  digitalWrite(LED_PIN, LOW);
  Serial.printf("LED OFF, uptime=%lu ms\n", millis());
  delay(STEP_MS / 2);
}

#else

void boardCheckSetup() {}
void boardCheckLoop() {}

#endif
