# Bhoomi's Clock

Wi-Fi/NTP desk clock for the **Waveshare ESP32-S3-Touch-LCD-2.8** (ST7789 240×320, ESP32-S3R8, 16 MB flash, 8 MB PSRAM), using LVGL 9.

## Status

- [x] Wi-Fi setup via captive portal. The screen shows a QR code to join the open `Bhoomi-Clock` hotspot, and the phone gets a setup page.
- [x] Credentials are saved only after a successful join. The hotspot turns off once connected.
- [x] NTP sync (shown in UTC for now)
- [ ] Time zone, clock face, touch

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

## Files

| File | What |
|---|---|
| `BhoomiClock.ino` | setup/loop wiring |
| `wifi_setup.*` | non-blocking join / captive portal / credential storage |
| `portal_html.h` | the phone setup page |
| `display.*` | ST7789 over esp_lcd + LVGL glue, backlight (GPIO 5) |
| `ui.*` | LVGL screens |
| `lv_conf.h` | LVGL config |
