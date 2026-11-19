#pragma once
#include <stdint.h>
namespace gardenart {
// Caller owns the 320x240 RGB565 PSRAM buffer.
uint8_t *load(unsigned sessions);
}
