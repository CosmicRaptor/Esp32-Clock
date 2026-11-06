// Bhoomi's clock — Waveshare ESP32-S3-Touch-LCD-2.8
// Step 1: Wi-Fi setup via captive portal, with on-screen instructions (LVGL),
//         plus a quick NTP check to prove we're online.
//
// Build & flash: ./build.sh            (see build.sh for board settings)

#include <WiFi.h>
#include <time.h>

#include "display.h"
#include "ui.h"
#include "wifi_setup.h"

static const char *AP_NAME = "Bhoomi-Clock";  // the open network the phone joins for setup

static bool ntpStarted = false;

static void onWifi(const wifisetup::Status &s) {
  ui::showWifi(s);
  if (s.phase == wifisetup::Phase::Online && !ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpStarted = true;
    Serial.println("[ntp] sync started");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Bhoomi clock booting ===");

  display::begin();
  ui::begin(AP_NAME);
  display::loop();  // draw "waking up" before the (slow) initial Wi-Fi scan

  wifisetup::begin(AP_NAME, onWifi);
}

void loop() {
  wifisetup::loop();
  ui::loop();
  display::loop();

  static uint32_t lastLog = 0;
  if (ntpStarted && millis() - lastLog > 30000) {
    lastLog = millis();
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &t);
    Serial.printf("[ntp] %s UTC%s\n", buf, now > 1700000000 ? "" : " (not synced yet)");
  }

  delay(2);
}
