# Bhoomi's Clock

Wi-Fi/NTP desk clock for the **Waveshare ESP32-S3-Touch-LCD-2.8** (ST7789 240×320, ESP32-S3R8, 16 MB flash, 8 MB PSRAM), using LVGL 9.

## Status

- [x] Wi-Fi setup via captive portal. The screen shows a QR code to join the open `Bhoomi-Clock` hotspot, and the phone gets a setup page.
- [x] Credentials are saved only after a successful join. The hotspot turns off once connected.
- [x] Time zone picker on the setup page, preselected from the phone. It can be changed later at http://bhoomi-clock.local.
- [x] NTP sync, local time shown in landscape (the stand: USB-C on the right)
- [x] Photos & sounds on the microSD card. Drag and drop them on the settings page. Photos are resized in the browser to fit 320×240 (JPEG, EXIF rotation applied), and sounds upload as-is.
- [x] Alarms from the portal: full-screen alarm with the label, the chosen sound from `/sounds` (or a built-in beep) on repeat, and a touch anywhere stops it. No snooze yet.
- [x] Spotify now playing: a small strip on the clock face, your song on the left and hers on the right (64×64 cover, title, artist). Both accounts are connected in the portal; the clock never sees Spotify.
- [x] Clock faces from the portal: layered, animated scenes (background, bobbing sprites, snow and petals, the time and date wherever the face puts them). The clock chooses by the faces' rules and its own time (the portal's pinned face wins). Running on the board; the sunset panda-and-cat face has been visually confirmed.
- [x] Nine illustrated panda faces, animated accents, daily scheduling, and themed Spotify widgets

## Build & flash

Needs the Arduino IDE (its bundled `arduino-cli` is used), the `esp32:esp32` core 3.x, the `lvgl@9.2.2` and `ESP32-audioI2S-master@3.4.7` libraries (`arduino-cli lib install ESP32-audioI2S-master@3.4.7`), and the Nanopb 0.4.9.1 runtime in `~/Documents/Arduino/libraries/Nanopb/` (not in the Library Manager index; see `portal/README.md`, "Notes").

The Arduino sketch is in `firmware/BhoomiClock/`; its folder name matches `BhoomiClock.ino`, as required by Arduino. To talk to the cloud portal, copy `firmware/BhoomiClock/portal_config.example.h` to `firmware/BhoomiClock/portal_config.h` (gitignored) and set `PORTAL_URL`, `PORTAL_DEVICE_TOKEN` and the Cloudflare Access service token (`PORTAL_ACCESS_CLIENT_ID` / `PORTAL_ACCESS_CLIENT_SECRET`). Without a URL the sketch still builds and the portal client stays off. The keys are only the defaults: the settings page can replace them (see below), so an expired key never needs a reflash.

```bash
./build.sh          # compile
./build.sh flash    # compile + upload to the first /dev/cu.usbmodem* port
```

The root `build.sh` wrapper compiles `firmware/BhoomiClock/` and leaves build outputs under the root `build/` directory. It copies `firmware/BhoomiClock/lv_conf.h` next to the LVGL library when the files differ, because Arduino can't point a library at a config file inside the sketch. Always edit the copy in this repo. `ARDUINO_CLI` and `ARDUINO_LIBRARIES` override the Mac defaults for Linux CI; `tools/ci_dependencies.sh` installs ESP32 core 3.3.12, LVGL 9.2.2, audioI2S 3.4.7 and Nanopb 0.4.9.1.

If uploads fail with "No serial data received", close the Arduino IDE Serial Monitor. Or hold **BOOT**, replug USB, and release.

## Using it

- **First boot / no saved Wi-Fi:** scan the on-screen QR code (or join `Bhoomi-Clock`), then pick your network on the page that pops up.
- **Saved network unreachable at power-on:** the setup hotspot comes back after about 20 s. The clock keeps retrying the saved network every minute.
- **Forget Wi-Fi:** hold **BOOT** for 5 s.
- **Change the time zone:** open http://bhoomi-clock.local (or the clock's IP) from any device on the same Wi-Fi.
- **Replace an expired key:** same page, **Cloud connection** card. It shows whether the clock reaches the portal, and says when Cloudflare Access turns it away (service token expired) or the device token is rejected. Under **Update keys**, paste the new Access Client ID / Secret or device token. They're saved on the clock (NVS) and used right away. The page never shows a saved key again, only its first or last 4 characters. **Go back to the built-in keys** forgets them.
- **Add photos/sounds:** same page, "Photos & sounds" section. Files go to `/photos` and `/sounds` on the card. The card must be FAT32, and the firmware never formats it. Uploads are slow on a weak signal (about 50 KB/s at −90 dBm), so resized photos (~10–40 KB) are quick but big sound files take a while.
- **Alarms:** set them in the portal. At the alarm time the screen turns pink and pulses, and the sound repeats until you touch the screen (or press **BOOT**). It stops by itself after 5 minutes. The sound is the file picked in the portal from `/sounds` (mp3, m4a, aac, flac, ogg/opus, wav). No pick, no card, a missing file, or a file that won't decode all fall back to the built-in beep (kept in the internal flash, so it works without a card). Try it without waiting: type `ring` (or `ring <file name>`) in the serial monitor at 115200 baud.
- **Screen upside down?** Set `FLIP_180` in `display.cpp`.

## Focus and night modes

Drag horizontally through Clock → Focus → Night, or right to go back, from anywhere on a page (including over buttons). The pages follow the finger and snap one page at a time. During dragging, only lightweight mode titles are drawn; artwork and controls return after settling, with a 120 ms title pause. Cancelled drags immediately restore the current screen. Face animation, UI asset application and screen construction wait while the pager moves; timers and alarms keep running. There is no timer shortcut. Swipe right from Focus to return to Clock at any time, including during a session; the timer keeps running until ended. Choose 15, 25 or 45 minutes, then tap **Start studying**. Focus uses an illustrated pastel study room with a panda writing in a notebook, a live clock/date badge and cream timer panel. A large countdown and thin progress ring appear during study; the selected duration is highlighted before starting. End stops a session. Spotify, the message inbox and mindfulness prompts stay hidden during study, including if you return to the clock. New portal notes stay unread and become available when study ends (the portal retains its one current note, not a message history).

One soft, flash-resident chime plays at completion, without repeating. Choose **Take 5 min** to start an optional break or **Done** to return to the clock. The break also ends with one chime. Timers use monotonic elapsed time, keep running when you swipe away, and tolerate NTP corrections and millis rollover. A reboot cancels a session. There are no streaks.

Night is manual: swipe to Night to dim the LCD backlight to 4%; swipe back to Clock for normal brightness. It shows red HH:MM on black, without seconds or artwork, and currently playing Spotify titles in red when available. An active study or break replaces the time with its countdown. Returning from Night to Focus keeps the dim palette; study artwork is omitted there. Alarms still ring and temporarily raise brightness, then restore it. There is no schedule or tap-to-peek setting. Timekeeping and focus work offline after time has been set; reboot starts in Clock mode.

Focus artwork is stored on microSD at `/ui/focus-study-v1.jpg` (31,513-byte baseline JPEG). On startup, firmware compares the managed file against its provisioned copy and writes/repairs it if needed; no LAN transfer or user-photo change is required. Opening Focus reads the JPEG in 1 KB chunks, decodes a 153,600-byte RGB565 buffer in PSRAM and releases it after its screen is deleted when leaving Focus. Start/End/break rebuilds within Focus reuse that buffer without another SD read/decode. Missing/failed SD artwork keeps the native controls usable with a plain fallback. Focus entered from Night keeps its existing dim red layout without loading illustrated artwork. Sources, SD-ready JPEG and generation prompt are in `assets/focus-study/`; exact LVGL renders are in its `previews/` folder. `uv run --with pillow python tools/render_focus_preview.py` renders the firmware UI, compares illustration/title redraw cost, and tests duration selection, Start/End, optional break, cancelled drags, scrolling and artwork reuse. Serial `[pager]` lines report scroll UI timing and clock build time; `[focus-art]` includes SD load/decode time.

Leaving Clock deletes its rendered clock/photo/Spotify widgets, decoded photo and covers, and rendered face including font caches. Focus and Night are built on demand and deleted on departure. Portal source assets and the fixed LVGL PSRAM pool remain cached for offline reconstruction; timer state lives separately from the screen. Setup and alarm overlays remain ready. The real LVGL pager has a headless pointer-drag check: `python3 tools/check_mode_pager.py` (uses the installed LVGL library; set `LVGL_ROOT` to override). It verifies forward/backward drags, page bounds and button click suppression. Host timer checks: `c++ -std=c++11 tools/check_focus_timer.cpp -o /tmp/check_focus_timer && /tmp/check_focus_timer`.

## Weather

The admin portal and the clock’s LAN settings page both have a searchable city picker and an off button. They change the same server-side location, independently of the clock’s time zone. Weather starts off until a city is chosen. The LAN picker searches Open-Meteo directly from the browser; changes are queued on the clock until the portal accepts them (a pending change is lost if the clock reboots before it is sent).

The clock requests `/api/device/weather` at boot and on its own 15-minute deadline. The server fetches Open-Meteo current conditions on demand and stores a simplified reading in KV for 15 minutes. No scheduled server job is needed. Only temperature, WMO condition, day/night, observation time and city reach the clock, as a separate protobuf message. The display adds a compact monochrome weather symbol and Celsius temperature beside the date, including downloaded faces and the photo view. The built-in date becomes shorter while weather is visible; face dates shorten only if needed to fit. Faces with no date slot keep it hidden. The Faces preview uses the cached weather with browser emoji.

Both sides keep the last good observation through failures, hiding it after three hours. Weather failures never stop the clock or alarms. The clock does not persist weather readings to flash; a reboot waits for a new successful weather response. An admin city change reaches the clock on its next weather check (up to 15 minutes); a LAN change requests a refresh immediately after it is accepted.

The small original weather glyphs are generated by `tools/generate_weather_icons.py` into `firmware/BhoomiClock/weather_icons.cpp`, with no extra fonts or libraries. Run `bash tools/check_weather_icons.sh` to check the LVGL 9 draw-buffer callback contract, all glyphs and buffer bounds on the host. [Weather data: Open-Meteo](https://open-meteo.com/).

## Faces

Weather variations keep the same scheduled face and switch its background and moving layers on the clock: default, cloudy/foggy, or stormy rain with lightning at random sky positions every second. The indoor scenes keep rain/lightning in their windows. The larger face definitions and renderer copy live in PSRAM. See the portal README's Faces section for condition mapping, preview controls and backward-compatible fields.

The built-in nine-face panda collection in the portal includes a themed Spotify strip on each scene. Song columns have a small gap at the centre, and missing-cover placeholders use the face's artist colour. The collection's layouts and daily schedule are documented in `portal/README.md`; the faces are installed and edited on the existing `/faces` page.

The portal's Faces page designs them (see `portal/README.md`, "Faces"). On the clock:

- `portal_client.cpp` fetches each face listed in the state (`GET /api/device/face/<id>`, when its `rev` is new) and the images it uses (`GET /api/device/asset/<hash>`) into PSRAM: a background JPEG and LVGL `RGB565A8` sprite files, which need no decoding. At most 5 MB of images are kept; images no face uses any more are dropped. Validated face definitions and content-addressed artwork are cached on microSD in private `/portal/face-cache` and `/portal/face-assets` directories. After reboot, a background task restores the last complete faces and images into PSRAM before using the network; the currently selected face is prioritised when the clock already has valid time. The clock still needs a valid local time to choose a scheduled face. It checks state and fetches missing or updated items online; without a usable card, it falls back to the original network behaviour. A new face only replaces its on-card last-good copy after all of its artwork is cached. The cache is bounded to approximately 32 MB and prunes obsolete files once current faces are safely committed.
- `face_rules.h` chooses the face: the pinned one, else the first whose `when` fits the local date and time, else the default, else the built-in clock/photo. `ui.cpp` re-chooses when a face arrives and once a minute. A face that isn't completely downloaded isn't shown: the previous one stays.
- `face_view.cpp` builds the chosen face out of LVGL objects: the background decoded once (320×240 RGB565), one image per sprite copy and per particle (the sprite data is used where it is, only copied once out of the download cache), a clipping box per particle area, and Montserrat labels (Tiny TTF at the slot's size) for the time and date, with a shadow label behind. Every 66 ms (15 ticks a second, what the portal's redraw budget assumes) `tick()` moves what moves, changing only what changed.
- `face_motion.h` is the motion and particle maths: no LVGL, the same formulas as the portal's `public/face-motion.js` (see `portal/README.md`, "Motions" and "Particles"). `tools/check_face_motion.sh` compiles it on the computer and compares its numbers with the JS (poses with stagger and wrapping, the PRNG, particle positions); `tools/check_face_rules.sh` runs the portal's shared rule vectors through `face_rules.h`.
- The now-playing strip floats above a face where the face puts it (`NowPlayingSlot`: position, width, colours, or off; the usual dark pill at the top if the face says nothing), and a 40 × 40 message box opens the current portal note on demand. Its unread dot clears when opened and stays cleared across restarts; a new send lights it again. Each face can set its position; existing faces choose a corner opposite the Spotify strip. The Wi-Fi-lost / set-time-zone hint of the plain clock isn't drawn on a face.

Checked on the board: all nine collection faces and their images downloaded, the sunset face built with seven twinkling particles, and Aryan confirmed the screen looks right. Still to check: that a sprite sheet shows one frame at a time (`lv_image` offset inside a smaller box), that pulsing sprites scale about their centre, and how the redraw budget in `portal/src/model.py` (`BUDGET_PX`) compares with the real frame rate.

## HTTPS

HTTP socket single-byte reads yield for one FreeRTOS tick when no byte is available, including partial response headers: Arduino Stream otherwise busy-waits through its timeout and can trigger the idle watchdog. Small buffered portal responses yield to FreeRTOS after each body write, use 100 ms socket read waits and enforce a 15-second body deadline. This prevents weak-Wi-Fi TLS reads from starving the idle watchdog. Failed transfers keep the last good state. OTA streams through the same yielding helper with a ten-minute overall body deadline so weak-Wi-Fi bulk reads cannot starve IDLE0.

A deployed `https://…workers.dev` portal works: certificates are checked against the built-in Mozilla CA bundle, and the TLS connection is kept alive between the 30 s polls (the first handshake takes ~2 s on weak Wi-Fi, later polls ~60 ms). The clock needs NTP to have synced first, so it waits before the first HTTPS request.

This took a workaround. This Arduino-ESP32 core builds mbedTLS with `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`, so its two fixed 16 KB record buffers must sit in internal RAM, and the largest free internal block is only ~31 KB (`TLS: SSL - Memory allocation failed`). `portal_client.cpp` installs an allocation hook with `mbedtls_platform_set_calloc_free` that puts every mbedTLS allocation of 1 KB or more in PSRAM; small ones stay internal. With it, internal heap stays around 32–47 KB free during polling (check `min heap` in the `[portal] poll (...)` log line). If you ever rebuild the core with `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` the hook becomes redundant but harmless.

## Pins (from Waveshare's demo code)

| What | GPIO |
|---|---|
| LCD (ST7789, SPI) | SCLK 40, MOSI 45, CS 42, DC 41, RST 39, backlight 5 |
| microSD (SD_MMC 1-bit) | CLK 14, CMD 17, D0 16, D3 21 (held high) |
| I2S speaker amp (PCM5101) | BCLK 48, LRC 38, DOUT 47 |
| Touch (CST328 or CST3530, own I2C bus) | SDA 1, SCL 3, INT 4, RST 2 |
| I2C (RTC, IMU) | SDA 11, SCL 10 |
| Battery ADC / power key | ADC 8 / key 6, power hold 7 |

## Files

| File | What |
|---|---|
| `firmware/BhoomiClock/BhoomiClock.ino` | setup/loop wiring, NTP |
| `firmware/BhoomiClock/wifi_setup.*` | non-blocking join / captive portal + DNS / credential storage |
| `firmware/BhoomiClock/web.*` | HTTP server (own task) and mDNS (`bhoomi-clock.local`); file list/upload/download/delete; cloud keys (`/portal`) |
| `firmware/BhoomiClock/page_html.h` | the single page: Wi-Fi + time zone (setup) or time zone + photos & sounds (settings) |
| `firmware/BhoomiClock/storage.*` | microSD mount (never formats), folder and file-name rules |
| `firmware/BhoomiClock/portal_client.*` | cloud portal client (own task): polls the state every 30 s over one kept-alive connection (protobuf, ETag; it carries what's playing), downloads the photo, the 64×64 covers and the clock faces with their images, PUTs the `/sounds` list. Keeps a copy of the last state in internal flash (`/state.pb`, FFat) and the photo on the SD card (`/portal/photo.jpg`), restored at boot |
| `firmware/BhoomiClock/clock.pb.*` | generated nanopb code (`proto/generate.sh`) |
| `firmware/BhoomiClock/app_lock.h` | the lock shared by the main loop and the web task |
| `firmware/BhoomiClock/timezones.*` | curated IANA → POSIX TZ table |
| `firmware/BhoomiClock/settings.*` | saved time zone (NVS) |
| `firmware/BhoomiClock/display.*` | ST7789 over esp_lcd + LVGL glue, landscape rotation, backlight (GPIO 5) |
| `firmware/BhoomiClock/sound.*` | alarm audio: ESP32-audioI2S decoder + I2S out, own task; loops a `/sounds` file or the built-in beep (generated into flash on first boot) |
| `firmware/BhoomiClock/touch.*` | CST328 / CST3530 touch chip (auto-detected) registered as an LVGL pointer device |
| `firmware/BhoomiClock/ui.*` | LVGL screens: setup (QR), status, clock, alarm overlay |
| `firmware/BhoomiClock/face_view.*`, `face_motion.h`, `face_rules.h` | the portal's clock faces: the LVGL renderer, the motion and particle maths, and the `when` rules; `tools/` checks them against the portal's JS |
| `firmware/BhoomiClock/font_montserrat_ttf.c` | Montserrat Medium (OFL), rendered by LVGL Tiny TTF for the big digits |
| `firmware/BhoomiClock/lv_conf.h` | LVGL config. LVGL's memory pool lives in PSRAM: with the C heap, each new big clock digit (a ~4 KB glyph bitmap) used up internal RAM until Wi-Fi starved |
| `proto/` | `clock.proto`: the clock ↔ portal protobuf schema, plus the generated nanopb code (`proto/generate.sh`) |
| `portal/` | admin portal (Cloudflare Worker, Python) for the photo, message and alarms; see `portal/README.md` |

## Firmware updates and diagnostics

Firmware 0.9.1 adds authenticated, outbound OTA management. `./build.sh` exports **`build/BhoomiClock.ino.bin`**. With signing enabled, upload that application file and its matching signature JSON in the admin page's **Clock health & firmware** card, then click **Install uploaded firmware**. Never upload the merged image or bootloader. Uploading alone does not install. A management poll runs every minute; KV propagation may add another minute. Cancel stops future polls from discovering an install, but cannot stop a download already started.

The custom 16 MB partition table has two 6 MB app slots, 3.875 MB FFat and a coredump partition. Install this layout once over USB before relying on OTA. Migrating the layout resets the internal filesystem; NVS Wi-Fi credentials and the microSD are preserved. Normal OTA only writes the inactive app slot, never the partition table or bootloader. Keep a USB recovery path available during initial testing.

Downloads use the existing verified HTTPS connection and device/Access credentials. Signing-enabled firmware first verifies ECDSA P-256/SHA-256 metadata against its built-in public key, then streams into flash, checks exact size and whole-file SHA-256, and passes ESP image validation before selecting a boot slot. An incomplete or corrupt download leaves the old app selected. Updates defer while sound is playing. Signing is application-level verification of remote updates; hardware Secure Boot and flash encryption are not enabled.

The Arduino core's `verifyRollbackLater` hook defers its otherwise automatic early acceptance. Trial firmware has three minutes to finish startup, execute at least 1,000 main-loop ticks, run for a minute, and receive a successful authenticated state response. An independent timer reboots into the previous image if it cannot pass; a crash/reset also triggers bootloader rollback. This checks liveness and cloud access, not visual correctness or every peripheral. A rejected candidate is remembered in NVS and not automatically retried. Publish a new build to retry after fixing it. Offline trial boots deliberately roll back.

Diagnostics default to hourly, with off / 5 min / 15 min / 30 min / 1 hour / 6 hours available in the admin card. Changes arrive on the next management poll. The first report is sent after connecting. Reports contain firmware version and image digest, uptime, reset reason, RSSI, free/minimum heap, largest free internal block, free PSRAM, last state HTTP code, last OTA result, trial state, installed upload digest and failed candidate digest. No passwords, network names, messages or raw logs are sent. The portal stores the latest snapshot with receipt time; refresh the page to see a newer report. Failed uploads retry after five minutes. A server guard allows at most one routine report per five minutes; firmware/update transitions may report after one minute (eventual KV consistency is not a strict global rate limiter).

OTA result codes: `-10` no room/slot; `-11` length mismatch; `-12` flash begin failed; `-13` interrupted download/hash mismatch/write failed; `-14` invalid image; `-15` image digest unavailable; `-16` NVS/boot selection failed; `-17` alarm started before reboot; `-18` missing/invalid signature or public key; `-19` invalid/replayed/downgraded version. Other values are HTTP status codes; zero means no OTA error recorded this boot.

From this checkout, `python3 tools/firmware_release.py upload` uploads the build without installing it; `python3 tools/firmware_release.py install <SHA256>` approves that exact upload. `status`, `cancel`, and `interval 3600` inspect the clock, cancel pending discovery, or change report frequency. The tool reads existing gitignored local credentials without printing them. `python3 tools/check_management_http.py` exercises the local Worker on port 8788; do not point it at production.

### Scheduled signed releases

`.github/workflows/ota.yml` checks the default branch at **09:17 India time on Monday, Wednesday and Friday** (03:47 UTC), and supports a manual Actions run. GitHub schedules can be delayed. Firmware, protocol, build and release-tool changes since the last `clock-ota/<version>` tag trigger a release; documentation and portal-only changes do not. The first run publishes the current source. This workflow does not deploy portal code.

For changed firmware it checks the clock's reported public-key fingerprint, chooses a semantic version higher than the source, published tags and latest clock report, runs portal/signing/host tests, compiles, signs, uploads the binary and detached metadata, and approves that exact digest for automatic installation. An annotated Git tag records source, version, digest, size and signature only after approval succeeds. No source version bump is committed by CI. Failed publishes remain eligible for the next scheduled/manual run. A tag means installation was queued; check the portal's device report for healthy installation or rollback. Rejected trials require a newer version; the clock remembers the highest accepted signed version in NVS even across rollback.

The signed bytes are the ASCII text `bhoomi-clock-ota-v1\n<version>\n<whole-file-sha256>\n<size>\n`. Metadata is stored separately so the application `.bin` stays a normal ESP image. The clock trusts only `firmware/BhoomiClock/ota_signing_key.h`, never a key from the portal. Missing signatures, an empty key, and invalid signatures fail closed. The existing HTTPS, Access and password gates still apply. The signing key belongs only on the trusted development machine and in GitHub Actions; the portal never receives it. GitHub repository write access and Actions secrets remain trusted. Firmware contains device credentials, so the workflow does not upload build artifacts to GitHub.

One-time activation (requires approval for secrets, deployment, flash and push):

1. Run `python3 tools/ota_signing.py keygen`. It creates `.ota-secrets/signing.pem` with mode 600 and updates the public header. Back up the private key securely; never commit it. The tool refuses to overwrite an existing key.
2. With GitHub CLI authenticated, run `python3 tools/configure_actions.py` to add repository Actions secrets `OTA_SIGNING_PRIVATE_KEY` (the PEM file), `PORTAL_URL`, `PORTAL_DEVICE_TOKEN`, `PORTAL_ACCESS_CLIENT_ID`, `PORTAL_ACCESS_CLIENT_SECRET`, and `ADMIN_PASSWORD`, using existing local credentials without printing them. The workflow requests permission to write release tags; other workflows retain their existing permissions. Actions are pinned to immutable commits.
3. Deploy the portal changes. Bump `FW_VERSION` above the installed version, build with the real public key and USB-flash this trusted baseline. Verify normal operation and the diagnostic `signing_key` fingerprint. The bootstrap compile with the empty placeholder key must not be installed. The schedule refuses to release until the reported fingerprint matches the committed public key.
4. Commit/push the workflow, public header and source to the default branch, then run the workflow manually to verify the complete signed OTA path. Record the actual installed version and size in `HANDOFF.md`. No eFuses or partition changes are required.

For a manual release, bump `FW_VERSION` before building, then sign and upload without approving installation:

```bash
python3 tools/ota_signing.py sign build/BhoomiClock.ino.bin --version <FW_VERSION> --private-key .ota-secrets/signing.pem --output build/signature.json
python3 tools/firmware_release.py upload --signature build/signature.json
```

The admin page accepts the same `.bin` and `.json` pair. Retries reuse the same artifact; Cloudflare KV propagation is handled by bounded retries. Public-key rotation needs a deliberately staged trusted update; replacing the header and secret alone is insufficient. Keep the old private key backed up until migration is verified.

The clock firmware also contains a local five-point mindfulness check-in, an offline response queue and a JPEG-frame video player with audio. The portal retains responses for 30 days. Both portals prepare videos in the browser; the clock downloads remote uploads to its SD card. Type `checkin` in the serial monitor to show the prompt. See the portal README for limits and details.

Alarm and mood-video speaker levels can be set independently from the admin or LAN settings pages. Local edits take effect immediately and take priority while syncing back to the portal. See the portal README for synchronization details.

Application debug output uses a fixed 8 KB PSRAM queue and a separate low-priority writer task. UI, portal and audio callers only enqueue; if USB is slow or no reader is consuming output, new records are dropped instead of blocking those tasks. Each record is capped at 383 bytes; long debug messages are truncated. Serial commands still read directly from USB. `[uistats]` includes queued bytes and dropped-record counts. The logging task reserves a 3 KB internal-RAM stack. `python3 tools/check_debug_log.py` verifies full-queue producers never touch the USB writer and checks queue wraparound. When validating an apparent freeze, include a period with the USB reader closed: continuous logging can mask backpressure stalls.

USB memory soak tests: send `memstats` at 115200 baud for a read-only JSON snapshot of uptime, free/minimum internal heap, largest internal block, free/minimum/largest PSRAM, LVGL pool free/largest space and fragmentation, reset reason and RSSI. It does not change cloud reporting. `python3 soak-tests/usb_collect.py` captures serial locally for 24 hours and requests one snapshot every 60 seconds; keep USB connected and the Mac awake. Minimum values are lifetime low-water marks, not current usage. Compare current free space and largest blocks after warm-up across similar workloads; USB reconnections and falling uptime indicate interruptions/reboots. The same command also emits `[uistats]`: current mode, page offset, actual scroll/animation activity, the UI pause flag, recovered interrupted-animation count and decoded Focus artwork bytes. A cancelled animation before its first LVGL tick can omit its scroll-end event; firmware reconciles the UI flag with actual input/animation state each loop so this cannot leave updates paused indefinitely. For a bounded interactive capture, use `python3 soak-tests/usb_collect.py --duration 900 --interval 10` (seconds; minimum interval 5). Defaults remain 24 hours/60 seconds. Preserve captures across resets/flashes as separate firmware runs.

Audio uses 16 × 256 frames of 32-bit stereo I2S DMA (32 KB internal RAM, about 85 ms at 48 kHz) rather than the library's 64 KB default. Video JPEG reads use at most 1 KB per SD request to bound DMA bounce-buffer demand while audio and TLS run. The USB soak exposed SD DMA allocation failures with the larger reserve; listen for audio underruns when validating this setting.

Unused audio-library EQ filters, VU metering and spectrum processing are disabled to leave more CPU time for simultaneous video and audio. Volume control remains enabled. Video frames taking at least their 200 ms budget to load and decode emit a `[video] slow frame` serial diagnostic. The user confirmed smooth audio and video at the previously affected low volume after this change; before it, levels 15 and below stuttered while 16–18 were smooth.

Full-screen mood-video playback releases the rendered clock face and its font caches before allocating video frames. Face refreshes remain suspended until playback ends, is touched away, is interrupted by an alarm, or fails; the current scheduled face is then rebuilt from cached portal assets. This saves PSRAM and animation work; the fixed LVGL pool remains allocated in PSRAM.
