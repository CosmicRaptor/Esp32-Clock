#pragma once
#include <Arduino.h>

// Capacitive touch on the Waveshare ESP32-S3-Touch-LCD-2.8. Waveshare ships two chips on this
// board (CST328 on older units, CST3530 on newer ones); begin() finds out which one is there.
// It sits on its own I2C bus (Wire1: SDA 1, SCL 3, INT 4, RST 2), separate from the RTC/IMU bus.
//
// The touch is registered with LVGL as a pointer device, so ordinary LVGL events (PRESSED, CLICKED)
// just work. Call begin() after display::begin(). Reads happen inside lv_timer_handler(), i.e. under
// app::Lock in the main loop; nothing else touches the chip.
namespace touch {

bool begin();  // false if no touch controller answered (the clock still works, without touch)
bool available();

}  // namespace touch
