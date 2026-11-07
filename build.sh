#!/bin/bash
# Build (and optionally flash) the clock firmware.
#   ./build.sh            compile only
#   ./build.sh flash      compile + upload to the first /dev/cu.usbmodem* port
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$ROOT/firmware/BhoomiClock"
cd "$ROOT"

CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom"
LIBS="$HOME/Documents/Arduino/libraries"

# LVGL looks for lv_conf.h next to its library folder. Avoid touching the
# installed copy when it already matches the repository version.
if ! cmp -s "$SKETCH/lv_conf.h" "$LIBS/lv_conf.h"; then
  cp "$SKETCH/lv_conf.h" "$LIBS/lv_conf.h"
fi

mkdir -p build
"$CLI" compile -b "$FQBN" --build-property upload.maximum_size=6291456 --output-dir "$ROOT/build" "$SKETCH"

if [[ "${1:-}" == "flash" ]]; then
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
  [[ -n "$PORT" ]] || { echo "board not found (no /dev/cu.usbmodem*)"; exit 1; }
  "$CLI" upload -b "$FQBN" -p "$PORT" --input-dir "$ROOT/build" "$SKETCH"
fi
