#include "touch.h"

#include <Wire.h>
#include <lvgl.h>

#include <algorithm>

#include "display.h"

namespace touch {
namespace {

constexpr int PIN_SDA = 1;
constexpr int PIN_SCL = 3;
constexpr int PIN_INT = 4;
constexpr int PIN_RST = 2;
constexpr uint32_t I2C_HZ = 400000;

constexpr uint8_t CST328_ADDR = 0x1A;
constexpr uint8_t CST3530_ADDR = 0x58;

// The chip reports in the panel's native frame (x 0..239, y 0..319, portrait). The display is
// driven swap_xy + mirror (see display.cpp); the touch follows the same transform, as in
// esp_lcd_touch: mirror in the native frame first, then swap.
constexpr int NATIVE_W = 240, NATIVE_H = 320;
constexpr bool SWAP_XY = true;
constexpr bool MIRROR_X = !display::FLIP_180;
constexpr bool MIRROR_Y = display::FLIP_180;

enum class Chip { None, CST328, CST3530 };
Chip chip = Chip::None;
int missed = 0;  // consecutive polls with no finger; the chip is slower than our polling, so debounce

// ---- CST328: 16-bit register addresses ----
bool read328(uint16_t reg, uint8_t *dst, size_t n) {
  Wire1.beginTransmission(CST328_ADDR);
  Wire1.write((uint8_t)(reg >> 8));
  Wire1.write((uint8_t)reg);
  if (Wire1.endTransmission(true)) return false;
  if (Wire1.requestFrom((uint8_t)CST328_ADDR, (size_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) dst[i] = Wire1.read();
  return true;
}

bool write328(uint16_t reg, const uint8_t *src, size_t n) {
  Wire1.beginTransmission(CST328_ADDR);
  Wire1.write((uint8_t)(reg >> 8));
  Wire1.write((uint8_t)reg);
  for (size_t i = 0; i < n; i++) Wire1.write(src[i]);
  return Wire1.endTransmission(true) == 0;
}

bool probe328() {
  digitalWrite(PIN_RST, HIGH);
  delay(50);
  digitalWrite(PIN_RST, LOW);
  delay(5);
  digitalWrite(PIN_RST, HIGH);
  delay(50);

  uint8_t buf[24] = {0};
  if (!write328(0xD101, buf, 0)) return false;       // debug-info mode
  if (!read328(0xD1F4, buf, sizeof buf)) return false;
  bool ok = (((uint16_t)buf[11] << 8) | buf[10]) == 0xCACA;  // boot-time register holds this marker
  write328(0xD109, buf, 0);                          // back to normal mode
  return ok;
}

bool read328Point(uint16_t &x, uint16_t &y) {
  uint8_t buf[41] = {0};
  uint8_t zero = 0;
  if (!read328(0xD005, buf, 1)) return false;
  uint8_t n = buf[0] & 0x0F;
  if (n == 0 || n > 5) {
    write328(0xD005, &zero, 1);
    return false;
  }
  bool ok = read328(0xD000, &buf[1], 27);
  write328(0xD005, &zero, 1);  // tell the chip we've read it
  if (!ok) return false;
  x = ((uint16_t)buf[2] << 4) | (buf[4] >> 4);
  y = ((uint16_t)buf[3] << 4) | (buf[4] & 0x0F);
  return true;
}

// ---- CST3530: 32-bit register addresses ----
constexpr uint32_t REG3530_DATA = 0xD0070000;
constexpr uint32_t REG3530_END = 0xD00002AB;

bool read3530(uint32_t reg, uint8_t *dst, size_t n) {
  Wire1.beginTransmission(CST3530_ADDR);
  for (int s = 24; s >= 0; s -= 8) Wire1.write((uint8_t)(reg >> s));
  if (Wire1.endTransmission(false)) return false;
  if (Wire1.requestFrom((uint8_t)CST3530_ADDR, (size_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) dst[i] = Wire1.read();
  return true;
}

void end3530() {
  Wire1.beginTransmission(CST3530_ADDR);
  for (int s = 24; s >= 0; s -= 8) Wire1.write((uint8_t)(REG3530_END >> s));
  Wire1.endTransmission(true);
}

bool probe3530() {
  digitalWrite(PIN_RST, LOW);
  delay(100);
  digitalWrite(PIN_RST, HIGH);
  delay(500);
  Wire1.beginTransmission(CST3530_ADDR);
  return Wire1.endTransmission(true) == 0;
}

bool read3530Point(uint16_t &x, uint16_t &y) {
  uint8_t buf[9] = {0};
  if (!read3530(REG3530_DATA, buf, sizeof buf)) return false;
  if ((buf[3] & 0x0F) == 0 || (buf[8] & 0xF0) == 0) {
    end3530();
    return false;
  }
  end3530();
  x = ((buf[7] & 0x0F) << 8) | buf[4];
  y = ((buf[7] & 0xF0) << 4) | buf[5];
  return true;
}

// ---- LVGL glue ----
void readCb(lv_indev_t *, lv_indev_data_t *data) {
  static int lastX = 0, lastY = 0;
  uint16_t rx = 0, ry = 0;
  bool down = chip == Chip::CST328 ? read328Point(rx, ry) : read3530Point(rx, ry);

  if (down) {
    int x = min<int>(rx, NATIVE_W - 1), y = min<int>(ry, NATIVE_H - 1);
    if (MIRROR_X) x = NATIVE_W - 1 - x;
    if (MIRROR_Y) y = NATIVE_H - 1 - y;
    if (SWAP_XY) std::swap(x, y);
    lastX = constrain(x, 0, display::WIDTH - 1);
    lastY = constrain(y, 0, display::HEIGHT - 1);
    if (missed >= 3) Serial.printf("[touch] down raw (%u,%u) -> screen (%d,%d)\n", rx, ry, lastX, lastY);
    missed = 0;
  } else if (missed < 1000) {
    missed++;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = missed < 3 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

bool begin() {
  Wire1.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  pinMode(PIN_INT, INPUT);
  pinMode(PIN_RST, OUTPUT);

  if (probe328()) chip = Chip::CST328;
  else if (probe3530()) chip = Chip::CST3530;
  if (chip == Chip::None) {
    Serial.println("[touch] no touch controller found; the screen won't react to touch");
    return false;
  }
  Serial.printf("[touch] %s found\n", chip == Chip::CST328 ? "CST328" : "CST3530");

  missed = 1000;  // start released
  lv_indev_t *dev = lv_indev_create();
  lv_indev_set_type(dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(dev, readCb);
  return true;
}

bool available() { return chip != Chip::None; }

}  // namespace touch
