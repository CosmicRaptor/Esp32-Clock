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
- [ ] Clock face design polish

## Build & flash

Needs the Arduino IDE (its bundled `arduino-cli` is used), the `esp32:esp32` core 3.x, the `lvgl@9.2.2` and `ESP32-audioI2S-master@3.4.7` libraries (`arduino-cli lib install ESP32-audioI2S-master@3.4.7`), and the Nanopb 0.4.9.1 runtime in `~/Documents/Arduino/libraries/Nanopb/` (not in the Library Manager index; see `portal/README.md`, "Notes").

To talk to the cloud portal, copy `portal_config.example.h` to `portal_config.h` (gitignored) and set `PORTAL_URL` and `PORTAL_DEVICE_TOKEN`. Without it the sketch still builds and the portal client stays off.

```bash
./build.sh          # compile
./build.sh flash    # compile + upload to the first /dev/cu.usbmodem* port
```

`build.sh` copies `lv_conf.h` next to the LVGL library, because Arduino can't point a library at a config file inside the sketch. Always edit the copy in this repo.

If uploads fail with "No serial data received", close the Arduino IDE Serial Monitor. Or hold **BOOT**, replug USB, and release.

## Using it

- **First boot / no saved Wi-Fi:** scan the on-screen QR code (or join `Bhoomi-Clock`), then pick your network on the page that pops up.
- **Saved network unreachable at power-on:** the setup hotspot comes back after about 20 s. The clock keeps retrying the saved network every minute.
- **Forget Wi-Fi:** hold **BOOT** for 5 s.
- **Change the time zone:** open http://bhoomi-clock.local (or the clock's IP) from any device on the same Wi-Fi.
- **Add photos/sounds:** same page, "Photos & sounds" section. Files go to `/photos` and `/sounds` on the card. The card must be FAT32, and the firmware never formats it. Uploads are slow on a weak signal (about 50 KB/s at −90 dBm), so resized photos (~10–40 KB) are quick but big sound files take a while.
- **Alarms:** set them in the portal. At the alarm time the screen turns pink and pulses, and the sound repeats until you touch the screen (or press **BOOT**). It stops by itself after 5 minutes. The sound is the file picked in the portal from `/sounds` (mp3, m4a, aac, flac, ogg/opus, wav). No pick, no card, a missing file, or a file that won't decode all fall back to the built-in beep (kept in the internal flash, so it works without a card). Try it without waiting: type `ring` (or `ring <file name>`) in the serial monitor at 115200 baud.
- **Screen upside down?** Set `FLIP_180` in `display.cpp`.

## HTTPS

A deployed `https://…workers.dev` portal works: certificates are checked against the built-in Mozilla CA bundle, and the TLS connection is kept alive between the 10 s polls (the first handshake takes ~2 s on weak Wi-Fi, later polls ~60 ms). The clock needs NTP to have synced first, so it waits before the first HTTPS request.

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
| `BhoomiClock.ino` | setup/loop wiring, NTP |
| `wifi_setup.*` | non-blocking join / captive portal + DNS / credential storage |
| `web.*` | HTTP server (own task) and mDNS (`bhoomi-clock.local`); file list/upload/download/delete |
| `page_html.h` | the single page: Wi-Fi + time zone (setup) or time zone + photos & sounds (settings) |
| `storage.*` | microSD mount (never formats), folder and file-name rules |
| `portal_client.*` | cloud portal client (own task): polls the state every 10 s over one kept-alive connection (protobuf, ETag; it carries what's playing), downloads the photo and the 64×64 covers, PUTs the `/sounds` list. Keeps a copy of the last state in internal flash (`/state.pb`, FFat) and the photo on the SD card (`/portal/photo.jpg`), restored at boot |
| `clock.pb.*` | generated nanopb code (`proto/generate.sh`) |
| `app_lock.h` | the lock shared by the main loop and the web task |
| `timezones.*` | curated IANA → POSIX TZ table |
| `settings.*` | saved time zone (NVS) |
| `display.*` | ST7789 over esp_lcd + LVGL glue, landscape rotation, backlight (GPIO 5) |
| `sound.*` | alarm audio: ESP32-audioI2S decoder + I2S out, own task; loops a `/sounds` file or the built-in beep (generated into flash on first boot) |
| `touch.*` | CST328 / CST3530 touch chip (auto-detected) registered as an LVGL pointer device |
| `ui.*` | LVGL screens: setup (QR), status, clock, alarm overlay |
| `font_montserrat_ttf.c` | Montserrat Medium (OFL), rendered by LVGL Tiny TTF for the big digits |
| `lv_conf.h` | LVGL config. LVGL's memory pool lives in PSRAM: with the C heap, each new big clock digit (a ~4 KB glyph bitmap) used up internal RAM until Wi-Fi starved |
| `proto/` | `clock.proto`: the clock ↔ portal protobuf schema, plus the generated nanopb code (`proto/generate.sh`) |
| `portal/` | admin portal (Cloudflare Worker, Python) for the photo, message and alarms; see `portal/README.md` |
