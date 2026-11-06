#include "ui.h"

#include <esp_heap_caps.h>
#include <jpeg_decoder.h>  // ESP-IDF esp_jpeg (TJpgDec): baseline JPEG -> RGB565
#include <lvgl.h>
#include <time.h>

#include "app_lock.h"

#include "display.h"
#include "portal_client.h"
#include "settings.h"
#include "sound.h"
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
lv_font_t *photoFont = nullptr;

lv_obj_t *scr;
// "join my network" view
lv_obj_t *setupView, *hint;
// spinner + message view (waking / joining / online splash)
lv_obj_t *msgView, *spinner, *bigIcon, *msgTitle, *msgBody;
// clock view (no photo)
lv_obj_t *clockView, *dateLabel, *timeLabel, *footLabel;
// clock view over the photo: full-screen image, dark bar at the bottom with time + date
lv_obj_t *photoView, *photoImg, *photoTime, *photoDate, *photoFoot;
// the message card, floating above whichever clock view is showing
lv_obj_t *noteCard, *noteLabel;

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

// ---------- photo view: the portal's photo with the time on a dark bar ----------
void buildPhoto() {
  photoView = lv_obj_create(scr);
  lv_obj_remove_style_all(photoView);
  lv_obj_set_size(photoView, lv_pct(100), lv_pct(100));
  lv_obj_remove_flag(photoView, LV_OBJ_FLAG_SCROLLABLE);

  photoImg = lv_image_create(photoView);
  lv_obj_set_size(photoImg, display::WIDTH, display::HEIGHT);
  lv_obj_align(photoImg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_image_set_inner_align(photoImg, LV_IMAGE_ALIGN_CENTER);

  lv_obj_t *bar = box(photoView, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(bar, lv_pct(100), 78);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
  lv_obj_set_style_pad_hor(bar, 14, 0);
  lv_obj_set_style_pad_column(bar, 12, 0);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  photoTime = lv_label_create(bar);
  lv_obj_set_style_text_font(photoTime, photoFont ? photoFont : &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(photoTime, lv_color_white(), 0);
  lv_label_set_long_mode(photoTime, LV_LABEL_LONG_CLIP);

  lv_obj_t *col = box(bar, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_size(col, 132, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(col, 2, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  photoDate = label(col, &lv_font_montserrat_16, lv_color_white(), LV_TEXT_ALIGN_RIGHT);
  photoFoot = label(col, &lv_font_montserrat_14, lv_color_hex(0xFFB3C0), LV_TEXT_ALIGN_RIGHT);
}

void buildNote() {
  noteCard = lv_obj_create(scr);
  lv_obj_remove_style_all(noteCard);
  lv_obj_set_size(noteCard, 296, LV_SIZE_CONTENT);
  lv_obj_align(noteCard, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_remove_flag(noteCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(noteCard, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(noteCard, LV_OPA_90, 0);
  lv_obj_set_style_radius(noteCard, 14, 0);
  lv_obj_set_style_pad_all(noteCard, 12, 0);
  noteLabel = label(noteCard, &lv_font_montserrat_16, INK);
  lv_obj_add_flag(noteCard, LV_OBJ_FLAG_HIDDEN);
}

void updateFooter() {
  const tz::Zone *z = settings::timezone();
  bool warn = true;
  if (phase == Phase::Reconnecting) {
    lv_obj_set_style_text_color(footLabel, ACCENT, 0);
    lv_label_set_text(footLabel, LV_SYMBOL_WARNING "  Wi-Fi lost, reconnecting...");
    lv_label_set_text(photoFoot, LV_SYMBOL_WARNING " Wi-Fi lost");
  } else if (!z) {
    lv_obj_set_style_text_color(footLabel, ACCENT, 0);
    lv_label_set_text_fmt(footLabel, "Set the time zone at %s.local", web::HOSTNAME);
    lv_label_set_text(photoFoot, "Set time zone");
  } else {
    warn = false;
    lv_obj_set_style_text_color(footLabel, MUTE, 0);
    lv_label_set_text_fmt(footLabel, "%s  " LV_SYMBOL_WIFI "  %s.local", z->city, web::HOSTNAME);
  }
  show(photoFoot, warn);  // over the photo only problems are worth the space
}

void updateClock() {
  static int lastMin = -1;
  static const tz::Zone *lastZone = nullptr;
  static Phase lastPhase = Phase::JoiningSaved;

  if (!timeValid()) {
    lv_label_set_text(dateLabel, "");
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_20, 0);
    lv_label_set_text(timeLabel, "Getting the time...");
    lv_label_set_text(photoDate, "");
    lv_obj_set_style_text_font(photoTime, &lv_font_montserrat_20, 0);
    lv_label_set_text(photoTime, "Getting the time...");
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
      lv_obj_set_style_text_font(photoTime, photoFont ? photoFont : &lv_font_montserrat_48, 0);
      lv_label_set_text(photoTime, buf);
      lv_label_set_text_fmt(photoDate, "%s\n%d %s", wday, t.tm_mday, month);
    }
  }
  if (settings::timezone() != lastZone || phase != lastPhase) updateFooter();
  lastZone = settings::timezone();
  lastPhase = phase;
}

// ---------- portal data (stub: serial logs until the clock face is designed) ----------
uint32_t seenPortalRev = 0;

enum class View { Setup, Msg, Clock, Photo };
View current = View::Msg;
bool inClock() { return current == View::Clock || current == View::Photo; }
void showView(View v);

// The decoded photo: 320x240 RGB565 in PSRAM, wrapped for LVGL. Two slots so the new picture is
// ready before the old one is released.
struct Decoded {
  lv_image_dsc_t dsc;
  uint8_t *px = nullptr;
};
Decoded slots[2];
int slotInUse = -1;
char shownPhotoId[13] = "";  // id of the photo on screen ("" = none)

// Decodes the portal's JPEG once; LVGL then just blits the result. Called with app::Lock held.
bool decodePhoto(const uint8_t *jpg, size_t n, const char *id) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = (uint8_t *)jpg;
  cfg.indata_size = n;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || !info.width || !info.height) {
    Serial.printf("[ui] photo %s: not a decodable JPEG\n", id);
    return false;
  }
  int slot = slotInUse == 0 ? 1 : 0;
  free(slots[slot].px);
  slots[slot].px = (uint8_t *)heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!slots[slot].px) {
    Serial.printf("[ui] photo %s: no memory for %u bytes\n", id, (unsigned)info.output_len);
    return false;
  }
  cfg.outbuf = slots[slot].px;
  cfg.outbuf_size = info.output_len;
  uint32_t t0 = millis();
  if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
    Serial.printf("[ui] photo %s: decode failed\n", id);
    free(slots[slot].px);
    slots[slot].px = nullptr;
    return false;
  }
  lv_image_dsc_t &d = slots[slot].dsc;
  memset(&d, 0, sizeof d);
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = info.width;
  d.header.h = info.height;
  d.header.stride = info.width * 2;
  d.data_size = info.output_len;
  d.data = slots[slot].px;
  lv_image_set_src(photoImg, &d);
  int old = slotInUse;
  slotInUse = slot;
  if (old >= 0) {
    free(slots[old].px);
    slots[old].px = nullptr;
  }
  strlcpy(shownPhotoId, id, sizeof shownPhotoId);
  Serial.printf("[ui] photo %s: %ux%u decoded in %u ms\n", id, info.width, info.height,
                (unsigned)(millis() - t0));
  return true;
}

void removePhoto() {
  if (slotInUse < 0) return;
  lv_image_set_src(photoImg, nullptr);
  free(slots[slotInUse].px);
  slots[slotInUse].px = nullptr;
  slotInUse = -1;
  shownPhotoId[0] = 0;
}

char noteShown[481] = "";   // the text on the card
uint32_t noteShownAt = 0;   // when it last (re)appeared: the card shows for the first 20 s of every minute

void applyPortal() {
  size_t n = 0;
  const uint8_t *jpg = portal::photo(&n);
  const char *note = portal::note();

  bool hadPhoto = slotInUse >= 0;
  if (jpg && n) {
    if (strcmp(shownPhotoId, portal::photoId()) != 0) decodePhoto(jpg, n, portal::photoId());
  } else {
    removePhoto();
  }
  if ((slotInUse >= 0) != hadPhoto && inClock()) showView(View::Clock);  // switch faces

  if (strcmp(noteShown, note ? note : "") != 0) {
    strlcpy(noteShown, note ? note : "", sizeof noteShown);
    lv_label_set_text(noteLabel, noteShown);
    noteShownAt = millis();
  }

  Serial.printf("[ui] portal update: note=%s%s%s, photo=%s (%u bytes), %u alarm(s)\n", note ? "\"" : "none",
                note ? note : "", note ? "\"" : "", jpg ? portal::photoId() : "none", (unsigned)n,
                (unsigned)portal::state().alarms_count);
}

// The card is up for the first 20 s of each minute, so the photo isn't covered all day.
void updateNote() {
  bool want = noteShown[0] && inClock() && (millis() - noteShownAt) % 60000 < 20000;
  if (want == !lv_obj_has_flag(noteCard, LV_OBJ_FLAG_HIDDEN)) return;
  show(noteCard, want);
}

// ---------- alarm: full-screen overlay + sound until touched ----------
constexpr uint32_t RING_MAX_MS = 5 * 60 * 1000;  // nobody came: stop by itself after 5 minutes
constexpr uint8_t BOOT_PIN = 0;                  // BOOT button: stops the alarm too, if touch isn't working
const lv_color_t ACCENT_LIGHT = lv_color_hex(0xF0A0AE);

lv_obj_t *alarmView, *alarmTime, *alarmText;
bool ringing = false;
uint32_t ringSince = 0;
bool ringFlash = false;

void stopRing(const char *why) {
  if (!ringing) return;
  ringing = false;
  sound::stop();
  show(alarmView, false);
  display::setBacklight(display::BACKLIGHT_PCT);
  Serial.printf("[alarm] stopped (%s)\n", why);
}

void startRing(unsigned hour, unsigned minute, const char *text, const char *soundFile) {
  if (ringing) return;  // already going off; the first one wins
  ringing = true;
  ringSince = millis();
  ringFlash = false;
  lv_label_set_text_fmt(alarmTime, "%02u:%02u", hour, minute);
  lv_label_set_text(alarmText, text[0] ? text : "Wake up!");
  lv_obj_set_style_bg_color(alarmView, ACCENT, 0);
  show(alarmView, true);
  lv_obj_move_foreground(alarmView);
  display::setBacklight(100);
  sound::play(soundFile);
  Serial.printf("[alarm] RING %02u:%02u \"%s\" sound=%s\n", hour, minute, text, soundFile[0] ? soundFile : "(beep)");
}

void buildAlarm() {
  // On the top layer: above every face and the message card, and never hidden by showView().
  alarmView = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(alarmView);
  lv_obj_set_size(alarmView, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(alarmView, ACCENT, 0);
  lv_obj_set_style_bg_opa(alarmView, LV_OPA_COVER, 0);
  lv_obj_remove_flag(alarmView, LV_OBJ_FLAG_SCROLLABLE);
  // The whole screen is the button: a touch anywhere stops the alarm (nothing under it is clickable).
  lv_obj_add_event_cb(alarmView, [](lv_event_t *) { stopRing("touched"); }, LV_EVENT_PRESSED, nullptr);

  lv_obj_t *col = fullColumn(alarmView);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);  // let touches fall through to alarmView
  lv_obj_set_style_pad_row(col, 6, 0);

  lv_obj_t *bell = label(col, &lv_font_montserrat_28, lv_color_white());
  lv_label_set_text(bell, LV_SYMBOL_BELL);
  alarmTime = label(col, photoFont ? photoFont : &lv_font_montserrat_48, lv_color_white());
  lv_label_set_long_mode(alarmTime, LV_LABEL_LONG_CLIP);
  alarmText = label(col, &lv_font_montserrat_20, lv_color_white());

  lv_obj_t *btn = lv_obj_create(col);
  lv_obj_remove_style_all(btn);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(btn, 200, 60);
  lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_t *bl = lv_label_create(btn);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(bl, ACCENT, 0);
  lv_label_set_text(bl, "Stop");
  lv_obj_center(bl);

  lv_obj_add_flag(alarmView, LV_OBJ_FLAG_HIDDEN);
}

void updateAlarm() {
  if (!ringing) return;
  if (digitalRead(BOOT_PIN) == LOW) return stopRing("BOOT button");
  if (millis() - ringSince > RING_MAX_MS) return stopRing("timed out");
  bool flash = ((millis() - ringSince) / 700) & 1;  // gentle pulse so it's noticed from across the room
  if (flash != ringFlash) {
    ringFlash = flash;
    lv_obj_set_style_bg_color(alarmView, flash ? ACCENT_LIGHT : ACCENT, 0);
  }
}

bool alarmMatches(const portal::Alarm &a, const struct tm &t) {
  if (a.hour != (uint32_t)t.tm_hour || a.minute != (uint32_t)t.tm_min) return false;
  if (a.days) return a.days & (1u << t.tm_wday);
  return a.has_date && a.date.year == (uint32_t)t.tm_year + 1900 && a.date.month == (uint32_t)t.tm_mon + 1 &&
         a.date.day == (uint32_t)t.tm_mday;
}

void checkAlarms() {
  static time_t lastMinute = 0;
  if (!timeValid() || !portal::haveState()) return;
  time_t now = time(nullptr);
  if (now / 60 == lastMinute) return;  // alarms are checked once per minute, so one can't fire twice
  lastMinute = now / 60;
  struct tm t;
  localtime_r(&now, &t);
  const portal::State &s = portal::state();
  for (pb_size_t i = 0; i < s.alarms_count; i++) {
    if (!alarmMatches(s.alarms[i], t)) continue;
    startRing(s.alarms[i].hour, s.alarms[i].minute, s.alarms[i].label, s.alarms[i].sound);
    break;
  }
}

void showView(View v) {
  if (v == View::Clock && slotInUse >= 0) v = View::Photo;  // "Clock" = whichever face applies
  current = v;
  show(setupView, v == View::Setup);
  show(msgView, v == View::Msg);
  show(clockView, v == View::Clock);
  show(photoView, v == View::Photo);
  if (inClock()) {
    updateFooter();
    updateClock();
  }
  updateNote();
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
  photoFont = lv_tiny_ttf_create_data(montserrat_medium_ttf, montserrat_medium_ttf_len, 56);

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  buildSetup();
  buildMsg();
  buildClock();
  buildPhoto();
  buildNote();
  buildAlarm();  // last, so it floats above everything
  showMsg(true, "Waking up...", "");
}

void testAlarm(const char *soundFile) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  startRing(t.tm_hour, t.tm_min, "Test alarm", soundFile ? soundFile : "");
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
      if (inClock()) {  // came back from a blip: no fanfare
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
  if (portal::revision() != seenPortalRev) {
    seenPortalRev = portal::revision();
    applyPortal();
  }
  checkAlarms();
  updateAlarm();
  if (onlineAt && millis() - onlineAt > SPLASH_MS) {
    onlineAt = 0;
    if (phase == Phase::Online) showView(View::Clock);
  }
  if (inClock() && millis() - last >= 250) {
    last = millis();
    updateClock();
    updateNote();
  }
}

}  // namespace ui
