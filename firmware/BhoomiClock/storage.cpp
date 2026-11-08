#include "storage.h"

#include <FS.h>
#include <SD_MMC.h>

namespace storage {
namespace {

// Pins (Waveshare ESP32-S3-Touch-LCD-2.8 TF slot, from Waveshare's demo)
constexpr int PIN_CLK = 14;
constexpr int PIN_CMD = 17;
constexpr int PIN_D0 = 16;
constexpr int PIN_D3 = 21;  // held high to keep the card in SD (not SPI) mode

bool isMounted = false;
volatile uint32_t gen = 0;

}  // namespace

bool begin() {
  if (isMounted) return true;
  pinMode(PIN_D3, OUTPUT);
  digitalWrite(PIN_D3, HIGH);
  delay(10);
  if (!SD_MMC.setPins(PIN_CLK, PIN_CMD, PIN_D0)) {
    Serial.println("[sd] pin setup failed");
    return false;
  }
  // 1-bit bus, and never format: a mount failure must not wipe someone's card.
  if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
    Serial.println("[sd] no card / mount failed");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    Serial.println("[sd] no card");
    return false;
  }
  for (const char *d : {"/photos", "/sounds", "/mood-staging"})
    if (!SD_MMC.exists(d)) SD_MMC.mkdir(d);
  // Recover a clip if power failed between moving the old directory and activating the new one.
  for (int mood = 1; mood <= 5; ++mood) {
    String active = "/mindfulness/" + String(mood), backup = active + ".old";
    if (!SD_MMC.exists(active) && SD_MMC.exists(backup)) SD_MMC.rename(backup, active);
  }
  isMounted = true;
  gen = gen + 1;
  Serial.printf("[sd] mounted: %llu MB, %llu MB used\n", SD_MMC.totalBytes() >> 20,
                SD_MMC.usedBytes() >> 20);
  return true;
}

bool mounted() { return isMounted; }
uint32_t generation() { return gen; }
void touch() { gen = gen + 1; }
uint64_t totalBytes() { return isMounted ? SD_MMC.totalBytes() : 0; }
uint64_t usedBytes() { return isMounted ? SD_MMC.usedBytes() : 0; }

bool validFolder(const String &folder) { return folder == "photos" || folder == "sounds" || folder == "mood-staging"; }
String folderPath(const String &folder) { return "/" + folder; }

String cleanName(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 64; i++) {
    char c = in[i];
    bool ok = isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' ||
              c == ')';
    out += ok ? c : '_';
  }
  out.trim();
  while (out.startsWith(".")) out.remove(0, 1);  // no hidden files / no ".."
  if (out.isEmpty() || out.endsWith(".")) return "";
  return out;
}

}  // namespace storage
