#include "ui.h"

#include <lvgl.h>
#include <time.h>

namespace ui {
namespace {

using wifisetup::Phase;

// Palette (warm, soft)
const lv_color_t BG = lv_color_hex(0xFBF7F2);
const lv_color_t INK = lv_color_hex(0x2B2420);
const lv_color_t MUTE = lv_color_hex(0x8A7F78);
const lv_color_t ACCENT = lv_color_hex(0xD9667A);
const lv_color_t OK = lv_color_hex(0x3A8F5C);
const lv_color_t ERR = lv_color_hex(0xC0392B);

String ap;

lv_obj_t *scr;
// "join my network" panel
lv_obj_t *setupBox, *qr, *apLabel, *hint;
// spinner + message panel (joining / online)
lv_obj_t *msgBox, *spinner, *bigIcon, *msgTitle, *msgBody, *timeLabel;

bool showClock = false;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

lv_obj_t *column(lv_obj_t *parent) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_hor(c, 16, 0);
  lv_obj_set_style_pad_ver(c, 18, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 10, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

void buildSetup() {
  setupBox = column(scr);

  lv_obj_t *t = label(setupBox, &lv_font_montserrat_20, INK);
  lv_label_set_text(t, "Hi! Please help me\nget online");

  lv_obj_t *s = label(setupBox, &lv_font_montserrat_14, MUTE);
  lv_label_set_text(s, "Scan with your phone camera,\nor join this Wi-Fi:");

  qr = lv_qrcode_create(setupBox);
  lv_qrcode_set_size(qr, 116);
  lv_qrcode_set_dark_color(qr, INK);
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 6, 0);
  String payload = "WIFI:T:nopass;S:" + ap + ";;";
  lv_qrcode_update(qr, payload.c_str(), payload.length());

  // Network name in a pill
  lv_obj_t *pill = lv_obj_create(setupBox);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(pill, ACCENT, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(pill, 16, 0);
  lv_obj_set_style_pad_ver(pill, 6, 0);
  apLabel = lv_label_create(pill);
  lv_obj_set_style_text_font(apLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(apLabel, lv_color_white(), 0);
  lv_label_set_text_fmt(apLabel, LV_SYMBOL_WIFI "  %s", ap.c_str());

  hint = label(setupBox, &lv_font_montserrat_14, MUTE);
}

void buildMsg() {
  msgBox = column(scr);
  lv_obj_set_style_pad_row(msgBox, 14, 0);

  spinner = lv_spinner_create(msgBox);
  lv_obj_set_size(spinner, 56, 56);
  lv_obj_set_style_arc_color(spinner, ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0xEFE6DE), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);

  bigIcon = label(msgBox, &lv_font_montserrat_48, OK);
  lv_label_set_text(bigIcon, LV_SYMBOL_OK);

  msgTitle = label(msgBox, &lv_font_montserrat_20, INK);
  msgBody = label(msgBox, &lv_font_montserrat_14, MUTE);

  timeLabel = label(msgBox, &lv_font_montserrat_28, INK);
}

void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void updateClock() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    lv_label_set_text(timeLabel, "getting the time...");
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_14, 0);
    return;
  }
  struct tm t;
  gmtime_r(&now, &t);
  lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_28, 0);
  lv_label_set_text_fmt(timeLabel, "%02d:%02d:%02d UTC", t.tm_hour, t.tm_min, t.tm_sec);
}

}  // namespace

void begin(const char *apName) {
  ap = apName;
  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  buildSetup();
  buildMsg();
  show(setupBox, false);
  show(msgBox, true);
  show(bigIcon, false);
  show(timeLabel, false);
  lv_label_set_text(msgTitle, "Waking up...");
  lv_label_set_text(msgBody, "");
}

void showWifi(const wifisetup::Status &s) {
  bool setup = s.phase == Phase::Portal || s.phase == Phase::PortalConnecting ||
               s.phase == Phase::PortalFailed;
  show(setupBox, setup);
  show(msgBox, !setup);
  showClock = false;

  switch (s.phase) {
    case Phase::Portal:
      lv_obj_set_style_text_color(hint, MUTE, 0);
      lv_label_set_text(hint, "A page will pop up.\nPick your home Wi-Fi there.");
      break;
    case Phase::PortalConnecting:
      lv_obj_set_style_text_color(hint, ACCENT, 0);
      lv_label_set_text_fmt(hint, "Connecting to\n%s ...", s.ssid.c_str());
      break;
    case Phase::PortalFailed:
      lv_obj_set_style_text_color(hint, ERR, 0);
      lv_label_set_text_fmt(hint, "%s\nPlease try again.", s.detail.c_str());
      break;
    case Phase::JoiningSaved:
    case Phase::Reconnecting:
      show(spinner, true);
      show(bigIcon, false);
      show(timeLabel, false);
      lv_label_set_text(msgTitle, s.phase == Phase::JoiningSaved ? "Connecting..." : "Reconnecting...");
      lv_label_set_text(msgBody, s.ssid.c_str());
      break;
    case Phase::Online:
      show(spinner, false);
      show(bigIcon, true);
      show(timeLabel, true);
      lv_label_set_text(msgTitle, "I'm online!");
      lv_label_set_text_fmt(msgBody, "%s\n%s", s.ssid.c_str(), s.detail.c_str());
      showClock = true;
      updateClock();
      break;
  }
}

void loop() {
  static uint32_t last = 0;
  if (showClock && millis() - last >= 250) {
    last = millis();
    updateClock();
  }
}

}  // namespace ui
