// Board pin map selected at build time via -DBOARD_ESP32DEV / -DBOARD_S2_MINI.
#pragma once

#if defined(BOARD_S2_MINI)
#include "board_pins_s2mini.h"
#elif defined(BOARD_ESP32DEV)
#include "board_pins_esp32dev.h"
#else
#error "Select a board env (e.g. -e app_s2mini or -e app_esp32dev)"
#endif
