#include "video_player.h"
#include "storage.h"
#include "sound.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>
#include <Arduino.h>

namespace video_player {
namespace {
lv_obj_t *view, *image;
lv_image_dsc_t dsc[2] = {};
uint8_t *pixels[2] = {};
int active = -1;
unsigned selected, frame;
uint32_t started, lastFrame;
bool running = false;
constexpr uint32_t FRAME_MS = 200, MAX_MS = 20000;
constexpr size_t MAX_JPEG = 100000, PIXELS = 320 * 240 * 2;

bool nextFrame() {
  char path[64];
  snprintf(path, sizeof(path), "/mindfulness/%u/%04u.jpg", selected, frame);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || !f.size() || f.size() > MAX_JPEG) return false;
  size_t n = f.size();
  uint8_t *jpg = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!jpg) return false;
  bool read = f.read(jpg, n) == n;
  f.close();
  int next = active == 0 ? 1 : 0;
  if (!pixels[next]) pixels[next] = (uint8_t *)heap_caps_malloc(PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = jpg; cfg.indata_size = n; cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  esp_jpeg_image_output_t info = {};
  bool ok = read && pixels[next] && esp_jpeg_get_image_info(&cfg, &info) == ESP_OK &&
            info.width == 320 && info.height == 240 && info.output_len <= PIXELS;
  if (ok) { cfg.outbuf = pixels[next]; cfg.outbuf_size = PIXELS; ok = esp_jpeg_decode(&cfg, &info) == ESP_OK; }
  free(jpg);
  if (!ok) return false;
  auto &d = dsc[next]; memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC; d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = 320; d.header.h = 240; d.header.stride = 640; d.data_size = info.output_len; d.data = pixels[next];
  lv_image_cache_drop(&d);
  lv_image_set_src(image, &d);
  active = next; ++frame; return true;
}
}
void begin() {
  view = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(view);
  lv_obj_set_size(view, 320, 240); lv_obj_set_style_bg_color(view, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0); lv_obj_remove_flag(view, LV_OBJ_FLAG_SCROLLABLE);
  image = lv_image_create(view); lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(view, [](lv_event_t *) { stop(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
}
bool play(unsigned mood) {
  if (!storage::mounted() || mood < 1 || mood > 5) return false;
  selected = mood; frame = 0; active = -1;
  if (!nextFrame()) { stop(); return false; }
  running = true; started = lastFrame = millis();
  lv_obj_remove_flag(view, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(view);
  char path[48]; snprintf(path, sizeof(path), "/mindfulness/%u/audio.wav", mood);
  if (SD_MMC.exists(path)) sound::playClip(path);
  return true;
}
void stop() {
  if (running) sound::stop();
  running = false;
  if (view) lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
  if (image) lv_image_set_src(image, nullptr);
  for (int i = 0; i < 2; ++i) { lv_image_cache_drop(&dsc[i]); free(pixels[i]); pixels[i] = nullptr; }
  active = -1;
}
bool playing() { return running; }
void tick() {
  if (!running) return;
  uint32_t now = millis();
  if (now - started >= MAX_MS) { stop(); return; }
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    unsigned target = (now - started) / FRAME_MS;
    if (target > frame) frame = target;  // Drop late frames so picture follows the audio clock.
    if (!nextFrame()) stop();
  }
}
}
