#pragma once
#include <Arduino.h>

// microSD card (TF slot), SD_MMC 1-bit. Files live in two folders:
//   /photos  JPEGs already resized to fit the 320x240 screen (resized in the browser)
//   /sounds  audio as uploaded (mp3, wav, m4a, ...)
// The card is never formatted by the firmware.
namespace storage {

bool begin();        // mount; safe to call again (e.g. card inserted later)
bool mounted();
uint64_t totalBytes();
uint64_t usedBytes();

bool validFolder(const String &folder);   // "photos" or "sounds"
String cleanName(const String &name);     // safe FAT file name, "" if nothing usable
String folderPath(const String &folder);  // "/photos"

}  // namespace storage
