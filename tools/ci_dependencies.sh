#!/usr/bin/env bash
# Reproducible firmware dependencies matching the local working toolchain.
set -euo pipefail
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
libs="$HOME/Arduino/libraries"
# The workflow restores only an exact OS/architecture/CLI/script-keyed cache.
# Configuration and environment exports must still run on a cache hit.
if [[ "${CI_DEPENDENCIES_CACHE_HIT:-false}" != "true" ]]; then
  arduino-cli core update-index
  arduino-cli core install esp32:esp32@3.3.12
  arduino-cli lib install lvgl@9.2.2 ESP32-audioI2S-master@3.4.7
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl --fail --silent --show-error --location --max-time 120 \
    https://github.com/nanopb/nanopb/archive/refs/tags/0.4.9.1.tar.gz -o "$tmp/nanopb.tar.gz"
  tar -xzf "$tmp/nanopb.tar.gz" -C "$tmp"
  mkdir -p "$libs/Nanopb"
  cp "$tmp/nanopb-0.4.9.1/"pb*.h "$tmp/nanopb-0.4.9.1/"pb*.c "$libs/Nanopb/"
  cat > "$libs/Nanopb/library.properties" <<'PROPERTIES'
name=Nanopb
version=0.4.9
author=Petteri Aimonen
maintainer=Petteri Aimonen
sentence=Nanopb 0.4.9.1 runtime
paragraph=Matches the committed generated protobuf sources.
category=Data Processing
url=https://github.com/nanopb/nanopb
architectures=*
PROPERTIES
fi
echo "ARDUINO_CLI=$(command -v arduino-cli)" >> "$GITHUB_ENV"
echo "ARDUINO_LIBRARIES=$libs" >> "$GITHUB_ENV"
echo "NANOPB=$libs/Nanopb" >> "$GITHUB_ENV"
echo "LVGL=$libs/lvgl" >> "$GITHUB_ENV"
