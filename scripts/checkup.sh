#!/usr/bin/env bash
# Flash board-check firmware, then open serial monitor to verify LED + heartbeat.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

"$ROOT/scripts/flash.sh" checkup

echo "==> Opening serial monitor (Ctrl+C to quit)..."
echo "    Expect LED ON/OFF every 1s and matching serial lines."
pio device monitor -e checkup
