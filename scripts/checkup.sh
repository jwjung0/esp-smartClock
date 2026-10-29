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
echo "    Expect LED ON/OFF every 1s and matching serial lines."
pio device monitor -e "$ENV"
