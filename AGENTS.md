# AGENTS.md: Bhoomi's Clock

Read this before changing anything here. `HANDOFF.md` has the current state and what's left to do. `README.md` and `portal/README.md` are the detailed references, and they're kept accurate, so read the section you're touching. The portal has its own rules in `portal/AGENTS.md`.

## What this is

A gift: a panda-themed desk clock for Aryan's girlfriend, Bhoomi. It runs on a **Waveshare ESP32-S3-Touch-LCD-2.8** (ST7789 320×240 in landscape, 16 MB flash, 8 MB PSRAM, touch, microSD, I2S speaker) and sits in a stand at her home. Aryan controls it remotely from a **cloud admin portal** (`portal/`, a Python Cloudflare Worker): the photo, a message, alarms, layered animated **clock faces**, and both people's Spotify "now playing".

```
Aryan's phone ──▶ portal (Cloudflare Worker + KV, behind Cloudflare Access)
                      ▲  GET /api/device/state every 10 s (protobuf, ETag → mostly 304)
                      │  GET /api/device/{image,face,asset,art}/…   PUT /api/device/sounds
                  the clock (this sketch) at Bhoomi's home, on weak Wi-Fi (−85 to −93 dBm)
```

The clock only makes outbound connections. It also serves a LAN settings page at `http://bhoomi-clock.local` (Wi-Fi, time zone, photos and sounds on the SD card, replacement keys) and a captive-portal setup hotspot `Bhoomi-Clock`.

## Layout

| Path | What |
|---|---|
| `*.ino`, `*.cpp`, `*.h` (root) | Arduino sketch (LVGL 9.2.2). See the file table in `README.md` |
| `clock.pb.{c,h}` | **Generated** nanopb code. Never edit by hand |
| `proto/clock.proto` | The clock ↔ portal API, the single source of truth. `proto/generate.sh` regenerates both sides |
| `portal/` | Admin portal: Python Worker, static pages, tests. See `portal/AGENTS.md` |
| `tools/check_face_*.sh` | Host-side checks that the C++ face maths and rules match the portal's JS |
| `portal_config.h` | **Gitignored secrets** (portal URL, device token, Access service token). Template: `portal_config.example.h` |

## Commands

```bash
./build.sh                    # compile only; prints app size. Always run after firmware or proto changes
./build.sh flash              # compile + upload over USB. Only with the user's go-ahead (see below)
tools/check_face_motion.sh    # C++ face_motion.h vs portal/public/face-motion.js
tools/check_face_rules.sh     # C++ face_rules.h vs portal/tests/vectors/face_rules.json
proto/generate.sh             # after editing clock.proto / clock.options (needs protoc + uv)
cd portal && npm test         # portal unit tests (Python + JS)
```

`build.sh` uses the Arduino IDE's bundled `arduino-cli` and copies `lv_conf.h` into the libraries folder. Edit the repo copy, never the one in `~/Documents/Arduino/libraries`. Libraries: `lvgl@9.2.2`, `ESP32-audioI2S-master@3.4.7`, and the Nanopb 0.4.9.1 runtime copied by hand into `~/Documents/Arduino/libraries/Nanopb/`.

**Serial log:** `arduino-cli monitor -p /dev/cu.usbmodem* -c baudrate=115200`, using the same bundled CLI path as in `build.sh`. pyserial isn't installed. A small stdlib `termios` reader also works. The board only shows output while it's printing. The Arduino IDE's Serial Monitor holds the port, so close it before uploading. If an upload says "No serial data received", ask the user to hold **BOOT**, replug USB and release.

## Hard constraints

- **The app uses custom two-slot 6 MB partitions** (`partitions.csv`, `PartitionScheme=custom`). Report the size from `./build.sh` in every firmware change. Changing the layout needs USB and resets FFat; never change it without the user's agreement.
- **Internal RAM is tight**, about 31 KB free while polling. Put anything big in PSRAM: `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. LVGL's pool is already in PSRAM, and mbedTLS allocations of 1 KB or more go there through the hook in `portal_client.cpp` (see "HTTPS" in `README.md`). Watch `min heap` in the `[portal] poll (...)` log line.
- **Tasks and locking:** the main loop drives LVGL, the web server and the portal client run as their own FreeRTOS tasks, and audio has its own task. Take the lock in `app_lock.h` before touching LVGL objects or shared state from any task other than the loop.
- **Weak Wi-Fi at the destination.** Every network path must time out, back off and keep the last good state. The clock must keep telling time and ringing alarms offline. Alarms and face rules are evaluated **on the clock** in its local time, never by the server.
- **Keep the protobuf schema backward compatible.** Only add fields with new numbers, and never renumber or reuse one. Change `proto/clock.options` limits together with `portal/src/model.py`, then run `proto/generate.sh`, `./build.sh` and `npm test`.
- **Faces must look the same in the portal preview and on the clock.** `face_motion.h` mirrors `portal/public/face-motion.js` and `face_rules.h` mirrors `portal/public/face-rules.js` and `model.py`. Change both sides together and run both `tools/check_face_*.sh`.
- The screen redraws over 40 MHz SPI. The portal's redraw budget (`BUDGET_PX`, `ANIM_FPS` in `portal/src/model.py`) assumes 15 animation ticks a second. Don't raise the frame rate without re-measuring.

## Local release access

`portal/.dev.vars` contains the admin password (gitignored, mode 600). Use `tools/firmware_release.py` for authenticated release uploads, explicit installation, interval settings and status. It reads existing Access credentials from `portal_config.h`; never print either file. Do not put credentials in docs or commits.

## Working with the user

- **Ask before flashing the board, deploying the portal (`npm run deploy`), changing secrets, or pushing.** The deployed portal and the flashed clock are live in someone's home.
- **Never commit secrets:** `portal_config.h`, `portal/.dev.vars`, tokens, or the deployed Access credentials. Don't print them in chat either.
- **The setup hotspot:** joining `Bhoomi-Clock` takes the user's Mac offline. If they must join it, say so, then end your turn and wait for them to come back. Prefer verifying over USB serial or `http://bhoomi-clock.local`.
- You can't see the screen. After a flash, read the serial log and ask the user what the screen shows.
- Commit style, from `git log`: a subject like `Area: what changed` (e.g. `Faces: …`, `Firmware: …`, `Portal: …`), then a wrapped body explaining what and why. Mention the firmware size when it changed.
- Docs are plain and specific. Update `README.md` / `portal/README.md` in the same change as the code they describe.
