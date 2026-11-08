# Handoff: Bhoomi's Clock

## Face artwork SD cache — flashed, 8 November 2026

Firmware 0.11.0 caches verified face protobufs and SHA-256-addressed images under `/portal/face-cache` and `/portal/face-assets` on microSD. It restores cached faces in the portal task before its first network poll, prioritises the current face when time is valid, and retains the last complete on-card face until a new definition and all its images are cached. Missing/corrupt entries fall back to the network; the cache is bounded and obsolete files are pruned only after current faces are committed. See README for the valid-time limitation and cache behavior. USB flash verified; all ten current faces populated their caches. On a second USB restart, serial confirmed the current sunset face was built from cached files before the first portal poll. No check-in was triggered. Firmware uses 3,031,906 bytes (48% of the 6 MB slot), with 85,576 bytes global RAM. Tests: 174 Python, 76 JS, and `tools/check_face_cache.cpp` host checks passed. Physical screen confirmation is still needed.

## Face revision repair — 8 November 2026

The café and sunset faces had published state revisions that did not match their stored documents, causing repeated downloads. Their existing documents were saved one at a time with time for KV propagation; the references now match and their 08–12 and 18–20 schedules remain intact. Video logs confirmed moods 1–3 were already installed rather than re-downloaded. Faces and artwork still live only in PSRAM and must be fetched again after each reboot; the board reported weak Wi-Fi around −96 dBm during the check.

## Separate speaker volumes — deployed and flashed, 8 November 2026

The admin and LAN pages have separate alarm and mood-video sliders (0–21, default 18). LAN changes persist and take effect immediately; pending local changes win over cloud settings until the server confirms the same values. See the portal README for synchronization behaviour. The clock and portal were updated; deployed Worker version `f7cfc93a-051b-4363-b76f-78d03c107e67`. Firmware uses 3,021,410 bytes (48% of a 6 MB slot), with 85,432 bytes global RAM. Python/JS tests, host conflict checks and isolated HTTP integration passed; USB flash verified and the clock's LAN volume endpoint reported 18/18 with no pending edit. A physical audio and live settings-change check is still needed.

## Mindfulness and mood videos — deployed and flashed, 8 November 2026

The five-point check-in runs every three hours and closes after five minutes. Responses queue offline and the private portal retains them for 30 days. Each mood can have a note and a video with sound: both portals prepare uploads in the browser, and the clock downloads remote uploads to microSD. `checkin` on serial shows the prompt; `moodstatus` reports installed clip hashes, frame counts and audio. The clock and portal were deployed, and Aryan confirmed the flow works. Moods 1–3 have videos on the clock; 1 and 2 currently use the same video. Portal version `98b3cd8d-9f8d-4acb-a57e-0f7fbda945c1`. Firmware including serial download diagnostics uses 3,014,762 bytes (47% of a 6 MB slot), with 85,432 bytes global RAM. The portal indicates a saved video but has no device-confirmed delivery status. No pending firmware install was approved after USB flashing. See the portal README for limits, playback and offline behaviour.

## Weather face variations — deployed and flashed, 7 November 2026

All nine panda faces now have default, cloudy/foggy and stormy rain scenes in production. Eighteen edited backgrounds and transparent rain/lightning sprites are under `portal/public/panda-collection/`. The Faces editor previews them; setup upgrades existing face IDs in place while preserving base edits, schedules, order and pin. The larger renderer copy is in PSRAM. Added protobuf fields are backward compatible, and code 97 is now accepted as a thunderstorm.

Aryan requested lightning **every second at new random sky positions**. This uses `scatter.period=1000`, a matching one-second blink/fade, and per-scene sky bounds; the café/bedroom use their windows. `face-motion.js` and `face_motion.h` agree on reseeding at each cycle. Weather classification agrees in Python, JS and C++.

All 27 browser-converted combinations passed the real model checks; maximum estimated animation budget is under 8%. Checks: **167 Python + 71 JS tests**, 291 C++/JS motion lines and 23 rule cases pass. Firmware **0.10.0** is **2,990,666 bytes (47% of a 6 MB slot)**, with 83,168 bytes global RAM. Portal deployment: `57165159-e0df-4ecf-bb28-7a466a25f85d`. All nine live faces were upgraded in place; schedules, fallback, order and pin were preserved. No extra portal page was added. Local review remains at `http://127.0.0.1:8790/weather-review`; its files are outside the deployed public directory.

USB flashing and hash verification succeeded. Serial polling remained healthy for several minutes on weak Wi-Fi; Aryan confirmed the physical screen looks right. During asset fetching we found an idle-task watchdog starvation path in HTTPClient's body loop. Small buffered responses now use cooperative yielding, 100 ms socket read waits and a 15-second overall body deadline; stalled transfers retain the previous state. The large OTA image stream keeps its separate transfer handling. A host test exercises the actual helper's yielding, deadline and write-error behavior.

The old approved 0.9.1 OTA digest was cancelled before USB flashing to avoid a downgrade. The optional release-library upload was blocked by automatic approval review and was not retried; the stored library release remains 0.9.1 with no pending approval. No secrets or firmware binaries belong in this commit. Artwork and maintenance details: [portal/ARTWORK.md](portal/ARTWORK.md).

**2026-11-07 OTA update:** custom two-slot 6 MB layout installed by USB; authenticated OTA and adjustable diagnostics deployed. A real OTA transfer passed startup health checks, and a deliberately rejected trial was confirmed to roll back to the previous app with the failed digest remembered. Final normal firmware 0.9.1 was delivered over OTA and passed startup health checks; it uses 2,988,306 bytes (47% of a 6 MB slot); no rejection-test code remains in the source. Portal checks: 160 Python + 68 JS tests and the local HTTP management checks pass. Portal deployment: `3a42f0c9-11a3-49c3-be96-dfba5ba7a446`. See the release workflow at the end. The older status below is historical.

*Written 2026-11-07, when `main` was at `e03f3ad` (pushed to origin, the same code deployed to the portal and flashed on the board).*

Start with `AGENTS.md` (rules and commands), then this file (state and what's next). `README.md` and `portal/README.md` hold the details.

## In one paragraph

A gift clock for Bhoomi (Aryan's girlfriend), a panda fan. It runs on a Waveshare ESP32-S3-Touch-LCD-2.8 in a 3D-printed stand. Aryan controls it from anywhere through an admin portal on Cloudflare: the photo, a short message, alarms, animated layered **clock faces** chosen by rules (night, winter, special dates…), and a strip showing what each of them is playing on Spotify. The clock polls the portal every 30 s over one kept-alive HTTPS connection, using protobuf with ETags. Everything time-based (alarms, which face to show) is decided on the clock, so it keeps working offline.

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
| Secrets | In `firmware/BhoomiClock/portal_config.h` (gitignored), `portal/.dev.vars` (local, gitignored), and Worker secrets in Cloudflare. The clock's keys can also be replaced from its LAN settings page (saved in NVS). No values are written in any doc |
| Outside the repo | `../hardware/reference/` holds board schematics and Waveshare demo zips (the authoritative pin maps); `../hardware/enclosures/` holds case and stand models (`Waveshare28_case_v3`, etc.) |

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

### Remaining work before the clock is given away

1. **OTA and diagnostics are implemented** (firmware 0.9.1). See the main README's “Firmware updates and diagnostics” and the portal README's API section. The USB baseline installs the custom two-slot 6 MB layout; future app releases can be uploaded and approved remotely. Bootloader rollback is enabled, with Arduino's early auto-confirmation deferred until the main loop and authenticated cloud pass startup checks. Reports default to hourly with remotely selectable 5 min–6 hour intervals or off.
2. **App partition space is resolved.** `partitions.csv` has two 6 MB app slots, ~3.9 MB FFat and coredump. `build.sh` exports application binaries in `build/`, checks the actual 6 MB slot limit, and uploads those exact artifacts. The initial migration was authorized and performed over USB, preserving Wi-Fi NVS and SD files.
3. **Confirm the faces work on the board** (see the checklist above), then tune the budget constants from real measurements.
4. **Test at Bhoomi's place, or with her router's signal level.** At −90 dBm or worse, every remote feature gets flaky. Consider a Wi-Fi extender near the clock.

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

- **The clock pulls, the server never pushes.** Her router blocks inbound connections, and on −90 dBm Wi-Fi one kept-alive poll every 30 s is sturdier than MQTT or WebSockets. With ETags it's mostly empty 304s, about 3% of the Workers free tier.
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

## OTA release workflow (2026-11-07)

The admin password is saved in gitignored `portal/.dev.vars` (mode 600), not in this document. `tools/firmware_release.py` uses it plus the existing Access service credentials from `firmware/BhoomiClock/portal_config.h`. `upload` publishes `build/BhoomiClock.ino.bin`; `install <sha256>` explicitly approves it; `status`, `cancel`, and `interval <seconds>` manage it. A Git push alone does not install firmware. Uploaded firmware is retained in content-addressed KV keys. Reports are latest snapshots rather than a log archive.

The physical screen still needs the user's visual check after future updates. Serial confirms initialization and the health gate, not pixel correctness. Keep all existing pre-deployment and pre-flash authorization rules.
