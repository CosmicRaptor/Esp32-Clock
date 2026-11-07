#pragma once
#include <Arduino.h>

#include "timezones.h"

// Persistent user settings (NVS).
namespace settings {

void begin();                          // load + apply the saved time zone
bool setTimezone(const String &id);    // validates, saves, applies; false if unknown id
const tz::Zone *timezone();            // nullptr until one has been chosen
const char *posixTz();                 // rule in effect ("UTC0" until chosen)

}  // namespace settings
