// Bhoomi's clock — Waveshare ESP32-S3-Touch-LCD-2.8 (landscape, in the stand)
//
// - Wi-Fi setup via captive portal, with on-screen instructions + join QR code
// - Time zone picked on the setup page, changeable later at http://bhoomi-clock.local
// - NTP time, shown in local time
// - Photos and sounds uploaded to the microSD card from the same page
// - Cloud admin portal (portal/): message, photo and alarms, polled over HTTPS as protobuf
// - Alarms ring from /sounds on the card (or a built-in beep) until you touch the screen
//
// Build & flash: ./build.sh flash

#include <WiFi.h>
#include <time.h>

#include "app_lock.h"
#include "display.h"
#include "portal_client.h"
#include "settings.h"
#include "sound.h"
#include "storage.h"
#include "touch.h"
#include "ui.h"
#include "web.h"
#include "wifi_setup.h"

static const char *AP_NAME = "Bhoomi-Clock";  // the open network the phone joins for setup

static bool ntpStarted = false;

static void onWifi(const wifisetup::Status &s) {
  ui::showWifi(s);
  if (s.phase == wifisetup::Phase::Online) {
    web::onOnline();
    if (!ntpStarted) {
      // configTzTime keeps our TZ rule (plain configTime would overwrite it with a fixed offset).
      configTzTime(settings::posixTz(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      ntpStarted = true;
      Serial.println("[ntp] sync started");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Bhoomi clock booting ===");

  app::Lock lock;
  settings::begin();
  display::begin();
  ui::begin(AP_NAME);
  touch::begin();  // LVGL pointer device; the alarm is stopped by touch
  display::loop();  // draw "waking up" before the (slow) initial Wi-Fi scan

  storage::begin();
  wifisetup::begin(AP_NAME, onWifi);
  web::begin();  // after Wi-Fi: opening a socket before the TCP/IP stack is up crashes
  sound::begin();  // alarm sounds: the decoder task + the built-in beep (internal flash)
  portal::begin();  // own task; idles until Wi-Fi is online (and, for https, NTP has synced)
}

// Serial monitor shortcuts for trying the alarm without waiting for the clock: `ring` or `ring <file>`.
static void serialCommands() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (line.length() < 100) line += c;
      continue;
    }
    line.trim();
    if (line == "ring" || line.startsWith("ring ")) {
      app::Lock lock;
      ui::testAlarm(line.length() > 5 ? line.c_str() + 5 : "");
    }
    line = "";
  }
}

void loop() {
  serialCommands();
  {
    app::Lock lock;  // the web task takes this too; see app_lock.h
    wifisetup::loop();
    ui::loop();
    display::loop();
  }

  static uint32_t lastLog = 0;
  if (ntpStarted && millis() - lastLog > 60000) {
    lastLog = millis();
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[40];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", &t);
    Serial.printf("[ntp] %s%s\n", buf, now > 1700000000 ? "" : " (not synced yet)");
  }

  delay(2);
}
