#pragma once
#include <Arduino.h>

#include "clock.pb.h"

// Talks to the cloud admin portal (see portal/README.md, "Clock API"), in its own task so a slow
// connection never stalls the screen:
//   - polls GET /api/device/state every 10 s over one kept-alive connection (ETag / If-None-Match -> bare 304).
//     The state carries the note, alarms, photo id and what the two people are playing on Spotify
//   - downloads GET /api/device/image/<id> when photo.id changes (retried on 404 / failure)
//   - downloads GET /api/device/art/<id>, the 64x64 covers for the now-playing strip
//   - PUTs the /sounds file list at boot and whenever the card's files change
// Configure it in portal_config.h (see portal_config.example.h); the keys can also be changed on the
// LAN settings page (setKey).
//
// Reading the result: take app::Lock first (ui::loop() already runs under it). Everything returned
// here points at the client's own memory and stays valid only until the lock is released; a new
// state or photo is swapped in under the same lock.
namespace portal {

constexpr const char *FW_VERSION = "0.6.0";  // sent as ?fw=

using State = bhoomi_clock_v1_DeviceState;  // .has_note/.note.text, .has_photo/.photo, .alarms[0..alarms_count)
using Alarm = bhoomi_clock_v1_Alarm;
using NowPlaying = bhoomi_clock_v1_NowPlaying;  // .left / .right: what the two people are listening to
using Listener = bhoomi_clock_v1_Listener;      // .status (OFF/PLAYING/PAUSED), .title, .artist, .art_id

void begin();  // start the task; call after Wi-Fi is initialised. No-op if not configured.

bool configured();
bool haveState();            // true once a state has been fetched and decoded
const State &state();        // the latest state (all zero until haveState())
const char *note();          // message text, or nullptr when there is none
const uint8_t *photo(size_t *size);  // 320x240 baseline JPEG, or nullptr (not downloaded / none set)
const char *photoId();       // id of the JPEG photo() returns, "" when none

// Changes whenever the note, photo bytes or alarms change; lets ui re-render only when needed.
uint32_t revision();

// ---- Spotify "now playing" (the strip on the clock face; DeviceState.nowplaying) ----
// Same locking rule as above.
// Side 0 is the left of the strip (the first person), side 1 the right.
bool nowPlayingFresh();                      // the portal answered in the last 2 minutes (else: show nothing)
const NowPlaying &nowPlaying();              // all zero (= both sides OFF) until the first poll
const uint8_t *cover(int side, size_t *size);  // that side's 64x64 baseline JPEG, or nullptr (none / not downloaded yet)
const char *coverId(int side);               // art_id of the JPEG cover() returns, "" when none
uint32_t nowPlayingRevision();               // changes when the answer or a cover changes

// ---- keys (the LAN settings page; take app::Lock first) ----
// The device token and the Cloudflare Access service token start out as portal_config.h's values;
// the settings page can replace them (saved in NVS) so an expired key never needs a reflash.
enum Key { KEY_TOKEN, KEY_ACCESS_ID, KEY_ACCESS_SECRET, KEY_COUNT };
// Why `value` can't be used for key k (a human sentence), or nullptr if it's fine. "" is fine (= built-in).
const char *keyProblem(Key k, const String &value);
// "" goes back to the built-in value. On success the client reconnects with it at once.
bool setKey(Key k, const String &value, String *error = nullptr);
// {"url","state":"ok|access|token|https|offline|error|waiting|nokey|off","code","ago","okAgo",
//  "keys":{"token"|"accessId"|"accessSecret":{"set","custom","builtin","hint"}}}. Never the key values.
String keysJson();

}  // namespace portal
