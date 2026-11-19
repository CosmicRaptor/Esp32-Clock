#pragma once
#include <stdint.h>
namespace focusart {
// Managed, versioned SD asset. No user photo is replaced.
bool install();
// Returns a 320x240 RGB565 PSRAM buffer; caller frees it after deleting its image.
uint8_t *load();
}
