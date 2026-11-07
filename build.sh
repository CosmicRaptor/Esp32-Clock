#!/bin/bash
# Build (and optionally flash) the clock firmware.
#   ./build.sh            compile only
#   ./build.sh flash      compile + upload to the first /dev/cu.usbmodem* port
set -euo pipefail
cd "$(dirname "$0")"

CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom"
LIBS="$HOME/Documents/Arduino/libraries"

# LVGL looks for lv_conf.h next to its library folder.
cp lv_conf.h "$LIBS/lv_conf.h"

mkdir -p build
"$CLI" compile -b "$FQBN" --build-property upload.maximum_size=6291456 --output-dir build .

if [[ "${1:-}" == "flash" ]]; then
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
  [[ -n "$PORT" ]] || { echo "board not found (no /dev/cu.usbmodem*)"; exit 1; }
  "$CLI" upload -b "$FQBN" -p "$PORT" --input-dir build .
fi
