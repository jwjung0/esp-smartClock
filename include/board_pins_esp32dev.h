// Classic ESP32 DevKit wiring (silk often Dxx / GPIO number).
#pragma once

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN 2  // D2 — onboard LED on many DevKits
#endif

// ST7789 2.4" 240x320 — SPI (panel SDA/SCL = MOSI/SCLK).
#define PIN_TFT_MOSI 21  // D21
#define PIN_TFT_SCLK 18  // D18 / VSPI SCK
#define PIN_TFT_CS   5   // D5  / VSPI SS  (if module has no CS pin, see README)
// DC/RST: use GPIO16/17 (RX2/TX2 silk). Do NOT use 25–27 — ADC2 conflicts with Wi‑Fi.
#define PIN_TFT_DC   16  // GPIO16 / RX2
#define PIN_TFT_RST  17  // GPIO17 / TX2
#define PIN_TFT_BL   19  // D19

// Climate + VEML7700 — shared I2C (GPIO 34–39 are input-only — not usable for I2C).
#define PIN_BME_SDA 23  // D23
#define PIN_BME_SCL 22  // D22
#define BME280_I2C_ADDR 0x76

// VEML7700 ambient light — same bus as climate (Wire).
#define PIN_VEML_SDA PIN_BME_SDA
#define PIN_VEML_SCL PIN_BME_SCL

// TTP223 capacitive touch — OUT active-high (AHL jumper open).
#define PIN_TOUCH_OUT 4
