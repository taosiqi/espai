#!/bin/zsh
set -e

label="com.taosiqi.espai-mac-service"
plist="$HOME/Library/LaunchAgents/$label.plist"

launchctl bootout "gui/$(id -u)" "$plist" >/dev/null 2>&1 || true
rm -f "$plist"

echo "espai Mac service background agent was removed."
echo "Logs, if any, are still in ~/.codex/espai-mac-service.*.log"
read "?Press Enter to close..."
