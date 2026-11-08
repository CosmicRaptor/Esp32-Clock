#pragma once
#include <Arduino.h>
#include <time.h>
namespace mindfulness {
struct Record { uint32_t at; uint32_t mood; };
void begin();
bool enqueue(uint32_t at, uint32_t mood);
bool peek(Record &out);
bool acknowledge(const Record &record);
bool shouldPrompt(time_t now, const struct tm &local);
}
