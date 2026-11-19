#include "debug_log.h"
#include "focus_art.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>
#include "focus_study_jpg.h"
#include "storage.h"

namespace focusart {
namespace {
constexpr const char *PATH = "/ui/focus-study-v1.jpg";
constexpr const char *TEMP = "/ui/focus-study-v1.jpg.part";
constexpr size_t PIXELS = 320 * 240 * 2;
bool matches() {
  File f = SD_MMC.open(PATH, FILE_READ);
  if (!f || f.size() != sizeof FOCUS_STUDY_JPG) return false;
  uint8_t chunk[512];
  for (size_t at=0; at<sizeof FOCUS_STUDY_JPG;) {
    size_t n = min(sizeof chunk, sizeof FOCUS_STUDY_JPG-at);
    if (f.read(chunk,n) != n || memcmp(chunk,FOCUS_STUDY_JPG+at,n)) return false;
    at += n;
  }
  return true;
}
}
bool install() {
  if (!storage::mounted()) return false;
  if (matches()) return true;
  if (!SD_MMC.exists("/ui") && !SD_MMC.mkdir("/ui")) return false;
  File f = SD_MMC.open(TEMP, FILE_WRITE);
  if (!f) return false;
  bool ok = true;
  for (size_t at=0; at<sizeof FOCUS_STUDY_JPG;) {
    size_t n = min((size_t)1024, sizeof FOCUS_STUDY_JPG-at);
    if (f.write(FOCUS_STUDY_JPG+at,n) != n) { ok=false; break; }
    at += n; delay(1);
  }
  f.close();
  if (ok && SD_MMC.exists(PATH)) ok = SD_MMC.remove(PATH);
  if (ok) ok = SD_MMC.rename(TEMP,PATH);
  if (!ok) SD_MMC.remove(TEMP);
  debuglog::printf("[focus-art] SD install %s (%u bytes)\n",ok ? "ok" : "failed",(unsigned)sizeof FOCUS_STUDY_JPG);
  return ok;
}
uint8_t *load() {
  uint32_t started=millis();
  if (!storage::mounted()) return nullptr;
  File f=SD_MMC.open(PATH, FILE_READ);
  if (!f || !f.size() || f.size()>64*1024) return nullptr;
  size_t size=f.size();
  auto *jpg=(uint8_t *)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if (!jpg) return nullptr;
  bool ok=true;
  for(size_t at=0;at<size;) {
    size_t n=min((size_t)1024,size-at);
    if(f.read(jpg+at,n)!=n){ok=false;break;}
    at+=n;
  }
  f.close();
  esp_jpeg_image_cfg_t cfg={};cfg.indata=jpg;cfg.indata_size=size;
  cfg.out_format=JPEG_IMAGE_FORMAT_RGB565;cfg.out_scale=JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info={};
  uint8_t *px=nullptr;
  if(ok && esp_jpeg_get_image_info(&cfg,&info)==ESP_OK && info.width==320 && info.height==240 && info.output_len==PIXELS) {
    px=(uint8_t *)heap_caps_malloc(PIXELS,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(px){cfg.outbuf=px;cfg.outbuf_size=PIXELS;if(esp_jpeg_decode(&cfg,&info)!=ESP_OK){free(px);px=nullptr;}}
  }
  free(jpg);
  debuglog::printf("[focus-art] SD decode %s (%u ms)\n",px ? "ok" : "failed",(unsigned)(millis()-started));
  return px;
}
}
