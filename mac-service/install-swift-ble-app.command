#!/bin/zsh
set -euo pipefail

cd "$(dirname "$0")"

app_name="EspaiBleBridge"
app_path="$PWD/$app_name.app"
contents="$app_path/Contents"
macos="$contents/MacOS"
source="$PWD/swift-ble-bridge/EspaiBleBridge.swift"
build_dir="$PWD/.build-swift"
binary_tmp="$build_dir/$app_name"
binary="$macos/$app_name"

mkdir -p "$build_dir" "$macos"

swiftc "$source" \
  -module-name "$app_name" \
  -framework Foundation \
  -framework CoreBluetooth \
  -o "$binary_tmp"

/usr/bin/killall "$app_name" >/dev/null 2>&1 || true
cp "$binary_tmp" "$binary"
chmod +x "$binary"

cat > "$contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>EspaiBleBridge</string>
  <key>CFBundleIdentifier</key>
  <string>com.taosiqi.espai.blebridge</string>
  <key>CFBundleName</key>
  <string>EspaiBleBridge</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>1</string>
  <key>LSBackgroundOnly</key>
  <true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>espai uses Bluetooth to send Codex quota data to the ESP32-S3 display.</string>
  <key>NSBluetoothPeripheralUsageDescription</key>
  <string>espai uses Bluetooth to send Codex quota data to the ESP32-S3 display.</string>
</dict>
</plist>
PLIST

osascript <<OSA
tell application "System Events"
  if exists login item "$app_name" then
    delete login item "$app_name"
  end if
  make login item at end with properties {path:"$app_path", hidden:true, name:"$app_name"}
end tell
OSA

open -n "$app_path"

echo "EspaiBleBridge.app installed and started without a Terminal window."
echo "Log file:"
echo "  $HOME/.codex/espai-ble-bridge-app.log"
if [ -t 0 ]; then
  read "?Press Enter to close..."
fi
