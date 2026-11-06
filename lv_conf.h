// LVGL 9 configuration for Bhoomi's clock.
// Arduino can't point a library at a sketch-local config, so build.sh copies this file next to
// the lvgl library folder (~/Documents/Arduino/libraries/lv_conf.h). Edit it here, not there.
// Anything not set here falls back to LVGL's defaults (lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// LVGL's own allocator, with its pool in PSRAM. (The C library heap puts every allocation of 4 KB or
// less in the scarce internal RAM, and the big clock digits are cached as ~4 KB glyph bitmaps: each
// new digit seen at a minute rollover used up internal RAM until Wi-Fi and the web server starved.)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (384 * 1024U)
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16

#define LV_USE_LOG 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_USE_QRCODE 1

// Big clock digits: render the embedded Montserrat TTF at any size on the device.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0

// Not needed; keeps the build smaller and faster.
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif
