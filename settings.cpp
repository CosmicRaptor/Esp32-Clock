#include "settings.h"

#include <Preferences.h>
#include <time.h>

namespace settings {
namespace {

Preferences prefs;
const tz::Zone *zone = nullptr;

void apply() {
  setenv("TZ", posixTz(), 1);
  tzset();
}

}  // namespace

void begin() {
  prefs.begin("clock", true);
  String id = prefs.getString("tz", "");
  prefs.end();
  zone = tz::find(id);
  apply();
  Serial.printf("[settings] time zone: %s (%s)\n", zone ? zone->id : "not set", posixTz());
}

bool setTimezone(const String &id) {
  const tz::Zone *z = tz::find(id);
  if (!z) return false;
  if (z != zone) {
    prefs.begin("clock", false);
    prefs.putString("tz", z->id);
    prefs.end();
    zone = z;
    Serial.printf("[settings] time zone set to %s\n", z->id);
  }
  apply();
  return true;
}

const tz::Zone *timezone() { return zone; }
const char *posixTz() { return zone ? zone->posix : "UTC0"; }

}  // namespace settings
