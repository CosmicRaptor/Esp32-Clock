#include "music_view.h"

#include <cstdio>
#include <cstring>

#include "display.h"
#include "music_player.h"

namespace musicview {
namespace {

const lv_color_t BASE = lv_color_hex(0x0E1116);
const lv_color_t INK = lv_color_hex(0xF4F6FA);
const lv_color_t MUTED = lv_color_hex(0xB7C1D0);
const lv_color_t SURFACE = lv_color_hex(0x262D36);
constexpr int PROGRESS_WIDTH = 288;

lv_obj_t *root;
lv_obj_t *tabs[2];
lv_obj_t *coverBox, *cover, *status, *title, *artist, *collection;
lv_obj_t *trackLine, *fill, *elapsed, *duration;
uint32_t shownAccent;
int selected;
Select chooseSide;

lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
  auto *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}

lv_obj_t *text(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, lv_color_t color) {
  auto *obj = lv_label_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_width(obj, w);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
  return obj;
}

lv_obj_t *button(const char *name, int x, int w, lv_event_cb_t callback, void *data = nullptr) {
  auto *hit = lv_button_create(root);
  lv_obj_remove_style_all(hit);
  lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(hit, x, 8);
  lv_obj_set_size(hit, w, 44);
  auto *pill = panel(hit, 0, 5, w, 34, 17);
  auto *caption = text(pill, 0, 0, w, &lv_font_montserrat_14, INK);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text_static(caption, name);
  lv_obj_center(caption);
  lv_obj_add_event_cb(hit, callback, LV_EVENT_SHORT_CLICKED, data);
  return pill;
}

void setText(lv_obj_t *label, const char *value) {
  if (strcmp(lv_label_get_text(label), value) != 0) lv_label_set_text(label, value);
}

void show(lv_obj_t *obj, bool visible) {
  if (visible != lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
  if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void setTime(lv_obj_t *label, uint32_t ms) {
  const unsigned seconds = ms / 1000;
  char value[16];
  snprintf(value, sizeof value, "%u:%02u", seconds / 60, seconds % 60);
  setText(label, value);
}

void paintTabs() {
  for (int i = 0; i < 2; ++i) {
    lv_obj_set_style_bg_color(tabs[i], i == selected ? lv_color_mix(lv_color_hex(shownAccent), BASE, 95) : SURFACE, 0);
  }
}

void paintAccent(uint32_t rgb) {
  shownAccent = rgb;
  const lv_color_t accent = lv_color_hex(rgb);
  const lv_color_t bright = lv_color_mix(accent, INK, 135);
  lv_obj_set_style_bg_color(root, lv_color_mix(accent, BASE, 34), 0);
  lv_obj_set_style_bg_color(coverBox, lv_color_mix(accent, BASE, 86), 0);
  lv_obj_set_style_bg_color(fill, bright, 0);
  lv_obj_set_style_text_color(status, bright, 0);
  paintTabs();
}

}  // namespace

bool active() { return root != nullptr; }

void open(lv_obj_t *parent, int side, Select select) {
  if (root) {
    selected = side;
    paintTabs();
    return;
  }
  selected = side;
  chooseSide = select;
  root = panel(parent, 0, 0, display::WIDTH, display::HEIGHT, 0);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);  // full-screen overlay consumes touches
  auto *back = button(LV_SYMBOL_LEFT " Clock", 12, 84, [](lv_event_t *) { close(); });
  lv_obj_set_style_bg_color(back, SURFACE, 0);
  const char *names[] = {"Aryan", "Bhoomi"};
  for (int i = 0; i < 2; ++i) {
    tabs[i] = button(names[i], 150 + i * 79, 75, [](lv_event_t *event) {
      chooseSide(static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
    }, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }

  coverBox = panel(root, 16, 68, 112, 112, 13);
  lv_obj_set_style_clip_corner(coverBox, true, 0);
  auto *placeholder = text(coverBox, 0, 0, 112, &lv_font_montserrat_28, INK);
  lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text_static(placeholder, LV_SYMBOL_AUDIO);
  lv_obj_center(placeholder);
  cover = lv_image_create(coverBox);
  lv_obj_set_size(cover, 112, 112);
  lv_image_set_inner_align(cover, LV_IMAGE_ALIGN_STRETCH);
  show(cover, false);

  status = text(root, 145, 80, 159, &lv_font_montserrat_14, MUTED);
  title = text(root, 145, 104, 159, &lv_font_montserrat_20, INK);
  artist = text(root, 145, 133, 159, &lv_font_montserrat_16, INK);
  collection = text(root, 145, 157, 159, &lv_font_montserrat_14, MUTED);
  for (auto *label : {title, artist, collection}) lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);

  trackLine = panel(root, 16, 199, PROGRESS_WIDTH, 5, 3);
  lv_obj_set_style_bg_color(trackLine, SURFACE, 0);
  fill = panel(trackLine, 0, 0, 0, 5, 3);
  elapsed = text(root, 16, 213, 128, &lv_font_montserrat_14, MUTED);
  duration = text(root, 176, 213, 128, &lv_font_montserrat_14, MUTED);
  lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
  paintAccent(music::DEFAULT_ACCENT);
  lv_obj_move_foreground(root);
}

void close() {
  if (!root) return;
  lv_obj_delete(root);
  root = nullptr;
}

void detachCover(const lv_image_dsc_t *art) {
  if (!root || lv_image_get_src(cover) != art) return;
  lv_image_set_src(cover, nullptr);
  show(cover, false);
}

void update(const bhoomi_clock_v1_Listener *track, const lv_image_dsc_t *art, uint32_t accent, uint32_t sampleAge) {
  if (!root) return;
  if (!art) accent = music::DEFAULT_ACCENT;
  if (shownAccent != accent) paintAccent(accent);
  if (lv_image_get_src(cover) != art) {
    lv_image_set_src(cover, art);
    show(cover, art != nullptr);
  }
  const bool playing = track && track->status == bhoomi_clock_v1_Listener_Status_PLAYING;
  setText(status, track ? (playing ? "Playing" : "Paused") : "");
  setText(title, track ? track->title : "Nothing playing");
  setText(artist, track ? track->artist : "");
  setText(collection, track ? track->collection : "");

  const bool hasDuration = track && track->duration_ms;
  const bool hasPosition = hasDuration && track->has_position;
  show(trackLine, hasPosition);
  show(elapsed, hasPosition);
  show(duration, hasDuration);
  if (hasDuration) setTime(duration, track->duration_ms);
  if (hasPosition) {
    const bool advance = playing && track->position.sampled_at_ms;
    const uint32_t position = music::elapsedMs(track->position.elapsed_ms, track->duration_ms, advance ? sampleAge : 0);
    const int width = uint64_t(position) * PROGRESS_WIDTH / track->duration_ms;
    if (lv_obj_get_style_width(fill, LV_PART_MAIN) != width) lv_obj_set_width(fill, width);
    setTime(elapsed, position);
  }
}

}  // namespace musicview
