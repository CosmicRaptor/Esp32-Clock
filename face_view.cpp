#include "face_view.h"

#include <esp_heap_caps.h>
#include <jpeg_decoder.h>  // ESP-IDF esp_jpeg (TJpgDec): baseline JPEG -> RGB565
#include <limits.h>

#include "display.h"
#include "face_motion.h"

extern "C" const uint8_t montserrat_medium_ttf[];
extern "C" const size_t montserrat_medium_ttf_len;

namespace faceview {
namespace {

using namespace facemotion;
using SpriteRef = bhoomi_clock_v1_SpriteRef;
using TextSlot = bhoomi_clock_v1_TextSlot;

// What a face may hold (portal/src/model.py has the same limits; the portal never hands out more).
constexpr int MAX_SPRITES = 20;   // distinct sprite files: 8 layers + icon + 3 emitters x 3 sprites
constexpr int MAX_INST = 24;      // sprite images from layers and the icon, copies counted
constexpr int MAX_PART = 60;      // particles in all
constexpr int MAX_EMIT = 3;
constexpr uint32_t BG_BYTES = display::WIDTH * display::HEIGHT * 2;

struct Spr {
  char hash[17] = "";
  uint8_t *mem = nullptr;  // our copy of the RGB565A8 file
  lv_image_dsc_t dsc;
};

// One sprite image on screen: a copy of a layer (or the icon).
struct Inst {
  lv_obj_t *img;
  const Layer *L;
  int copy;
  int fw, fh;  // one frame
  int frames;
  int x, y, scale, opa, frame;  // what is set on the object now, so unchanged values cost nothing
  bool hidden;
};

struct Part {
  lv_obj_t *img;
  const Emitter *E;
  ParticleParams q;
  int w, h;
  int x, y, opa;
  bool hidden;
};

struct Txt {
  lv_obj_t *lbl = nullptr, *shadow = nullptr;
  const TextSlot *S = nullptr;
  char shown[40] = "";
};

portal::Face face;  // our own copy of the face we're drawing
bool on = false;
uint32_t shownAt = 0;

lv_obj_t *rootObj = nullptr;     // always there, hidden or not
lv_obj_t *content = nullptr;     // everything of the current face; deleted as a whole
lv_image_dsc_t bgDsc;
uint8_t *bgPx = nullptr;
Spr sprs[MAX_SPRITES];
int sprN = 0;
Inst insts[MAX_INST];
int instN = 0;
Part parts[MAX_PART];
int partN = 0;
Txt timeT, dateT;
lv_font_t *fontTime = nullptr, *fontDate = nullptr;
int fontTimeSize = 0, fontDateSize = 0;

void freeSprites(Spr *list, int n) {
  for (int i = 0; i < n; i++) {
    heap_caps_free(list[i].mem);
    list[i].mem = nullptr;
  }
}

// Decodes the 320x240 background once into PSRAM; LVGL then just blits it.
uint8_t *decodeBackground(const uint8_t *jpg, size_t n) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = (uint8_t *)jpg;
  cfg.indata_size = n;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || info.width != display::WIDTH || info.height != display::HEIGHT ||
      info.output_len != BG_BYTES) {
    Serial.println("[face] background: not a 320x240 JPEG");
    return nullptr;
  }
  uint8_t *px = (uint8_t *)heap_caps_malloc(BG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!px) {
    Serial.println("[face] background: out of memory");
    return nullptr;
  }
  cfg.outbuf = px;
  cfg.outbuf_size = BG_BYTES;
  if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
    Serial.println("[face] background: decode failed");
    heap_caps_free(px);
    return nullptr;
  }
  return px;
}

// Copies a sprite file out of the portal client's cache and wraps it for LVGL: the file is the 12-byte
// lv_image_header_t followed by the RGB565 and A8 planes, so the image data is just the bytes after the header.
bool loadSprite(Spr *list, int &n, const SpriteRef &r) {
  for (int i = 0; i < n; i++)
    if (strcmp(list[i].hash, r.hash) == 0) return true;
  if (n >= MAX_SPRITES) return false;
  size_t len = 0;
  const uint8_t *f = portal::asset(r.hash, &len);
  if (!f || len < 12 || f[0] != LV_IMAGE_HEADER_MAGIC || f[1] != LV_COLOR_FORMAT_RGB565A8) return false;
  uint32_t w = f[4] | (f[5] << 8), h = f[6] | (f[7] << 8);
  if (!w || !h || len != 12 + 3 * (size_t)w * h) return false;
  uint8_t *mem = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) return false;
  memcpy(mem, f, len);
  Spr &s = list[n++];
  strlcpy(s.hash, r.hash, sizeof s.hash);
  s.mem = mem;
  memset(&s.dsc, 0, sizeof s.dsc);
  s.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s.dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
  s.dsc.header.w = w;
  s.dsc.header.h = h;
  s.dsc.header.stride = w * 2;
  s.dsc.data = mem + 12;
  s.dsc.data_size = len - 12;
  return true;
}

Spr *sprOf(const char *hash) {
  for (int i = 0; i < sprN; i++)
    if (strcmp(sprs[i].hash, hash) == 0) return &sprs[i];
  return nullptr;
}

lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

lv_obj_t *makeImage(lv_obj_t *parent, Spr *s, int w, int h) {
  lv_obj_t *img = lv_image_create(parent);
  lv_obj_remove_flag(img, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_image_set_src(img, &s->dsc);
  lv_obj_set_size(img, w, h);  // a frame of a sheet: the image is wider than this and the offset picks the cell
  lv_image_set_inner_align(img, LV_IMAGE_ALIGN_TOP_LEFT);
  lv_image_set_pivot(img, w / 2, h / 2);  // pulse scales about the centre
  return img;
}

void setHidden(lv_obj_t *o, bool hide) {
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void addLayer(const Layer &L) {
  Spr *s = sprOf(L.sprite.hash);
  int frames = frameCount(L);
  int fw = s->dsc.header.w / frames, fh = s->dsc.header.h;
  int copies = L.copies ? L.copies : 1;
  for (int c = 0; c < copies && instN < MAX_INST; c++) {
    Inst &I = insts[instN++];
    I = {makeImage(content, s, fw, fh), &L, c, fw, fh, frames, INT_MIN, INT_MIN, INT_MIN, -1, -1, false};
  }
}

void addEmitter(const Emitter &E) {
  lv_obj_t *box = plain(content);  // clips its children: particles never leave the area
  lv_obj_set_pos(box, E.area.x, E.area.y);
  lv_obj_set_size(box, E.area.w, E.area.h);
  for (uint32_t i = 0; i < E.count && partN < MAX_PART; i++) {
    ParticleParams q = particleParams(E, i);
    Spr *s = sprOf(E.sprites[q.sprite].hash);
    Part &P = parts[partN++];
    P = {makeImage(box, s, s->dsc.header.w, s->dsc.header.h), &E, q, (int)s->dsc.header.w, (int)s->dsc.header.h, INT_MIN, INT_MIN, -1, false};
  }
}

lv_font_t *makeFont(int size, lv_font_t *share, int shareSize) {
  if (share && shareSize == size) return share;
  return lv_tiny_ttf_create_data(montserrat_medium_ttf, montserrat_medium_ttf_len, size);
}

void makeText(Txt &T, const TextSlot &S, const lv_font_t *font) {
  T.S = &S;
  T.shown[0] = 0;
  T.shadow = nullptr;
  if (S.has_shadow) {
    T.shadow = lv_label_create(content);
    lv_obj_set_style_text_font(T.shadow, font, 0);
    lv_obj_set_style_text_color(T.shadow, lv_color_hex(S.shadow), 0);
  }
  T.lbl = lv_label_create(content);
  lv_obj_set_style_text_font(T.lbl, font, 0);
  lv_obj_set_style_text_color(T.lbl, lv_color_hex(S.color), 0);
}

// y is the top of the line box (the label's top); x is its left edge, centre or right edge, by `align`.
void setText(Txt &T, const char *s) {
  if (!T.lbl || strcmp(T.shown, s) == 0) return;
  strlcpy(T.shown, s, sizeof T.shown);
  lv_label_set_text(T.lbl, s);
  lv_obj_update_layout(T.lbl);
  int w = lv_obj_get_width(T.lbl);
  int x = T.S->x;
  if (T.S->align == bhoomi_clock_v1_TextSlot_Align_CENTER) x -= w / 2;
  else if (T.S->align == bhoomi_clock_v1_TextSlot_Align_RIGHT) x -= w;
  lv_obj_set_pos(T.lbl, x, T.S->y);
  if (T.shadow) {
    int off = T.S->size / 24 > 1 ? T.S->size / 24 : 1;
    lv_label_set_text(T.shadow, s);
    lv_obj_set_pos(T.shadow, x + off, T.S->y + off);
  }
}

void formatDate(char *out, size_t n, int format, const struct tm &t) {
  char wday[12], month[12];
  strftime(wday, sizeof wday, "%A", &t);
  strftime(month, sizeof month, "%B", &t);
  switch (format) {
    case bhoomi_clock_v1_TextSlot_DateFormat_SHORT: snprintf(out, n, "%.3s, %d %.3s", wday, t.tm_mday, month); break;
    case bhoomi_clock_v1_TextSlot_DateFormat_DAY_MONTH: snprintf(out, n, "%d %s", t.tm_mday, month); break;
    case bhoomi_clock_v1_TextSlot_DateFormat_WEEKDAY: snprintf(out, n, "%s", wday); break;
    case bhoomi_clock_v1_TextSlot_DateFormat_NUMERIC: snprintf(out, n, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900); break;
    default: snprintf(out, n, "%s, %d %s", wday, t.tm_mday, month);
  }
}

void freeFonts() {
  if (fontDate && fontDate != fontTime) lv_tiny_ttf_destroy(fontDate);
  if (fontTime) lv_tiny_ttf_destroy(fontTime);
  fontTime = fontDate = nullptr;
}

void updateInst(Inst &I, uint32_t t) {
  Pose p = layerPose(*I.L, I.copy, t);
  int x = roundHalfUp(I.L->x + I.copy * I.L->stagger_x + p.dx);
  int y = roundHalfUp(I.L->y + I.copy * I.L->stagger_y + p.dy);
  if (x != I.x || y != I.y) {
    lv_obj_set_pos(I.img, x, y);
    I.x = x;
    I.y = y;
  }
  int scale = roundHalfUp(p.scale * 256 / 100);
  scale = I.frames > 1 ? 256 : scale < 64 ? 64 : scale > 640 ? 640 : scale;  // a sheet's other frames would show: the portal refuses pulse + frames
  if (scale != I.scale) {
    lv_image_set_scale(I.img, scale);
    I.scale = scale;
  }
  int opa = roundHalfUp(p.opacity * 255 / 100);
  opa = opa < 0 ? 0 : opa > 255 ? 255 : opa;
  if (opa != I.opa) {
    lv_obj_set_style_image_opa(I.img, opa, 0);
    I.opa = opa;
    if ((opa == 0) != I.hidden) {
      I.hidden = opa == 0;
      setHidden(I.img, I.hidden);
    }
  }
  if (I.frames > 1 && p.frame != I.frame) {
    lv_image_set_offset_x(I.img, -p.frame * I.fw);
    I.frame = p.frame;
  }
}

void updatePart(Part &P, uint32_t t) {
  ParticlePos p = particlePos(*P.E, P.q, P.w, P.h, t);
  int x = p.x - P.E->area.x, y = p.y - P.E->area.y;  // relative to the emitter's box
  if (x != P.x || y != P.y) {
    lv_obj_set_pos(P.img, x, y);
    P.x = x;
    P.y = y;
  }
  if (P.E->kind == bhoomi_clock_v1_Emitter_Kind_TWINKLE) {
    int opa = roundHalfUp(p.opacity * 255 / 100);
    opa = opa < 0 ? 0 : opa > 255 ? 255 : opa;
    if (opa != P.opa) {
      lv_obj_set_style_image_opa(P.img, opa, 0);
      P.opa = opa;
      if ((opa == 0) != P.hidden) {
        P.hidden = opa == 0;
        setHidden(P.img, P.hidden);
      }
    }
  }
}

}  // namespace

void begin(lv_obj_t *parent) {
  rootObj = plain(parent);
  lv_obj_set_size(rootObj, lv_pct(100), lv_pct(100));
  lv_obj_add_flag(rootObj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *root() { return rootObj; }
bool active() { return on; }

void clear() {
  if (content) {  // objects first: they point at the sprites, the fonts and the background
    lv_obj_delete(content);
    content = nullptr;
  }
  freeFonts();
  freeSprites(sprs, sprN);
  sprN = instN = partN = 0;
  heap_caps_free(bgPx);
  bgPx = nullptr;
  timeT = Txt();
  dateT = Txt();
  on = false;
}

bool show(const portal::Face &f) {
  if (!rootObj || !f.has_background || !f.has_time) return false;

  // 1. Get everything ready without touching what is on screen.
  size_t n = 0;
  const uint8_t *jpg = portal::asset(f.background.hash, &n);
  if (!jpg) return false;
  uint8_t *px = decodeBackground(jpg, n);
  if (!px) return false;

  Spr tmp[MAX_SPRITES];
  int tmpN = 0;
  bool ok = true;
  int inst = f.has_icon ? (f.icon.copies ? f.icon.copies : 1) : 0, particles = 0;
  if (f.has_icon) ok = ok && f.icon.has_sprite && loadSprite(tmp, tmpN, f.icon.sprite);
  for (pb_size_t i = 0; ok && i < f.layers_count; i++) {
    ok = f.layers[i].has_sprite && loadSprite(tmp, tmpN, f.layers[i].sprite);
    inst += f.layers[i].copies ? f.layers[i].copies : 1;
  }
  for (pb_size_t i = 0; ok && i < f.emitters_count; i++) {
    const Emitter &E = f.emitters[i];
    ok = E.has_area && E.sprites_count > 0;
    for (pb_size_t k = 0; ok && k < E.sprites_count; k++) ok = loadSprite(tmp, tmpN, E.sprites[k]);
    particles += E.count;
  }
  if (!ok || inst > MAX_INST || particles > MAX_PART) {
    Serial.printf("[face] %s: can't be shown (a file is missing or broken, or it is too big)\n", f.id);
    freeSprites(tmp, tmpN);
    heap_caps_free(px);
    return false;
  }
  int sizeTime = f.time.size, sizeDate = f.has_date ? f.date.size : 0;
  lv_font_t *ft = makeFont(sizeTime, nullptr, 0);
  lv_font_t *fd = f.has_date ? makeFont(sizeDate, ft, sizeTime) : nullptr;
  if (!ft || (f.has_date && !fd)) {
    Serial.printf("[face] %s: no memory for its fonts\n", f.id);
    if (fd && fd != ft) lv_tiny_ttf_destroy(fd);
    if (ft) lv_tiny_ttf_destroy(ft);
    freeSprites(tmp, tmpN);
    heap_caps_free(px);
    return false;
  }

  // 2. Swap.
  clear();
  face = f;
  bgPx = px;
  for (int i = 0; i < tmpN; i++) sprs[i] = tmp[i];
  sprN = tmpN;
  fontTime = ft;
  fontDate = fd;

  content = plain(rootObj);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));
  lv_obj_move_to_index(content, 0);  // under the now-playing strip

  memset(&bgDsc, 0, sizeof bgDsc);
  bgDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  bgDsc.header.cf = LV_COLOR_FORMAT_RGB565;
  bgDsc.header.w = display::WIDTH;
  bgDsc.header.h = display::HEIGHT;
  bgDsc.header.stride = display::WIDTH * 2;
  bgDsc.data = bgPx;
  bgDsc.data_size = BG_BYTES;
  lv_obj_t *bg = lv_image_create(content);
  lv_image_set_src(bg, &bgDsc);
  lv_obj_set_pos(bg, 0, 0);

  // back to front: back emitters, layers, front emitters, time, date, icon
  for (pb_size_t i = 0; i < face.emitters_count; i++)
    if (!face.emitters[i].front) addEmitter(face.emitters[i]);
  for (pb_size_t i = 0; i < face.layers_count; i++) addLayer(face.layers[i]);
  for (pb_size_t i = 0; i < face.emitters_count; i++)
    if (face.emitters[i].front) addEmitter(face.emitters[i]);
  makeText(timeT, face.time, fontTime);
  if (face.has_date) makeText(dateT, face.date, fontDate);
  if (face.has_icon) addLayer(face.icon);

  on = true;
  shownAt = millis();
  Serial.printf("[face] %s (%s): %d sprite image(s), %d particle(s), %d file(s)\n", face.id, face.rev, instN, partN, sprN);
  time_t now = time(nullptr);  // place everything now, so the first frame isn't a pile of sprites in the corner
  struct tm t;
  localtime_r(&now, &t);
  tick(t);
  return true;
}

void tick(const struct tm &now) {
  if (!on) return;
  uint32_t t = millis() - shownAt;
  for (int i = 0; i < instN; i++) updateInst(insts[i], t);
  for (int i = 0; i < partN; i++) updatePart(parts[i], t);
  char buf[40];
  strftime(buf, sizeof buf, "%H:%M", &now);
  setText(timeT, buf);
  if (face.has_date) {
    formatDate(buf, sizeof buf, face.date.format, now);
    setText(dateT, buf);
  }
}

}  // namespace faceview
