#pragma once
#include <Arduino.h>

// ST7789 240x320 SPI panel on the Waveshare ESP32-S3-Touch-LCD-2.8, driven through esp_lcd,
// with LVGL 9 on top. Call display::begin() once, then display::loop() from loop().
namespace display {

constexpr int WIDTH = 240;
constexpr int HEIGHT = 320;

void begin();
void loop();
void setBacklight(uint8_t percent);

}  // namespace display
