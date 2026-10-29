// LOLIN / WEMOS S2 Mini (ESP32-S2) wiring.
// Outer header silk matches D1 Mini: D0–D8, RST, A0, TX, RX, 3V3, GND.
//
//   D0=GPIO5   D1=GPIO35 (SCL)   D2=GPIO33 (SDA)   D3=GPIO18   D4=GPIO16
//   D5=GPIO7 (SCK)   D6=GPIO9 (MISO)   D7=GPIO11 (MOSI)   D8=GPIO13
//   A0=GPIO3   TX=GPIO39   RX=GPIO37   RST=EN   3V3 / GND = power
// Inner pins are labeled by GPIO number (1, 2, 4, …).
#pragma once

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN 15  // onboard LED (not a D-pin)
#endif

// ST7789 1.54" 240x240 — SPI (panel SDA/SCL = MOSI/SCLK).
#define PIN_TFT_MOSI 11  // D7
#define PIN_TFT_SCLK 7   // D5
#define PIN_TFT_CS   13  // D8
#define PIN_TFT_DC   5   // D0
#define PIN_TFT_RST  3   // A0
#define PIN_TFT_BL   16  // D4

// BME280 — I2C (board default Wire pins).
#define PIN_BME_SDA 33  // D2
#define PIN_BME_SCL 35  // D1
#define BME280_I2C_ADDR 0x76
