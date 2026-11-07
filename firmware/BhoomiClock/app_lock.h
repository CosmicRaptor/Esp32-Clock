#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// One lock for app state (Wi-Fi state machine, settings, LVGL objects). The main loop holds it
// while it runs; the web task takes it only around handlers that touch that state. File transfers
// don't take it, so the screen keeps updating during uploads.
namespace app {

inline SemaphoreHandle_t mutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}

struct Lock {
  Lock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mutex()); }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

}  // namespace app
