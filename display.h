#pragma once
#include <Arduino.h>

// ST7789 240x320 SPI panel on the Waveshare ESP32-S3-Touch-LCD-2.8, driven through esp_lcd,
// with LVGL 9 on top. Call display::begin() once, then display::loop() from loop().
namespace display {

// Landscape, as the board sits in the stand: USB-C on the viewer's right, side buttons on top.
// If the picture is upside down, flip FLIP_180 in display.cpp.
constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;

void begin();
void loop();
void setBacklight(uint8_t percent);

}  // namespace display
