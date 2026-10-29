// Classic ESP32 DevKit wiring (silk often Dxx / GPIO number).
#pragma once

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN 2  // D2 — onboard LED on many DevKits
#endif

// ST7789 1.54" 240x240 — SPI (panel SDA/SCL = MOSI/SCLK).
#define PIN_TFT_MOSI 23  // D23 / VSPI MOSI
#define PIN_TFT_SCLK 18  // D18 / VSPI SCK
#define PIN_TFT_CS   5   // D5  / VSPI SS  (if module has no CS pin, see README)
// DC/RST: use GPIO16/17 (RX2/TX2 silk). Do NOT use 25–27 — ADC2 conflicts with Wi‑Fi.
#define PIN_TFT_DC   16  // GPIO16 / RX2
#define PIN_TFT_RST  17  // GPIO17 / TX2
#define PIN_TFT_BL   22  // D22

// BME280 — I2C (avoid TFT; default Wire SCL is D22, used by BL).
#define PIN_BME_SDA 21  // D21
#define PIN_BME_SCL 19  // D19
#define BME280_I2C_ADDR 0x76
