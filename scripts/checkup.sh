#!/usr/bin/env bash
# Flash board-check firmware, then open serial monitor to verify LED + heartbeat.
# Usage: ./scripts/checkup.sh [esp32dev|s2mini]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BOARD="${1:-esp32dev}"
ENV="checkup_${BOARD}"

"$ROOT/scripts/flash.sh" checkup "$BOARD"

echo "==> Opening serial monitor (Ctrl+C to quit)..."
echo "    1) Backlight should pulse 6 times at boot (BL on GPIO22)."
echo "    2) LCD should cycle RED/GREEN/BLUE/WHITE/YELLOW/CYAN/BLACK."
echo "       esp32dev: DC->RX2 (GPIO16), RST->TX2 (GPIO17) — not D26/D27."
echo "    3) BMx280: I2C scan, then BMP280/BME280 OK + readings (SDA=D21 SCL=D19)."
echo "       Leave CSB/SDO at board default (unconnected)."
echo "    4) OpenWeather: joins saved WiFi (NVS from app portal), GET 2.5/weather,"
echo "       prints T/RH/main on Serial + LCD (~4s), then WiFi off. Needs"
echo "       include/weather_config.h API key + prior app WiFi provisioning."
pio device monitor -e "$ENV"
