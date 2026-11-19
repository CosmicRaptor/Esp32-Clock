#include "debug_log.h"
#include "ui.h"

#include <esp_heap_caps.h>
#include <jpeg_decoder.h>  // ESP-IDF esp_jpeg (TJpgDec): baseline JPEG -> RGB565
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>

#include "app_lock.h"
#include "mindfulness.h"
#include "focus_timer.h"
#include "focus_view.h"
#include "video_player.h"

#include "display.h"
#include "face_view.h"
#include "portal_client.h"
#include "weather_icons.h"
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
constexpr uint32_t MODE_REVEAL_MS = 120; // the title is already visible throughout the drag
constexpr uint32_t FACE_TICK_MS = 66;  // portal faces animate at 15 ticks a second (portal's ANIM_FPS: the budget assumes it)

enum class Mode { Clock, Focus, Night };
Mode mode = Mode::Clock;
focus::Timer focusTimer;
bool nightDim = false, homeLoaded = true;
bool pagerMoving = false;
uint32_t pagerRecoveries = 0;
int requestedMode = -1;
uint32_t revealAt = 0;
lv_obj_t *modeRoot = nullptr, *modeTime = nullptr, *modeRing = nullptr, *modePanda = nullptr;
lv_obj_t *modeCaption = nullptr, *nightSpotify = nullptr;
lv_obj_t *pager = nullptr, *pages[3] = {}, *pageTitles[3] = {};
void requestMode(Mode next);
void updateModes();
void applyBrightness();
void setPagerContentVisible(bool visible);
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
lv_obj_t *noteCard, *noteLabel, *noteButton, *noteDot;
bool inboxOpen = false;
uint64_t noteIdentity = 0, readIdentity = 0;
void openInbox(lv_event_t *);
void closeInbox(lv_event_t *);

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
  if (!o) return;
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
  clockView = fullColumn(pages[0]);
  lv_obj_set_style_pad_row(clockView, 0, 0);
  lv_obj_set_flex_align(clockView, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  static lv_font_t dateFont = lv_font_montserrat_20;
  dateFont.fallback = &weather_icons;
  dateLabel = label(clockView, &dateFont, MUTE);
  timeLabel = label(clockView, bigFont ? bigFont : &lv_font_montserrat_48, INK);
  lv_label_set_long_mode(timeLabel, LV_LABEL_LONG_CLIP);
  footLabel = label(clockView, &lv_font_montserrat_14, MUTE);
}

// ---------- photo view: the portal's photo with the time on a dark bar ----------
void buildPhoto() {
  photoView = lv_obj_create(pages[0]);
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
  static lv_font_t dateFont = lv_font_montserrat_16;
  dateFont.fallback = &weather_icons;
  photoDate = label(col, &dateFont, lv_color_white(), LV_TEXT_ALIGN_RIGHT);
  photoFoot = label(col, &lv_font_montserrat_14, lv_color_hex(0xFFB3C0), LV_TEXT_ALIGN_RIGHT);
}

void buildNote() {
  Preferences prefs;
  if (prefs.begin("note-inbox", true)) { readIdentity = prefs.getULong64("read", 0); prefs.end(); }
  noteButton = lv_obj_create(pages[0]);
  lv_obj_remove_style_all(noteButton);
  lv_obj_set_size(noteButton, 40, 40);
  lv_obj_set_style_bg_color(noteButton, lv_color_hex(0x231E1E), 0);
  lv_obj_set_style_bg_opa(noteButton, LV_OPA_80, 0);
  lv_obj_set_style_radius(noteButton, 12, 0);
  lv_obj_remove_flag(noteButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(noteButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(noteButton, openInbox, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *icon = label(noteButton, &lv_font_montserrat_20, lv_color_white());
  lv_label_set_text(icon, LV_SYMBOL_ENVELOPE); lv_obj_center(icon);
  noteDot = lv_obj_create(noteButton); lv_obj_remove_style_all(noteDot);
  lv_obj_set_size(noteDot, 9, 9); lv_obj_align(noteDot, LV_ALIGN_TOP_RIGHT, -4, 4);
  lv_obj_set_style_radius(noteDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(noteDot, ACCENT, 0); lv_obj_set_style_bg_opa(noteDot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(noteDot, LV_OBJ_FLAG_CLICKABLE);
  noteCard = lv_obj_create(scr); lv_obj_remove_style_all(noteCard);
  lv_obj_set_size(noteCard, 296, 210); lv_obj_center(noteCard);
  lv_obj_set_style_bg_color(noteCard, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(noteCard, LV_OPA_COVER, 0); lv_obj_set_style_radius(noteCard, 14, 0);
  lv_obj_t *title = label(noteCard, &lv_font_montserrat_16, ACCENT);
  lv_label_set_text(title, "A NOTE FOR YOU"); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
  lv_obj_t *content = lv_obj_create(noteCard); lv_obj_remove_style_all(content);
  lv_obj_set_size(content, 272, 130); lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  noteLabel = label(content, &lv_font_montserrat_16, INK);
  lv_obj_t *close = lv_obj_create(noteCard); lv_obj_remove_style_all(close);
  lv_obj_set_size(close, 280, 35); lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(close, closeInbox, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *hint = label(close, &lv_font_montserrat_14, MUTE); lv_label_set_text(hint, "Tap here to close"); lv_obj_center(hint);
  show(noteCard, false); show(noteButton, false);
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
    if (t.tm_min != lastMin || settings::timezone() != lastZone || !lv_label_get_text(timeLabel)[0]) {
      lastMin = t.tm_min;
      char buf[40];
      lv_obj_set_style_text_font(timeLabel, bigFont ? bigFont : &lv_font_montserrat_48, 0);
      strftime(buf, sizeof buf, "%H:%M", &t);
      lv_label_set_text(timeLabel, buf);
      char wday[12], month[12];
      strftime(wday, sizeof wday, "%A", &t);
      strftime(month, sizeof month, "%B", &t);
      char weather[32], shortDate[24];
      portal::weatherText(weather, sizeof weather);
      strftime(shortDate, sizeof shortDate, "%a, %d %b", &t);
      if (weather[0]) lv_label_set_text_fmt(dateLabel, "%s%s", shortDate, weather);
      else lv_label_set_text_fmt(dateLabel, "%s, %d %s", wday, t.tm_mday, month);
      lv_obj_set_style_text_font(photoTime, photoFont ? photoFont : &lv_font_montserrat_48, 0);
      lv_label_set_text(photoTime, buf);
      if (weather[0]) {
        strftime(shortDate, sizeof shortDate, "%b", &t);
        lv_label_set_text_fmt(photoDate, "%s\n%d %s%s", wday, t.tm_mday, shortDate, weather);
      } else lv_label_set_text_fmt(photoDate, "%s\n%d %s", wday, t.tm_mday, month);
    }
  }
  if (settings::timezone() != lastZone || phase != lastPhase) updateFooter();
  lastZone = settings::timezone();
  lastPhase = phase;
}

// ---------- portal data (stub: serial logs until the clock face is designed) ----------
uint32_t seenPortalRev = 0;

enum class View { Setup, Msg, Clock, Photo, Face };
View current = View::Msg;
bool inClock() { return homeLoaded && mode == Mode::Clock && (current == View::Clock || current == View::Photo || current == View::Face); }
bool faceSuspendedForVideo = false;
bool faceShown = false;  // a portal face is built and is what "the clock" means right now (see refreshFace())
bool npVisible = false;  // the now-playing strip is on screen (see below)
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
    debuglog::printf("[ui] photo %s: not a decodable JPEG\n", id);
    return false;
  }
  int slot = slotInUse == 0 ? 1 : 0;
  free(slots[slot].px);
  slots[slot].px = (uint8_t *)heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!slots[slot].px) {
    debuglog::printf("[ui] photo %s: no memory for %u bytes\n", id, (unsigned)info.output_len);
    return false;
  }
  cfg.outbuf = slots[slot].px;
  cfg.outbuf_size = info.output_len;
  uint32_t t0 = millis();
  if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
    debuglog::printf("[ui] photo %s: decode failed\n", id);
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
  debuglog::printf("[ui] photo %s: %ux%u decoded in %u ms\n", id, info.width, info.height,
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

char noteShown[481] = "";

uint64_t identityForNote(const char *text, int64_t sentAt) {
  if (!text || !*text) return 0;
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) { hash ^= *p; hash *= 1099511628211ULL; }
  for (unsigned i = 0; i < 8; ++i) { hash ^= (uint64_t)sentAt >> (8 * i) & 255; hash *= 1099511628211ULL; }
  return hash ? hash : 1;
}
void openInbox(lv_event_t *) {
  inboxOpen = true;
  lv_label_set_text(noteLabel, noteShown[0] ? noteShown : "No note right now.");
  show(noteCard, true); lv_obj_move_foreground(noteCard);
  if (noteIdentity && noteIdentity != readIdentity) {
    Preferences prefs;
    if (prefs.begin("note-inbox", false)) {
      if (prefs.putULong64("read", noteIdentity)) readIdentity = noteIdentity;
      prefs.end();
    }
    show(noteDot, noteIdentity != readIdentity);
  }
}
void closeInbox(lv_event_t *) { inboxOpen = false; show(noteCard, false); }

void applyPortal() {
  if (!homeLoaded) return;
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

  }

  noteIdentity = identityForNote(note, portal::state().has_note ? portal::state().note.sent_at : 0);
  if (inboxOpen) lv_label_set_text(noteLabel, noteShown[0] ? noteShown : "No note right now.");

  debuglog::printf("[ui] portal update: note=%s%s%s, photo=%s (%u bytes), %u alarm(s)\n", note ? "\"" : "none",
                note ? note : "", note ? "\"" : "", jpg ? portal::photoId() : "none", (unsigned)n,
                (unsigned)portal::state().alarms_count);
}

constexpr int NP_H = 46;  // the now-playing strip's height (below)

// Existing faces automatically choose the corner opposite their Spotify strip.
void updateNote() {
  bool clock = inClock() && mode == Mode::Clock && !focusTimer.quiet();
  if (!clock && inboxOpen) { inboxOpen = false; show(noteCard, false); }
  show(noteButton, clock);
  if (clock) lv_obj_move_foreground(noteButton);
  show(noteDot, noteIdentity && noteIdentity != readIdentity);
  if (!clock) return;
  int x = 272, y = 8;
  if (current == View::Photo) y = 192;
  if (current == View::Face) {
    const auto *custom = faceview::messageBoxSlot();
    const auto *strip = faceview::nowPlayingSlot();
    if (custom) { x = custom->x; y = custom->y; }
    else if (!strip || (strip->show && strip->y < 80)) y = 192;
  }
  static int oldX = -1, oldY = -1;
  if (x != oldX || y != oldY) { oldX = x; oldY = y; lv_obj_align(noteButton, LV_ALIGN_TOP_LEFT, x, y); }
}

// ---------- now playing: a tiny strip, your song on the left and hers on the right ----------
// One strip that lives on whichever face is showing: between the time and the footer on the plain
// clock, a dark pill at the top over the photo. A side with nothing playing is left out (the other
// stays where it is); with neither playing the strip is hidden and the face is just the clock.
constexpr int NP_ART = 38;                       // cover thumbnail, px (the JPEG is 64x64, scaled down)
constexpr int NP_GAP = 6;
constexpr uint32_t NP_PAUSED_HIDE_MS = 5 * 60 * 1000;  // a song paused this long is no longer "what you're playing"
const lv_color_t NP_TITLE_PHOTO = lv_color_white();
const lv_color_t NP_ARTIST_PHOTO = lv_color_hex(0xFFB3C0);

struct NpSide {
  lv_obj_t *half, *artBox, *artImg, *note, *col, *title, *artist;
  lv_image_dsc_t dsc;
  uint8_t *px = nullptr;        // decoded cover: 64x64 RGB565 in PSRAM
  char coverId[41] = "";        // id of the cover decoded into px ("" = none)
  char titleShown[61] = "";
  char artistShown[61] = "";
  uint32_t pausedSince = 0;
};
NpSide np[2];
lv_obj_t *npStrip;
uint32_t seenNpRev = 0;

void buildStrip() {
  npStrip = lv_obj_create(clockView);
  lv_obj_remove_style_all(npStrip);
  lv_obj_set_size(npStrip, 296, NP_H);
  lv_obj_remove_flag(npStrip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(npStrip, 12, 0);
  for (int i = 0; i < 2; i++) {
    NpSide &n = np[i];
    n.half = lv_obj_create(npStrip);
    lv_obj_remove_style_all(n.half);
    lv_obj_remove_flag(n.half, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(n.half, lv_pct(50), lv_pct(100));
    lv_obj_align(n.half, i ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    // the right side is the left one mirrored: cover on the outside, text on the inside, right-aligned
    lv_obj_set_flex_flow(n.half, i ? LV_FLEX_FLOW_ROW_REVERSE : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(n.half, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(n.half, NP_GAP, 0);

    n.artBox = lv_obj_create(n.half);
    lv_obj_remove_style_all(n.artBox);
    lv_obj_remove_flag(n.artBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(n.artBox, NP_ART, NP_ART);
    lv_obj_set_style_radius(n.artBox, 7, 0);
    lv_obj_set_style_clip_corner(n.artBox, true, 0);
    lv_obj_set_style_bg_color(n.artBox, ACCENT, 0);  // shows while the cover is on its way, or when a song has none
    lv_obj_set_style_bg_opa(n.artBox, LV_OPA_COVER, 0);
    n.note = lv_label_create(n.artBox);
    lv_obj_set_style_text_font(n.note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(n.note, lv_color_white(), 0);
    lv_label_set_text(n.note, LV_SYMBOL_AUDIO);
    lv_obj_center(n.note);
    n.artImg = lv_image_create(n.artBox);
    lv_obj_set_size(n.artImg, NP_ART, NP_ART);
    lv_image_set_inner_align(n.artImg, LV_IMAGE_ALIGN_STRETCH);  // 64x64 -> 38x38
    lv_obj_align(n.artImg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(n.artImg, LV_OBJ_FLAG_HIDDEN);

    n.col = box(n.half, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(n.col, 100, LV_SIZE_CONTENT);
    lv_text_align_t al = i ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
    n.title = label(n.col, &lv_font_montserrat_14, INK, al);
    n.artist = label(n.col, &lv_font_montserrat_14, MUTE, al);
    // a long title scrolls round instead of being cut off
    lv_label_set_long_mode(n.title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(n.artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(n.half, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_add_flag(npStrip, LV_OBJ_FLAG_HIDDEN);
}

// Moves the strip to the face that is showing and restyles it for it.
void placeStrip() {
  bool onFace = current == View::Face;
  bool onPhoto = current == View::Photo || onFace;  // over a picture: the dark pill at the top
  lv_obj_t *parent = onFace ? faceview::root() : onPhoto ? photoView : clockView;
  if (lv_obj_get_parent(npStrip) != parent) lv_obj_set_parent(npStrip, parent);
  if (onFace) lv_obj_move_foreground(npStrip);  // above the face's own objects
  int w, padH;
  lv_color_t titleCol = onPhoto ? NP_TITLE_PHOTO : INK, artistCol = onPhoto ? NP_ARTIST_PHOTO : MUTE;
  const auto *slot = onFace ? faceview::nowPlayingSlot() : nullptr;  // this face's own placement and colours, if it has any
  if (slot) {
    w = slot->w;
    padH = slot->has_bg ? 6 : 0;
    lv_obj_set_style_bg_color(npStrip, lv_color_hex(slot->bg), 0);
    lv_obj_set_style_bg_opa(npStrip, slot->has_bg ? (lv_opa_t)(slot->bg_pct * 255 / 100) : LV_OPA_TRANSP, 0);
    lv_obj_align(npStrip, LV_ALIGN_TOP_LEFT, slot->x, slot->y);
    titleCol = lv_color_hex(slot->title);
    artistCol = lv_color_hex(slot->artist);
  } else if (onPhoto) {
    w = 304;
    padH = 6;
    lv_obj_set_style_bg_color(npStrip, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(npStrip, LV_OPA_60, 0);
    lv_obj_align(npStrip, LV_ALIGN_TOP_MID, 0, 8);
  } else {
    w = 296;
    padH = 0;
    lv_obj_set_style_bg_opa(npStrip, LV_OPA_TRANSP, 0);
    lv_obj_move_to_index(npStrip, 2);  // date, time, [strip], footer
  }
  lv_obj_set_size(npStrip, w, NP_H);
  lv_obj_set_style_pad_hor(npStrip, padH, 0);
  // Leave breathing room between the two listeners' text columns at the centre.
  int colW = (w - 2 * padH) / 2 - NP_ART - NP_GAP - 4;
  for (NpSide &n : np) {
    lv_obj_set_width(n.col, colW);
    lv_obj_set_style_bg_color(n.artBox, slot ? artistCol : ACCENT, 0);
    lv_obj_set_style_text_color(n.note, slot ? lv_color_hex(slot->has_bg ? slot->bg : slot->title) : lv_color_white(), 0);
    lv_obj_set_style_text_color(n.title, titleCol, 0);
    lv_obj_set_style_text_color(n.artist, artistCol, 0);
  }
}

// Decodes a side's 64x64 cover into its own PSRAM buffer. Called with app::Lock held.
bool decodeCover(NpSide &n, const uint8_t *jpg, size_t len, const char *id) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = (uint8_t *)jpg;
  cfg.indata_size = len;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || !info.width || !info.height || info.output_len > 64 * 1024) {
    debuglog::printf("[ui] cover %.8s: not a decodable JPEG\n", id);
    return false;
  }
  uint8_t *px = (uint8_t *)heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!px) return false;
  cfg.outbuf = px;
  cfg.outbuf_size = info.output_len;
  if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
    debuglog::printf("[ui] cover %.8s: decode failed\n", id);
    free(px);
    return false;
  }
  memset(&n.dsc, 0, sizeof n.dsc);
  n.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  n.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  n.dsc.header.w = info.width;
  n.dsc.header.h = info.height;
  n.dsc.header.stride = info.width * 2;
  n.dsc.data_size = info.output_len;
  n.dsc.data = px;
  lv_image_set_src(n.artImg, &n.dsc);
  free(n.px);
  n.px = px;
  return true;
}

// New answer or cover from the portal: update the texts and covers (visibility is updateNowPlaying()'s job).
void applyNowPlaying() {
  if (!homeLoaded) return;
  const portal::NowPlaying &now = portal::nowPlaying();
  for (int i = 0; i < 2; i++) {
    NpSide &n = np[i];
    const portal::Listener &l = i ? now.right : now.left;
    if (strcmp(n.titleShown, l.title) != 0) {
      strlcpy(n.titleShown, l.title, sizeof n.titleShown);
      lv_label_set_text(n.title, n.titleShown);
    }
    if (strcmp(n.artistShown, l.artist) != 0) {
      strlcpy(n.artistShown, l.artist, sizeof n.artistShown);
      lv_label_set_text(n.artist, n.artistShown);
    }
    size_t len = 0;
    const uint8_t *jpg = portal::cover(i, &len);
    const char *id = portal::coverId(i);
    if (!jpg || !len) {  // no cover (yet): the pink note stays
      if (n.coverId[0]) {
        lv_image_set_src(n.artImg, nullptr);
        free(n.px);
        n.px = nullptr;
        n.coverId[0] = 0;
        lv_obj_add_flag(n.artImg, LV_OBJ_FLAG_HIDDEN);
      }
    } else if (strcmp(n.coverId, id) != 0) {
      if (decodeCover(n, jpg, len, id)) {
        strlcpy(n.coverId, id, sizeof n.coverId);
        lv_obj_remove_flag(n.artImg, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

// Shows or hides each side and the strip itself. Cheap; runs with the clock's 250 ms tick.
void updateNowPlaying() {
  if (!homeLoaded) return;
  bool fresh = portal::nowPlayingFresh();  // not heard from the portal in a while: better nothing than stale
  const portal::NowPlaying &now = portal::nowPlaying();
  bool any = false;
  for (int i = 0; i < 2; i++) {
    NpSide &n = np[i];
    int st = fresh ? (int)(i ? now.right.status : now.left.status) : bhoomi_clock_v1_Listener_Status_OFF;
    if (st == bhoomi_clock_v1_Listener_Status_PAUSED) {
      if (!n.pausedSince) n.pausedSince = millis() | 1;
      if (millis() - n.pausedSince > NP_PAUSED_HIDE_MS) st = bhoomi_clock_v1_Listener_Status_OFF;
    } else {
      n.pausedSince = 0;
    }
    bool on = st != bhoomi_clock_v1_Listener_Status_OFF;
    any = any || on;
    if (on == !lv_obj_has_flag(n.half, LV_OBJ_FLAG_HIDDEN) &&
        (st == bhoomi_clock_v1_Listener_Status_PAUSED) == (lv_obj_get_style_opa(n.half, 0) < LV_OPA_COVER))
      continue;
    show(n.half, on);
    lv_obj_set_style_opa(n.half, st == bhoomi_clock_v1_Listener_Status_PAUSED ? LV_OPA_50 : LV_OPA_COVER, 0);
    debuglog::printf("[ui] now playing %s side: %s\n", i ? "right" : "left",
                  !on ? "hidden" : st == bhoomi_clock_v1_Listener_Status_PAUSED ? "paused (dimmed)" : "playing");
  }
  const auto *slot = current == View::Face ? faceview::nowPlayingSlot() : nullptr;
  bool want = any && inClock() && mode == Mode::Clock && !focusTimer.quiet() && !(slot && !slot->show);  // a face can switch the strip off
  if (want != npVisible) {
    npVisible = want;
    show(npStrip, want);
    debuglog::printf("[ui] now playing strip %s\n", want ? "shown" : "hidden");
  }
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
  applyBrightness();
  debuglog::printf("[alarm] stopped (%s)\n", why);
}

void startRing(unsigned hour, unsigned minute, const char *text, const char *soundFile) {
  if (ringing) return;  // already going off; the first one wins
  video_player::stop();
  ringing = true;
  ringSince = millis();
  ringFlash = false;
  lv_label_set_text_fmt(alarmTime, "%02u:%02u", hour, minute);
  lv_label_set_text(alarmText, text[0] ? text : "Wake up!");
  lv_obj_set_style_bg_color(alarmView, ACCENT, 0);
  show(alarmView, true);
  lv_obj_move_foreground(alarmView);
  applyBrightness();
  sound::play(soundFile);
  debuglog::printf("[alarm] RING %02u:%02u \"%s\" sound=%s\n", hour, minute, text, soundFile[0] ? soundFile : "(beep)");
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

// Quiet, locally scheduled check-in. A response is persisted before playback begins.
lv_obj_t *moodView, *moodStatus, *moodNoteView, *moodNoteText;
bool moodVisible = false, noteVisible = false;
uint32_t moodSince = 0, noteSince = 0;
unsigned noteMood = 0;
void playMoodVideo(unsigned mood);
void finishMoodNote() {
  if (!noteVisible || ringing) return;
  noteVisible = false; show(moodNoteView, false);
  playMoodVideo(noteMood);
}
void showMoodNote(unsigned mood) {
  const char *note = portal::moodNote(mood);
  if (!note || !note[0]) { playMoodVideo(mood); return; }
  noteMood = mood; noteSince = millis(); noteVisible = true;
  lv_label_set_text(moodNoteText, note);
  show(moodNoteView, true); lv_obj_move_foreground(moodNoteView);
  if (ringing) lv_obj_move_foreground(alarmView);
}
void buildMood() {
  moodView = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(moodView); lv_obj_set_size(moodView, 320, 240);
  lv_obj_set_style_bg_color(moodView, BG, 0); lv_obj_set_style_bg_opa(moodView, LV_OPA_COVER, 0);
  lv_obj_remove_flag(moodView, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *eyebrow = label(moodView, &lv_font_montserrat_14, ACCENT);
  lv_label_set_text(eyebrow, "A LITTLE MOMENT"); lv_obj_align(eyebrow, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_t *title = label(moodView, &lv_font_montserrat_20, INK);
  lv_label_set_text(title, "How are you feeling?"); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);
  const char *names[] = {"Very low", "Low", "Okay", "Good", "Great"};
  const uint32_t colors[] = {0xF4DEE6, 0xF6E3DE, 0xF7EBD4, 0xE6EEDB, 0xDCEFE4};
  for (int i = 0; i < 5; ++i) {
    lv_obj_t *button = lv_obj_create(moodView);
    lv_obj_set_size(button, 57, 76); lv_obj_set_pos(button, 9 + i * 61, 91);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(button, 2, 0); lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 0, 0); lv_obj_set_style_bg_color(button, lv_color_hex(colors[i]), 0);
    lv_obj_t *text = label(button, &lv_font_montserrat_14, INK);
    lv_label_set_text(text, names[i]); lv_obj_center(text); lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, [](lv_event_t *e) {
      unsigned mood = (uintptr_t)lv_event_get_user_data(e);
      if (!mindfulness::enqueue((uint32_t)time(nullptr), mood)) {
        lv_label_set_text(moodStatus, "Could not save. Please try again."); return;
      }
      moodVisible = false; show(moodView, false);
      showMoodNote(mood);
    }, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
  }
  moodStatus = label(moodView, &lv_font_montserrat_14, MUTE);
  lv_label_set_text(moodStatus, "A little moment for you"); lv_obj_align(moodStatus, LV_ALIGN_BOTTOM_MID, 0, -22);
  show(moodView, false);
  moodNoteView = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(moodNoteView);
  lv_obj_set_size(moodNoteView, 320, 240); lv_obj_set_style_bg_color(moodNoteView, BG, 0);
  lv_obj_set_style_bg_opa(moodNoteView, LV_OPA_COVER, 0);
  lv_obj_remove_flag(moodNoteView, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(moodNoteView, [](lv_event_t *) { finishMoodNote(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *caption = label(moodNoteView, &lv_font_montserrat_14, ACCENT);
  lv_label_set_text(caption, "A LITTLE NOTE FOR YOU"); lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 35);
  lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE);
  moodNoteText = label(moodNoteView, &lv_font_montserrat_20, INK);
  lv_obj_set_width(moodNoteText, 280); lv_obj_center(moodNoteText); lv_obj_remove_flag(moodNoteText, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *hint = label(moodNoteView, &lv_font_montserrat_14, MUTE);
  lv_label_set_text(hint, "Tap to continue"); lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24); lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
  show(moodNoteView, false);
}
void showMood() {
  moodVisible = true; moodSince = millis();
  lv_label_set_text(moodStatus, "A little moment for you");
  show(moodView, true); lv_obj_move_foreground(moodView);
  if (ringing) lv_obj_move_foreground(alarmView);
}
void updateMood() {
  if (moodVisible && millis() - moodSince >= 300000) { moodVisible = false; show(moodView, false); }
  if (!timeValid() || mode != Mode::Clock || focusTimer.quiet()) return;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  if (!mindfulness::shouldPrompt(now, t)) return;
  showMood();
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

// Which portal face is on screen. The clock picks it itself from the faces' rules and its own local time
// (portal::chooseFace), so this runs when a face arrives or changes and once a minute (an hour can end).
char faceShownId[13] = "", faceShownRev[13] = "";
uint32_t seenFacesRev = 0;
int faceShownWeather = -1;
int faceCheckedMin = -1;

void refreshFace() {
  if (!homeLoaded || mode != Mode::Clock || faceSuspendedForVideo || !timeValid()) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  faceCheckedMin = t.tm_min;
  bool ready = false;
  const portal::Face *f = portal::chooseFace(t, &ready);
  bool changed = false;
  if (!f) {
    if (!ready || !faceShown) return;  // the one we want is still on its way: keep what's showing
    faceview::clear();                 // no face fits any more: back to the clock's own
    faceShown = false;
    faceShownId[0] = faceShownRev[0] = 0;
    changed = true;
    debuglog::println("[ui] no portal face applies: the built-in clock");
  } else if (!faceShown || strcmp(f->id, faceShownId) != 0 || strcmp(f->rev, faceShownRev) != 0 || portal::weatherFaceGroup() != faceShownWeather) {
    if (!faceview::show(*f)) return;  // can't be built: keep what's showing, and try again next minute
    faceShown = true;
    faceShownWeather = portal::weatherFaceGroup();
    strlcpy(faceShownId, f->id, sizeof faceShownId);
    strlcpy(faceShownRev, f->rev, sizeof faceShownRev);
    changed = true;
  }
  if (changed && inClock()) showView(View::Clock);
}


void playMoodVideo(unsigned mood) {
  // The video fully covers the clock: release its rendered face (including font
  // caches) before allocating video frames. Portal assets stay cached in PSRAM.
  faceSuspendedForVideo = true;
  faceview::clear();
  faceShown = false;
  if (inClock()) showView(View::Clock);
  video_player::play(mood);
  // loop() restores the currently scheduled face after completion, touch,
  // alarm interruption, or a failed first frame; never rebuild during playback.
}

void showView(View v) {
  if (!homeLoaded) return;
  // "Clock" = whichever face applies: a portal face, else the photo, else the plain clock
  if (v == View::Clock) v = faceShown ? View::Face : slotInUse >= 0 ? View::Photo : View::Clock;
  current = v;
  show(pager, inClock());
  show(setupView, v == View::Setup);
  show(msgView, v == View::Msg);
  show(clockView, v == View::Clock);
  show(photoView, v == View::Photo);
  show(faceview::root(), v == View::Face);
  if (inClock()) {
    placeStrip();
    updateFooter();
    updateClock();
  }
  updateNowPlaying();
  updateNote();
}

void showMsg(bool busy, const char *title, const String &body) {
  show(spinner, busy);
  show(bigIcon, !busy);
  lv_label_set_text(msgTitle, title);
  lv_label_set_text(msgBody, body.c_str());
  showView(View::Msg);
}


// Only the selected mode owns a rendered screen. Cached portal source assets stay
// available so returning to the clock never needs a network round trip.
void unloadHome() {
  if (!homeLoaded) return;
  closeInbox(nullptr); show(noteButton, false);
  lv_obj_delete(npStrip); npStrip = nullptr; npVisible = false;
  for (auto &n : np) { free(n.px); n = NpSide{}; }
  faceview::clear(); faceShown = false;
  removePhoto();
  lv_obj_delete(clockView); lv_obj_delete(photoView);
  clockView = photoView = nullptr;
  homeLoaded = false;
}
void loadHome() {
  if (homeLoaded) return;
  uint32_t started = millis();
  buildClock(); buildPhoto(); buildStrip(); homeLoaded = true;
  // Force text refresh even when returning in the same minute.
  lv_label_set_text(timeLabel, "");
  applyPortal(); applyNowPlaying(); refreshFace(); showView(View::Clock);
  debuglog::printf("[pager] clock build %u ms\n",(unsigned)(millis()-started));
}
void applyBrightness() {
  static int previous = -1;
  int wanted = ringing ? 100 : nightDim ? 4 : display::BACKLIGHT_PCT;
  if (wanted != previous) { display::setBacklight(wanted); previous = wanted; }
}
void requestMode(Mode next) { requestedMode = (int)next; }
lv_obj_t *modeText(const char *text, int y, const lv_font_t *font = &lv_font_montserrat_16) {
  auto *l = label(modeRoot, font, nightDim ? lv_color_hex(0x990D0D) : INK);
  lv_label_set_text(l, text); lv_obj_set_pos(l, 0, y); return l;
}
void modeButton(const char *text, int x, int y, int width, void (*action)()) {
  auto *b = lv_button_create(modeRoot); lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_size(b, width, 38); lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_color(b, nightDim ? lv_color_hex(0x240000) : lv_color_hex(0xEFE1DB), 0);
  auto *l = label(b, &lv_font_montserrat_14, nightDim ? lv_color_hex(0xAA2222) : INK);
  lv_label_set_text(l, text); lv_obj_center(l);
  lv_obj_add_event_cb(b, [](lv_event_t *e) { ((void (*)())lv_event_get_user_data(e))(); }, LV_EVENT_CLICKED, (void *)action);
}
void newModeRoot() {
  if (modeRoot) lv_obj_delete(modeRoot);
  focusview::clear(false);
  modeTime = modeRing = modeCaption = modePanda = nightSpotify = nullptr;
  modeRoot = lv_obj_create(pages[(int)mode]); lv_obj_remove_style_all(modeRoot);
  lv_obj_set_size(modeRoot, 320, 240); lv_obj_remove_flag(modeRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(modeRoot, nightDim ? lv_color_black() : BG, 0);
  lv_obj_set_style_bg_opa(modeRoot, LV_OPA_COVER, 0);
}
void startFocus(unsigned minutes) { focusTimer.start(millis(), minutes); requestMode(Mode::Focus); }
void drawPanda() {
  // Small LVGL primitives: no bitmap allocation or dependency on downloaded art.
  modePanda = box(modeRoot, LV_FLEX_FLOW_ROW); lv_obj_set_size(modePanda, 56, 42);
  lv_obj_set_pos(modePanda, 132, 143); lv_obj_set_layout(modePanda, LV_LAYOUT_NONE);
  auto ellipse = [](lv_obj_t *p, int x, int y, int w, int h, uint32_t col) {
    auto *o = lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(o,lv_color_hex(col),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
  };
  ellipse(modePanda,4,0,14,14,0x292424); ellipse(modePanda,38,0,14,14,0x292424);
  ellipse(modePanda,7,5,42,30,0xFFFFFF);
  ellipse(modePanda,15,14,10,12,0x292424); ellipse(modePanda,32,14,10,12,0x292424);
  ellipse(modePanda,19,17,3,3,0xFFFFFF); ellipse(modePanda,35,17,3,3,0xFFFFFF);
  ellipse(modePanda,26,25,5,3,0x292424);
  auto *book = lv_obj_create(modePanda); lv_obj_remove_style_all(book); lv_obj_set_pos(book,8,30); lv_obj_set_size(book,40,10);
  lv_obj_set_style_bg_color(book,ACCENT,0); lv_obj_set_style_bg_opa(book,LV_OPA_COVER,0);
}
void buildMode() {
  newModeRoot();
  if (mode == Mode::Focus && !nightDim) {
    focusview::build(modeRoot,focusTimer,[](focusview::Action action,unsigned minutes) {
      switch(action) {
        case focusview::Action::Start: startFocus(minutes); break;
        case focusview::Action::End: focusTimer.done(); requestMode(Mode::Focus); break;
        case focusview::Action::TakeBreak: focusTimer.takeBreak(millis()); requestMode(Mode::Focus); break;
        case focusview::Action::Done: focusTimer.done(); requestMode(Mode::Clock); break;
      }
    });
    return;
  }
  if (mode == Mode::Night && (focusTimer.state == focus::State::Idle || focusTimer.running())) {
    modeTime = modeText("", 65, bigFont ? bigFont : &lv_font_montserrat_48);
    nightSpotify = modeText("", 191, &lv_font_montserrat_14);
    lv_label_set_long_mode(nightSpotify, LV_LABEL_LONG_CLIP);
    return;
  }
  if (focusTimer.state == focus::State::Idle) {
    modeText("FOCUS", 28, &lv_font_montserrat_20);
    modeText("A little time, just for studying", 65);
    modeButton("15 min", 15, 108, 90, [](){ startFocus(15); });
    modeButton("25 min", 115, 108, 90, [](){ startFocus(25); });
    modeButton("45 min", 215, 108, 90, [](){ startFocus(45); });
    modeText("Tap a time to start", 181); return;
  }
  if (focusTimer.running()) {
    modeCaption = modeText(focusTimer.quiet() ? "A LITTLE FOCUS" : "TAKE A BREATHER", 8, &lv_font_montserrat_14);
    modeRing = lv_arc_create(modeRoot); lv_obj_set_size(modeRing,190,190); lv_obj_set_pos(modeRing,65,28);
    lv_arc_set_bg_angles(modeRing,0,360); lv_arc_set_rotation(modeRing,270); lv_arc_set_range(modeRing,0,1000);
    lv_obj_remove_style(modeRing,nullptr,LV_PART_KNOB); lv_obj_remove_flag(modeRing,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(modeRing,2,LV_PART_MAIN); lv_obj_set_style_arc_width(modeRing,3,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(modeRing,nightDim ? lv_color_hex(0x240000) : lv_color_hex(0xE9DCD4),LV_PART_MAIN);
    lv_obj_set_style_arc_color(modeRing,nightDim ? lv_color_hex(0x990D0D) : ACCENT,LV_PART_INDICATOR);
    modeTime = modeText("", 75, photoFont ? photoFont : &lv_font_montserrat_48);
    if (!nightDim) drawPanda();
    modeButton("End", 124, 200, 72, [](){ focusTimer.done(); requestMode(Mode::Focus); });
  } else {
    modeText(focusTimer.state == focus::State::Complete ? "A little progress. Well done." : "Break finished", 48, &lv_font_montserrat_20);
    if (focusTimer.state == focus::State::Complete)
      modeButton("Take 5 min", 26, 124, 130, [](){ focusTimer.takeBreak(millis()); requestMode(Mode::Focus); });
    modeButton("Done", 170, 124, 120, [](){ focusTimer.done(); requestMode(Mode::Clock); });
  }
}
void updateModes() {
  bool ended = focusTimer.tick(millis());
  if (ended) {
    if (!ringing) sound::chime();
    requestMode(mode == Mode::Night ? Mode::Night : Mode::Focus);
  }
  if (!pagerMoving && requestedMode >= 0 && !ringing && !video_player::playing() && !moodVisible && !noteVisible) {
    Mode next = (Mode)requestedMode; requestedMode = -1;
    // Programmatic actions (Done/completion) use the very same pager as touch.
    if (abs(lv_obj_get_scroll_x(pager) - (int)next * display::WIDTH) > 1) {
      lv_obj_scroll_to_x(pager, (int)next * display::WIDTH, LV_ANIM_ON);
      return;
    }
    bool changed = next != mode;
    if (next == Mode::Night) nightDim = true;
    if (next == Mode::Clock) nightDim = false;
    mode = next; onlineAt = 0;
    if (next != Mode::Clock) unloadHome();
    // Adjacent pages contain only a title while dragging. Release the old
    // content only after snapping; construct the new screen after its reveal.
    if (modeRoot) { lv_obj_delete(modeRoot); modeRoot = nullptr; }
    focusview::clear(changed || mode != Mode::Focus || nightDim);
    modeTime = modeRing = modeCaption = modePanda = nightSpotify = nullptr;
    lv_obj_set_style_bg_color(pages[(int)mode], nightDim ? lv_color_black() : BG, 0);
    lv_obj_set_style_text_color(pageTitles[(int)mode], nightDim ? lv_color_hex(0x990D0D) : INK, 0);
    debuglog::printf("[ui] page %s (focus=%d)\n", next == Mode::Clock ? "Clock" : next == Mode::Focus ? "Focus" : "Night", (int)focusTimer.state);
    revealAt = millis() | 1;
    if (!changed) revealAt = millis() - MODE_REVEAL_MS - 1;
    applyBrightness();
  }
  if (!pagerMoving && revealAt && millis() - revealAt >= MODE_REVEAL_MS) {
    revealAt = 0;
    if (mode == Mode::Clock) {
      modeTime = modeRing = modeCaption = modePanda = nightSpotify = nullptr;
      if (!homeLoaded) loadHome();
      else { applyPortal(); applyNowPlaying(); refreshFace(); showView(View::Clock); }
    } else buildMode();
    setPagerContentVisible(true);
  }
  static uint32_t lastPaint = 0;
  if (!pagerMoving && !revealAt && modeRoot && millis() - lastPaint >= 250) {
    lastPaint = millis();
    if (focusview::active()) focusview::tick(focusTimer,millis());
    if (modeTime && focusTimer.running()) {
      uint32_t seconds = (focusTimer.remaining(millis()) + 999) / 1000;
      lv_label_set_text_fmt(modeTime,"%02u:%02u",(unsigned)(seconds/60),(unsigned)(seconds%60));
      if (modeRing) lv_arc_set_value(modeRing, (uint64_t)focusTimer.remaining(millis()) * 1000 / focusTimer.duration);
    } else if (modeTime && mode == Mode::Night) {
      time_t now = time(nullptr); struct tm t; localtime_r(&now,&t);
      if (timeValid()) lv_label_set_text_fmt(modeTime,"%02d:%02d",t.tm_hour,t.tm_min);
      else lv_label_set_text(modeTime,"--:--");
    }
    if (nightSpotify) {
      const auto &n = portal::nowPlaying();
      bool visible = !focusTimer.quiet() && portal::nowPlayingFresh();
      const char *left = visible && n.left.status == bhoomi_clock_v1_Listener_Status_PLAYING ? n.left.title : "";
      const char *right = visible && n.right.status == bhoomi_clock_v1_Listener_Status_PLAYING ? n.right.title : "";
      lv_label_set_text_fmt(nightSpotify, "%s%s%s", left, left[0] && right[0] ? "\n" : "", right);
    }
  }
  applyBrightness();
}


void setPagerContentVisible(bool visible) {
  // Opacity avoids reflow or cancelling the pointer's active scroll object.
  // Keep objects alive until the snap ends so a cancelled drag is instant.
  lv_obj_t *roots[] = {clockView, photoView, faceview::root(), noteButton, modeRoot};
  for (auto *root : roots) if (root)
    lv_obj_set_style_opa(root, visible ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}


void finishPagerScroll() {
  pagerMoving = false;
  display::endScrollMetrics();
  int x = lv_obj_get_scroll_x(pager);
  int index = constrain((x + display::WIDTH / 2) / display::WIDTH, 0, 2);
  if (index != (int)mode) requestMode((Mode)index);
  else if (!revealAt) setPagerContentVisible(true);
}

void updatePagerMotion() {
  // LVGL emits BEGIN before registering an animation. If that animation is
  // cancelled before its first tick, it deliberately omits END. Reconcile with
  // the real input/animation state instead of leaving all UI updates paused.
  if (pagerMoving && !lv_obj_is_scrolling(pager) && !lv_anim_get(pager, nullptr)) {
    ++pagerRecoveries;
    finishPagerScroll();
  }
}

void buildPager() {
  pager = lv_obj_create(scr); lv_obj_remove_style_all(pager);
  lv_obj_set_size(pager, display::WIDTH, display::HEIGHT);
  lv_obj_set_scroll_dir(pager, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(pager, LV_SCROLL_SNAP_START);
  lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_remove_flag(pager, LV_OBJ_FLAG_SCROLL_ELASTIC);
  const char *names[] = {"Clock", "Focus", "Night"};
  for (int i = 0; i < 3; ++i) {
    pages[i] = lv_obj_create(pager); lv_obj_remove_style_all(pages[i]);
    lv_obj_set_size(pages[i], display::WIDTH, display::HEIGHT);
    lv_obj_set_pos(pages[i], i * display::WIDTH, 0);
    lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pages[i], LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_set_style_bg_color(pages[i], i == 2 ? lv_color_black() : BG, 0);
    lv_obj_set_style_bg_opa(pages[i], LV_OPA_COVER, 0);
    pageTitles[i] = label(pages[i], &lv_font_montserrat_28, i == 2 ? lv_color_hex(0x990D0D) : INK);
    lv_label_set_text(pageTitles[i], names[i]); lv_obj_center(pageTitles[i]);
  }
  lv_obj_add_event_cb(pager, [](lv_event_t *e) {
    if (lv_event_get_target(e) != pager) return;
    if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
      pagerMoving = true;
      setPagerContentVisible(false);
      display::beginScrollMetrics();
      return;
    }
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) return;
    finishPagerScroll();
  }, LV_EVENT_ALL, nullptr);
}

}  // namespace

void begin(const char *apName) {
  ap = apName;
  bigFont = lv_tiny_ttf_create_data(montserrat_medium_ttf, montserrat_medium_ttf_len, 92);
  photoFont = lv_tiny_ttf_create_data(montserrat_medium_ttf, montserrat_medium_ttf_len, 56);

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  buildPager();
  buildSetup();
  buildMsg();
  buildClock();
  buildPhoto();
  faceview::begin(pages[0]);
  buildStrip();
  buildNote();
  mindfulness::begin();
  buildMood();
  video_player::begin();
  buildAlarm();  // alarms take priority over check-ins and clips
  showMsg(true, "Waking up...", "");
}

void printStats() {
  debuglog::printf("[uistats] {\"mode\":\"%s\",\"pager_moving\":%s,\"pager_busy\":%s,\"offset\":%ld,\"recoveries\":%u,\"focus_art_bytes\":%u,\"log_dropped\":%u,\"log_buffered\":%u}\n",
    mode == Mode::Clock ? "Clock" : mode == Mode::Focus ? "Focus" : "Night",
    pagerMoving ? "true" : "false",
    (lv_obj_is_scrolling(pager) || lv_anim_get(pager,nullptr)) ? "true" : "false",
    (long)lv_obj_get_scroll_x(pager),(unsigned)pagerRecoveries,(unsigned)focusview::artworkBytes(),(unsigned)debuglog::dropped(),(unsigned)debuglog::buffered());
}

void testMindfulness() { if (timeValid() && mode == Mode::Clock && !focusTimer.quiet()) showMood(); else debuglog::println("[mindfulness] waiting for clock time"); }

void testAlarm(const char *soundFile) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  startRing(t.tm_hour, t.tm_min, "Test alarm", soundFile ? soundFile : "");
}

void showWifi(const wifisetup::Status &s) {
  phase = s.phase;
  if (pagerMoving || mode != Mode::Clock || !homeLoaded || revealAt) return;
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
  updatePagerMotion();
  updateModes();
  static uint32_t last = 0, lastFaceTick = 0;
  if (!pagerMoving && portal::revision() != seenPortalRev) {
    seenPortalRev = portal::revision();
    applyPortal();
  }
  if (!pagerMoving && portal::nowPlayingRevision() != seenNpRev) {
    seenNpRev = portal::nowPlayingRevision();
    applyNowPlaying();
    updateNowPlaying();
  }
  if (!pagerMoving && (portal::facesRevision() != seenFacesRev || (faceShown && portal::weatherFaceGroup() != faceShownWeather))) {
    seenFacesRev = portal::facesRevision();
    refreshFace();
  }
  checkAlarms();
  updateAlarm();
  if (!pagerMoving) updateMood();
  if (noteVisible && millis() - noteSince >= 5000) finishMoodNote();
  video_player::tick();
  if (faceSuspendedForVideo && !video_player::playing()) {
    faceSuspendedForVideo = false;
    refreshFace();
  }
  if (onlineAt && millis() - onlineAt > SPLASH_MS) {
    onlineAt = 0;
    if (phase == Phase::Online) showView(View::Clock);
  }
  if (!pagerMoving && homeLoaded && mode == Mode::Clock && inClock() && millis() - last >= 250) {
    last = millis();
    updateClock();
    updateNowPlaying();
    updateNote();
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (timeValid() && t.tm_min != faceCheckedMin) refreshFace();
  }
  if (!pagerMoving && homeLoaded && mode == Mode::Clock && current == View::Face && millis() - lastFaceTick >= FACE_TICK_MS) {  // the face's own, faster animation tick
    lastFaceTick = millis();
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    faceview::tick(t);
  }
}

}  // namespace ui
