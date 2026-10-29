// TFT_eSPI board setup (picked up via #include <tft_setup.h>).
// Pins come from board_pins_*.h via the active PlatformIO env.
#pragma once

#include "board_pins.h"

#if defined(BOARD_S2_MINI)
#define USER_SETUP_INFO "esp-smartClock-s2-mini"
#elif defined(BOARD_ESP32DEV)
#define USER_SETUP_INFO "esp-smartClock-esp32dev"
#endif

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MOSI PIN_TFT_MOSI
#define TFT_SCLK PIN_TFT_SCLK
#define TFT_CS   PIN_TFT_CS
#define TFT_DC   PIN_TFT_DC
#define TFT_RST  PIN_TFT_RST
#define TFT_BL   PIN_TFT_BL

#define LOAD_GLCD
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
