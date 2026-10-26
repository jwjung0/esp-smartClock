// TFT_eSPI board setup (picked up via #include <tft_setup.h>).
// 1.54" 240x240 — adjust pins to match your wiring.
#pragma once

#define USER_SETUP_INFO "esp-smartClock"

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17
#define TFT_BL   22

#define LOAD_GLCD
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
