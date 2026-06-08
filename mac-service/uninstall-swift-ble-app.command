#!/bin/zsh
set -euo pipefail

cd "$(dirname "$0")"

app_name="EspaiBleBridge"
app_path="$PWD/$app_name.app"

osascript <<OSA >/dev/null 2>&1 || true
tell application "System Events"
  if exists login item "$app_name" then
    delete login item "$app_name"
  end if
end tell
OSA

pkill -f "$app_path/Contents/MacOS/$app_name" >/dev/null 2>&1 || true
rm -rf "$app_path"

echo "EspaiBleBridge.app was stopped and removed."
echo "Log file, if any, is still at ~/.codex/espai-ble-bridge-app.log"
if [ -t 0 ]; then
  read "?Press Enter to close..."
fi
