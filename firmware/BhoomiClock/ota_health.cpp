#include "ota_health.h"
#include <Arduino.h>
#include <atomic>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include "portal_client.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "OTA requires bootloader rollback support"
#endif

// Arduino otherwise accepts a new image before setup() has even run.
extern "C" bool verifyRollbackLater() { return true; }
namespace otahealth {
static std::atomic<bool> verifying{false};
static uint32_t started = 0, ticks = 0;
static esp_timer_handle_t deadline;
static void expired(void *) { esp_ota_mark_app_invalid_rollback_and_reboot(); }
void begin() {
  esp_ota_img_states_t state;
  verifying = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  if (!verifying) return;
  started = millis();
  // Independent timer also catches a stuck setup or UI loop. No network dependency for recovery.
  esp_timer_create_args_t args = {};
  args.callback = expired;
  args.name = "ota-health";
  if (esp_timer_create(&args, &deadline) != ESP_OK || esp_timer_start_once(deadline, 180000000) != ESP_OK)
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
bool pending() { return verifying; }
void tick() {
  if (!verifying) return;
  ++ticks;
  if (millis() - started < 60000 || ticks < 1000) return;
  // Confirm a live main loop and authenticated cloud connection, not just a successful boot.
  if (!portal::healthy()) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    esp_timer_stop(deadline);
    verifying = false;
    Serial.println("[ota] startup health checks passed");
  }
}
}
