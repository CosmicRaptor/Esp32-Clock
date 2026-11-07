#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
LVGL="${LVGL:-$HOME/Documents/Arduino/libraries/lvgl}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
c++ -std=c++17 -O1 -Wall -DLV_CONF_SKIP -I"$LVGL" ../weather_icons.cpp check_weather_icons.cpp -o "$tmp/check"
"$tmp/check"
