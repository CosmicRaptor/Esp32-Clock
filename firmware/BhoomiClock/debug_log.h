#pragma once
#include <stddef.h>
#include <stdint.h>
namespace debuglog {
// After Serial.begin; allocation/task failure disables logs, never the clock.
bool begin();
void printf(const char *format, ...) __attribute__((format(printf,1,2)));
void print(const char *text);
void println(const char *text="");
uint32_t dropped();
size_t buffered();
}
