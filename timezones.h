#pragma once
#include <Arduino.h>

// Curated time zones: IANA id (what phones report) -> POSIX TZ rule (what newlib understands).
// The setup page detects the phone's zone and preselects it.
namespace tz {

struct Zone {
  const char *id;     // IANA, e.g. "Asia/Kolkata"
  const char *city;   // shown in the picker and on screen (ASCII only: screen font)
  const char *posix;  // e.g. "IST-5:30"
};

const Zone *find(const String &id);  // nullptr if unknown
String listJson();                   // [{"id":..,"city":..},...]

}  // namespace tz
