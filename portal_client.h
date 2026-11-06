#pragma once
#include <Arduino.h>

#include "clock.pb.h"

// Talks to the cloud admin portal (see portal/README.md, "Clock API"), in its own task so a slow
// connection never stalls the screen:
//   - polls GET /api/device/state about once a minute (ETag / If-None-Match -> bare 304)
//   - downloads GET /api/device/image/<id> when photo.id changes (retried on 404 / failure)
//   - PUTs the /sounds file list at boot and whenever the card's files change
// Configure it in portal_config.h (see portal_config.example.h).
//
// Reading the result: take app::Lock first (ui::loop() already runs under it). Everything returned
// here points at the client's own memory and stays valid only until the lock is released; a new
// state or photo is swapped in under the same lock.
namespace portal {

constexpr const char *FW_VERSION = "0.5.0";  // sent as ?fw=

using State = bhoomi_clock_v1_DeviceState;  // .has_note/.note.text, .has_photo/.photo, .alarms[0..alarms_count)
using Alarm = bhoomi_clock_v1_Alarm;

void begin();  // start the task; call after Wi-Fi is initialised. No-op if not configured.

bool configured();
bool haveState();            // true once a state has been fetched and decoded
const State &state();        // the latest state (all zero until haveState())
const char *note();          // message text, or nullptr when there is none
const uint8_t *photo(size_t *size);  // 320x240 baseline JPEG, or nullptr (not downloaded / none set)
const char *photoId();       // id of the JPEG photo() returns, "" when none

// Changes whenever the note, photo bytes or alarms change; lets ui re-render only when needed.
uint32_t revision();

}  // namespace portal
