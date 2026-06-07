#!/bin/zsh
set -e

cd "$(dirname "$0")"

label="com.taosiqi.espai-mac-service"
plist="$HOME/Library/LaunchAgents/$label.plist"
log_dir="$HOME/.codex"
service_path="$PWD/dist/espai-mac-service"
env_file="$PWD/.env"

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

if [ ! -x "$service_path" ]; then
  echo "Missing $service_path"
  echo "Run: bun run build"
  read "?Press Enter to close..."
  exit 1
fi

mkdir -p "$HOME/Library/LaunchAgents" "$log_dir"

python3 - "$plist" "$label" "$service_path" "$PWD" "$log_dir" "${ESPAI_HOST:-0.0.0.0}" "${ESPAI_PORT:-8787}" "${CODEX_BIN:-/Applications/Codex.app/Contents/Resources/codex}" <<'PY'
import plistlib
import sys
from pathlib import Path

plist_path, label, service_path, workdir, log_dir, host, port, codex_bin = sys.argv[1:]
payload = {
    "Label": label,
    "ProgramArguments": [service_path],
    "WorkingDirectory": workdir,
    "RunAtLoad": True,
    "KeepAlive": True,
    "EnvironmentVariables": {
        "ESPAI_HOST": host,
        "ESPAI_PORT": port,
        "CODEX_BIN": codex_bin,
    },
    "StandardOutPath": str(Path(log_dir) / "espai-mac-service.out.log"),
    "StandardErrorPath": str(Path(log_dir) / "espai-mac-service.err.log"),
}
with open(plist_path, "wb") as f:
    plistlib.dump(payload, f)
PY

launchctl bootout "gui/$(id -u)" "$plist" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$(id -u)" "$plist"
launchctl enable "gui/$(id -u)/$label"
launchctl kickstart -k "gui/$(id -u)/$label"

echo "espai Mac service is running in the background."
echo "URL: http://${ESPAI_HOST:-0.0.0.0}:${ESPAI_PORT:-8787}/api/status"
echo "Logs:"
echo "  $log_dir/espai-mac-service.out.log"
echo "  $log_dir/espai-mac-service.err.log"
echo
echo "You can close this window."
read "?Press Enter to close..."
