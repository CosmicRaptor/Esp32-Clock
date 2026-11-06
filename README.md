# Bhoomi's Clock

Wi-Fi/NTP desk clock for the **Waveshare ESP32-S3-Touch-LCD-2.8** (ST7789 240×320, ESP32-S3R8, 16 MB flash, 8 MB PSRAM), using LVGL 9.

## Status

- [x] Wi-Fi setup via captive portal. The screen shows a QR code to join the open `Bhoomi-Clock` hotspot, and the phone gets a setup page.
- [x] Credentials are saved only after a successful join. The hotspot turns off once connected.
- [x] Time zone picker on the setup page, preselected from the phone. It can be changed later at http://bhoomi-clock.local.
- [x] NTP sync, local time shown in landscape (the stand: USB-C on the right)
- [x] Photos & sounds on the microSD card. Drag and drop them on the settings page. Photos are resized in the browser to fit 320×240 (JPEG, EXIF rotation applied), and sounds upload as-is.
- [ ] Clock face design, showing photos, playing sounds, touch

## Build & flash

Needs the Arduino IDE (its bundled `arduino-cli` is used), the `esp32:esp32` core 3.x, and the `lvgl@9.2.2` library.

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
- **Screen upside down?** Set `FLIP_180` in `display.cpp`.

## Pins (from Waveshare's demo code)

| What | GPIO |
|---|---|
| LCD (ST7789, SPI) | SCLK 40, MOSI 45, CS 42, DC 41, RST 39, backlight 5 |
| microSD (SD_MMC 1-bit) | CLK 14, CMD 17, D0 16, D3 21 (held high) |
| I2S speaker amp (PCM5101) | BCLK 48, LRC 38, DOUT 47 |
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
| `app_lock.h` | the lock shared by the main loop and the web task |
| `timezones.*` | curated IANA → POSIX TZ table |
| `settings.*` | saved time zone (NVS) |
| `display.*` | ST7789 over esp_lcd + LVGL glue, landscape rotation, backlight (GPIO 5) |
| `ui.*` | LVGL screens: setup (QR), status, clock |
| `font_montserrat_ttf.c` | Montserrat Medium (OFL), rendered by LVGL Tiny TTF for the big digits |
| `lv_conf.h` | LVGL config |
| `proto/` | `clock.proto`: the clock ↔ portal protobuf schema, plus the generated nanopb code (`proto/generate.sh`) |
| `portal/` | admin portal (Cloudflare Worker, Python) for the photo, message and alarms; see `portal/README.md` |
