#pragma once
#include "wifi_setup.h"

// Screens for the Wi-Fi setup flow (and, for now, a simple "online" time readout).
namespace ui {

void begin(const char *apName);
void showWifi(const wifisetup::Status &s);
void loop();  // refreshes the clock text once a second, rings alarms

// Rings an alarm right now (shown on screen, sound from /sounds/<soundFile> or the beep if empty).
// Triggered by typing `ring` or `ring <file>` in the serial monitor.
void testAlarm(const char *soundFile);

}  // namespace ui
