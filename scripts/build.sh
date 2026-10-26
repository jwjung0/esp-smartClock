#!/usr/bin/env bash
# Compile only (no upload).
# Usage: ./scripts/build.sh [checkup|app]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE="${1:-checkup}"

case "$MODE" in
  checkup|app) ;;
  *)
    echo "Usage: $0 [checkup|app]" >&2
    echo "  checkup  board LED + serial check (default)" >&2
    echo "  app      clock application firmware" >&2
    exit 1
    ;;
esac

echo "==> Mode: $MODE"
echo "==> Compiling..."
if ! pio run -e "$MODE"; then
  echo "==> Compile failed." >&2
  exit 1
fi

echo "==> Build OK."
