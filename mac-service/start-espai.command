#!/bin/zsh
set -e

cd "$(dirname "$0")"

if [ -f ".env" ]; then
  set -a
  source ".env"
  set +a
elif [ -f ".env.example" ]; then
  cp ".env.example" ".env"
  set -a
  source ".env"
  set +a
fi

if [ ! -x "./dist/espai-mac-service" ]; then
  echo "Missing ./dist/espai-mac-service"
  echo "Run: bun run build"
  read "?Press Enter to close..."
  exit 1
fi

echo "Starting espai Mac service..."
echo "Keep this window open while the ESP32 is using quota data."
echo "If you do not want this window, double-click install-background-service.command once."
echo

"./dist/espai-mac-service"
