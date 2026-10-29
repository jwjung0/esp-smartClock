#!/usr/bin/env bash
# Compile only (no upload).
# Usage (app, default): ./scripts/build.sh [esp32dev|s2mini] [local|prod]
# Usage (checkup):      ./scripts/build.sh checkup [esp32dev|s2mini]
# Default: esp32dev prod  →  app_esp32dev_prod
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

usage() {
  echo "Usage: $0 [esp32dev|s2mini] [local|prod]" >&2
  echo "       $0 checkup [esp32dev|s2mini]" >&2
  echo "Default: esp32dev prod (app). Do not pass 'app'." >&2
}

if [[ "${1:-}" == "app" ]]; then
  echo "Error: 'app' is the default — omit it." >&2
  usage
  exit 1
fi

if [[ "${1:-}" == "checkup" ]]; then
  MODE="checkup"
  BOARD="${2:-esp32dev}"
  DEPLOY=""
  case "$BOARD" in
    esp32dev|s2mini) ;;
    *)
      usage
      exit 1
      ;;
  esac
  ENV="checkup_${BOARD}"
  echo "==> Mode: checkup  Board: $BOARD  Env: $ENV"
else
  MODE="app"
  BOARD="${1:-esp32dev}"
  DEPLOY="${2:-prod}"
  case "$BOARD" in
    esp32dev|s2mini) ;;
    *)
      usage
      exit 1
      ;;
  esac
  case "$DEPLOY" in
    local|prod) ;;
    *)
      usage
      exit 1
      ;;
  esac
  ENV="app_${BOARD}_${DEPLOY}"
  echo "==> Mode: app  Board: $BOARD  Deploy: $DEPLOY  Env: $ENV"
fi

echo "==> Compiling..."
if ! pio run -e "$ENV"; then
  echo "==> Compile failed." >&2
  exit 1
fi

echo "==> Build OK."
