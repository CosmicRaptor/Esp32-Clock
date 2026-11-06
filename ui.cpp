#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include "settings.h"
#include "web.h"

extern "C" const uint8_t montserrat_medium_ttf[];
extern "C" const size_t montserrat_medium_ttf_len;

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

constexpr uint32_t SPLASH_MS = 2500;  // "I'm online!" before switching to the clock

String ap;
lv_font_t *bigFont = nullptr;

lv_obj_t *scr;
// "join my network" view
lv_obj_t *setupView, *hint;
// spinner + message view (waking / joining / online splash)
lv_obj_t *msgView, *spinner, *bigIcon, *msgTitle, *msgBody;
// clock view
lv_obj_t *clockView, *dateLabel, *timeLabel, *footLabel;

wifisetup::Phase phase = Phase::JoiningSaved;
uint32_t onlineAt = 0;

bool timeValid() { return time(nullptr) > 1700000000; }

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

lv_obj_t *box(lv_obj_t *parent, lv_flex_flow_t flow) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_flex_flow(c, flow);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t *fullColumn(lv_obj_t *parent) {
  lv_obj_t *c = box(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(c, lv_pct(100), lv_pct(100));
  lv_obj_set_style_pad_all(c, 14, 0);
  lv_obj_set_style_pad_row(c, 10, 0);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return c;
}

void show(lv_obj_t *o, bool on) {
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ---------- setup view: title on top, QR left, instructions right ----------
void buildSetup() {
  setupView = fullColumn(scr);
  lv_obj_set_style_pad_row(setupView, 12, 0);

  lv_obj_t *t = label(setupView, &lv_font_montserrat_20, INK);
  lv_label_set_text(t, "Hi! Please help me get online");

  lv_obj_t *row = box(setupView, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(row, 14, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *qr = lv_qrcode_create(row);
  lv_qrcode_set_size(qr, 112);
  lv_qrcode_set_dark_color(qr, INK);
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 6, 0);
  String payload = "WIFI:T:nopass;S:" + ap + ";;";
  lv_qrcode_update(qr, payload.c_str(), payload.length());

  lv_obj_t *col = box(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(col, 156, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(col, 8, 0);

  lv_obj_t *s = label(col, &lv_font_montserrat_14, MUTE, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(s, "Scan with your phone camera, or join:");

  lv_obj_t *pill = lv_obj_create(col);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(pill, ACCENT, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(pill, 10, 0);
  lv_obj_set_style_pad_ver(pill, 5, 0);
  lv_obj_t *apLabel = lv_label_create(pill);
  lv_obj_set_style_text_font(apLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(apLabel, lv_color_white(), 0);
  lv_label_set_text_fmt(apLabel, LV_SYMBOL_WIFI " %s", ap.c_str());

  hint = label(col, &lv_font_montserrat_14, MUTE, LV_TEXT_ALIGN_LEFT);
}

// ---------- message view ----------
void buildMsg() {
  msgView = fullColumn(scr);
  lv_obj_set_style_pad_row(msgView, 12, 0);

  spinner = lv_spinner_create(msgView);
  lv_obj_set_size(spinner, 52, 52);
  lv_obj_set_style_arc_color(spinner, ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0xEFE6DE), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);

  bigIcon = label(msgView, &lv_font_montserrat_48, OK);
  lv_label_set_text(bigIcon, LV_SYMBOL_OK);

  msgTitle = label(msgView, &lv_font_montserrat_20, INK);
  msgBody = label(msgView, &lv_font_montserrat_14, MUTE);
}

// ---------- clock view ----------
void buildClock() {
  clockView = fullColumn(scr);
  lv_obj_set_style_pad_row(clockView, 0, 0);
  lv_obj_set_flex_align(clockView, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  dateLabel = label(clockView, &lv_font_montserrat_20, MUTE);
  timeLabel = label(clockView, bigFont ? bigFont : &lv_font_montserrat_48, INK);
  lv_label_set_long_mode(timeLabel, LV_LABEL_LONG_CLIP);
  footLabel = label(clockView, &lv_font_montserrat_14, MUTE);
}

void updateFooter() {
  const tz::Zone *z = settings::timezone();
  if (phase == Phase::Reconnecting) {
    lv_obj_set_style_text_color(footLabel, ACCENT, 0);
    lv_label_set_text(footLabel, LV_SYMBOL_WARNING "  Wi-Fi lost, reconnecting...");
  } else if (!z) {
    lv_obj_set_style_text_color(footLabel, ACCENT, 0);
    lv_label_set_text_fmt(footLabel, "Set the time zone at %s.local", web::HOSTNAME);
  } else {
    lv_obj_set_style_text_color(footLabel, MUTE, 0);
    lv_label_set_text_fmt(footLabel, "%s  " LV_SYMBOL_WIFI "  %s.local", z->city, web::HOSTNAME);
  }
}

void updateClock() {
  static int lastMin = -1;
  static const tz::Zone *lastZone = nullptr;
  static Phase lastPhase = Phase::JoiningSaved;

  if (!timeValid()) {
    lv_label_set_text(dateLabel, "");
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_20, 0);
    lv_label_set_text(timeLabel, "Getting the time...");
    lastMin = -1;
  } else {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_min != lastMin || settings::timezone() != lastZone) {
      lastMin = t.tm_min;
      char buf[40];
      lv_obj_set_style_text_font(timeLabel, bigFont ? bigFont : &lv_font_montserrat_48, 0);
      strftime(buf, sizeof buf, "%H:%M", &t);
      lv_label_set_text(timeLabel, buf);
      char wday[12], month[12];
      strftime(wday, sizeof wday, "%A", &t);
      strftime(month, sizeof month, "%B", &t);
      lv_label_set_text_fmt(dateLabel, "%s, %d %s", wday, t.tm_mday, month);
    }
  }
  if (settings::timezone() != lastZone || phase != lastPhase) updateFooter();
  lastZone = settings::timezone();
  lastPhase = phase;
}

enum class View { Setup, Msg, Clock };
View current = View::Msg;

void showView(View v) {
  current = v;
  show(setupView, v == View::Setup);
  show(msgView, v == View::Msg);
  show(clockView, v == View::Clock);
  if (v == View::Clock) {
    updateFooter();
    updateClock();
  }
}

void showMsg(bool busy, const char *title, const String &body) {
  show(spinner, busy);
  show(bigIcon, !busy);
  lv_label_set_text(msgTitle, title);
  lv_label_set_text(msgBody, body.c_str());
  showView(View::Msg);
}

}  // namespace

void begin(const char *apName) {
  ap = apName;
  bigFont = lv_tiny_ttf_create_data(montserrat_medium_ttf, montserrat_medium_ttf_len, 92);

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  buildSetup();
  buildMsg();
  buildClock();
  showMsg(true, "Waking up...", "");
}

void showWifi(const wifisetup::Status &s) {
  phase = s.phase;
  switch (s.phase) {
    case Phase::Portal:
      lv_obj_set_style_text_color(hint, MUTE, 0);
      lv_label_set_text(hint, "A page will pop up. Pick your home Wi-Fi there.");
      showView(View::Setup);
      break;
    case Phase::PortalConnecting:
      lv_obj_set_style_text_color(hint, ACCENT, 0);
      lv_label_set_text_fmt(hint, "Connecting to %s ...", s.ssid.c_str());
      showView(View::Setup);
      break;
    case Phase::PortalFailed:
      lv_obj_set_style_text_color(hint, ERR, 0);
      lv_label_set_text_fmt(hint, "%s Please try again.", s.detail.c_str());
      showView(View::Setup);
      break;
    case Phase::JoiningSaved:
      showMsg(true, "Connecting...", s.ssid);
      break;
    case Phase::Reconnecting:
      // Keep showing the time if we have it; the footer says we're reconnecting.
      if (timeValid()) showView(View::Clock);
      else showMsg(true, "Reconnecting...", s.ssid);
      break;
    case Phase::Online:
      if (current == View::Clock) {  // came back from a blip: no fanfare
        updateFooter();
      } else {
        showMsg(false, "I'm online!", s.ssid);
        onlineAt = millis() | 1;
      }
      break;
  }
}

void loop() {
  static uint32_t last = 0;
  if (onlineAt && millis() - onlineAt > SPLASH_MS) {
    onlineAt = 0;
    if (phase == Phase::Online) showView(View::Clock);
  }
  if (current == View::Clock && millis() - last >= 250) {
    last = millis();
    updateClock();
  }
}

}  // namespace ui
