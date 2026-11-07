# Handoff: Bhoomi's Clock

*Written 2026-11-07, when `main` was at `e03f3ad` (pushed to origin, the same code deployed to the portal and flashed on the board).*

Start with `AGENTS.md` (rules and commands), then this file (state and what's next). `README.md` and `portal/README.md` hold the details.

## In one paragraph

A gift clock for Bhoomi (Aryan's girlfriend), a panda fan. It runs on a Waveshare ESP32-S3-Touch-LCD-2.8 in a 3D-printed stand. Aryan controls it from anywhere through an admin portal on Cloudflare: the photo, a short message, alarms, animated layered **clock faces** chosen by rules (night, winter, special dates…), and a strip showing what each of them is playing on Spotify. The clock polls the portal every 10 s over one kept-alive HTTPS connection, using protobuf with ETags. Everything time-based (alarms, which face to show) is decided on the clock, so it keeps working offline.

## Weather deployed and flashed: 7 November 2026

Weather is deployed as Worker version `d3c7e41e-9bd8-4f18-8518-f3e5be1e192a`, and the corrected firmware `0.8.0` was flashed over USB with the user’s approval. Firmware size is **2,978,470 bytes (94%)**, with 86,856 bytes of global RAM. Checks: 155 Python and 66 JavaScript tests pass, plus `bash tools/check_weather_icons.sh` against the real LVGL headers.

Weather appears beside the date after a city is chosen in the admin portal or LAN settings. Both change the same location. A separate 15-minute protobuf request uses an on-demand Open-Meteo KV cache. Aryan selected Mumbai in the portal; the live endpoint returned clear night, 32°C, and the clock’s LAN `/weather` confirmed Mumbai with no pending update. `/portal` reported `state=ok`, HTTP 304. Serial confirmed the sunset face rendered with seven particles, continued asset downloads and sound-list upload (204). Aryan confirmed the physical display works after the correction.

The first weather build rebooted when rendering a weather symbol: its bitmap callback returned packed bytes instead of the LVGL 9 A8 draw-buffer object. The decoded crash was in `lv_draw_sw_blend_color_to_rgb565`, reached from `draw_letter_cb`. The generator now expands into the supplied draw buffer and returns it; the host regression checks all eight glyphs, padded rows and buffer bounds. The repaired firmware was flashed and verified on the board.

See the Weather sections in both READMEs. Direct cloud verification needed the clock’s normal `ESP32HTTPClient` User-Agent; Python’s default User-Agent was rejected with HTTP 403.

## Live update: 7 November 2026

The approved nine illustrated panda faces are now installed through the existing `/faces` editor, with themed Spotify placement and the approved daily Asia/Kolkata schedule (see `portal/README.md`). `Paris sunset with cat` is the fallback, no face is pinned, and the old `Paris 1` is retained. No separate collection page is deployed; its local review files were archived outside `public`.

Portal version `ae172a85-d5c5-4b09-b01e-10f5e31ec32d` is deployed. The current working-tree firmware was flashed over USB: **2,969,722 bytes (94%)**, with the Spotify text-column gap and themed cover placeholders. Serial confirmed all nine face definitions and their images fetched, then the sunset face (`b0cc96514c6a`, rev `a385c740e48f`) built with seven particles. Aryan confirmed the physical screen looks right. The collection, firmware styling, assets, tests and documentation are included in the panda collection change on `main`. Portal checks: 147 Python and 63 JS tests passing.

## What's live right now

| Thing | State |
|---|---|
| Repo | `github.com/CosmicRaptor/Esp32-Clock`, branch `main`. The branches `faces-firmware`, `panda-faces` and `spotify-now-playing` are fully merged, so they can be deleted (ask first) |
| Portal | Deployed Worker `bhoomi-clock` at `https://bhoomi-clock.bhoomi-clock-portal.workers.dev`, version `d3c7e41e-9bd8-4f18-8518-f3e5be1e192a`. Behind Cloudflare Access (email one-time PIN for people, a service token for the clock) plus an admin password |
| Clock firmware | Flashed with weather support, version 0.8.0 (2,978,470 bytes, 94%). On Aryan's Wi-Fi (weak, about −85 dBm). Not handed over to Bhoomi yet |
| Secrets | In `portal_config.h` (firmware, gitignored), `portal/.dev.vars` (local, gitignored), and Worker secrets in Cloudflare. The clock's keys can also be replaced from its LAN settings page (saved in NVS). No values are written in any doc |
| Outside the repo | The parent folder `../` holds the board schematics, the Waveshare demo zips (the authoritative pin maps) and the case and stand models (`Waveshare28_case_v3`, etc.) |

## Feature status

| Feature | Built | Seen working on the board |
|---|---|---|
| Wi-Fi captive-portal setup with QR code, time zone, NTP | ✅ | ✅ |
| LAN settings page: photos and sounds on SD, cloud keys | ✅ | ✅ |
| Portal: photo, message, alarms (sound from `/sounds` or the built-in beep, touch to stop) | ✅ | ✅ |
| Spotify now-playing strip (both people) | ✅ | ✅ |
| HTTPS to the portal (mbedTLS buffers moved to PSRAM) | ✅ | ✅ |
| **Clock faces**: portal editor, validation, redraw budget, device API, LVGL renderer | ✅ | ✅ All nine collection faces downloaded; sunset face with seven particles visually confirmed |
| Per-face Spotify strip placement, layer size %, random scatter | ✅ | ⚠️ Same as above |

**What to check first on the faces** (from `README.md`, "Faces"):
- a sprite sheet shows exactly one frame at a time
- pulsing sprites scale about their centre
- the real frame rate compared with the redraw budget (`BUDGET_PX` in `portal/src/model.py`)
- the time and date text land exactly where the portal preview puts them

## What's next, in priority order

### Must do before the clock is given away (all need USB)

1. **OTA firmware updates.** None exist today. Once the clock is at Bhoomi's place, any bug fix would mean getting it back. Rough plan:
   - Add new fields to `DeviceState` (firmware version, URL, SHA-256).
   - Serve the image from the portal (from KV, or R2 if it's too big for a KV value).
   - Install it with `esp_https_ota` into the other app slot. The current scheme already has two 3 MB slots.
   - Add a fallback: check whether this Arduino core's bootloader has app rollback enabled. If it doesn't, add an app-level boot health check that switches back to the previous slot.
   - Ideally the image is signed, or at least its hash is checked, with the hash delivered over the authenticated state.
2. **Room in the app partition.** It's ~94% full (≈2.97 MB of 3.0 MB), and OTA will add code.
   - Switch to a custom `partitions.csv` with two 6 MB app slots, a ~3.9 MB FFat and a coredump partition. A draft from the design discussion is in the box below.
   - This can only be flashed over USB, and it wipes FFat. The built-in beep regenerates itself on first boot, and `/state.pb` simply re-syncs.
   - **Do this before OTA ships.** A partition table can never be changed safely over OTA.
   - Also: about 800 KB of the app is `ESP32-audioI2S`'s decoders (AAC, Vorbis, Opus, FLAC, MP3). Converting every sound to MP3 on upload and replacing the library with a small MP3+WAV player in `sound.cpp` would save about 750 KB.
3. **Confirm the faces work on the board** (see the checklist above), then tune the budget constants from real measurements.
4. **Test at Bhoomi's place, or with her router's signal level.** At −90 dBm or worse, every remote feature gets flaky. Consider a Wi-Fi extender near the clock.

```
# Name,    Type, SubType,  Offset,   Size
nvs,       data, nvs,      0x9000,   0x5000
otadata,   data, ota,      0xe000,   0x2000
app0,      app,  ota_0,    0x10000,  0x600000
app1,      app,  ota_1,    0x610000, 0x600000
ffat,      data, fat,      0xC10000, 0x3E0000
coredump,  data, coredump, 0xFF0000, 0x10000
```

Check how the esp32 Arduino core picks up a sketch-folder `partitions.csv` and its size limit before relying on this. `build.sh` currently passes `PartitionScheme=app3M_fat9M_16MB`.

### Should do

5. **Faces survive a power cut.** Today faces and their images live only in PSRAM and download again after a reboot. Cache them on the SD card by content hash.
6. **Security hardening:**
   - **Cloudflare Access service token expiry:** check its lifetime in Zero Trust and set a reminder. The clock's LAN page can take a new token, but only from inside her network.
   - **LAN settings page:** it has no PIN, so anyone on her Wi-Fi can use it.
   - **Setup hotspot:** it's open and comes back when Wi-Fi is lost.
   - **Optional, strong:** sign the state document with an Ed25519 key Aryan holds, and put the public key in the firmware. Then even a compromised portal couldn't put content on her screen.
7. **Real art.** Panda faces need a clean background plus separate transparent sprite PNGs (moving parts painted out of the background). The reference sheet of 9 face mock-ups is in the design conversation, not the repo. The art pipeline is the bottleneck, not the code.

### Ideas discussed, not started

- Alarm snooze.
- Message read receipts: the clock reports when she opens it.
- A "send a heart back" tap.
- Full-screen animations as server-converted MJPEG, decoded with JPEGDEC.
- Polling less often at night or when idle.
- A Telegram bot as a second way to control the clock.

## Key decisions and why

- **The clock pulls, the server never pushes.** Her router blocks inbound connections, and on −90 dBm Wi-Fi one kept-alive poll every 10 s is sturdier than MQTT or WebSockets. With ETags it's mostly empty 304s, about 9% of the Workers free tier.
- **Protobuf for the clock, JSON for the admin pages.** nanopb gives fixed-size structs on the ESP32, and `clock.proto` is the one contract between them.
- **Cloudflare Worker + KV, no VPS.** It's free and needs no maintenance, which suits a gift meant to run for years. KV's limit of 1,000 writes a day shapes the device path (see `portal/AGENTS.md`).
- **Images are converted in the browser** (baseline JPEG, LVGL `RGB565A8`), so neither the Worker nor the clock decodes or resizes anything heavy.
- **Faces are layered scenes, not GIFs, Lottie or video.** That gives control of each item's motion, small downloads, and only the moving parts redraw. Every motion is a deterministic function of time, so the portal preview and the clock match. There's no rotation, because it's software-rendered and expensive.
- **Rules run on the clock:** pinned face, then the first matching rule, then the fallback, then the built-in face. Alarms work the same way, so both keep working offline.
- **Defence in depth on the portal:** HTTPS only, Cloudflare Access with the JWT re-checked in the Worker, the admin password, and the device bearer token.

## Known quirks

- Joining the `Bhoomi-Clock` hotspot takes Aryan's Mac offline. An agent must hand over and wait, not keep running.
- The Arduino IDE being open holds the serial port. Uploads may need **BOOT** held while replugging.
- Running packages fetched with `npx` (e.g. `lv_font_conv`) was blocked by the permission classifier earlier. Prefer offline routes (the clock uses LVGL Tiny TTF with the bundled Montserrat).
- The Nanopb runtime isn't in the Arduino Library Manager. It's copied by hand into `~/Documents/Arduino/libraries/Nanopb/`.
- `protoc`'s version must not be newer than the `protobuf` package the Worker vendors.
- Spotify's development-mode app only works for users listed in its dashboard, and OAuth redirects locally need `127.0.0.1`, not `localhost`.
