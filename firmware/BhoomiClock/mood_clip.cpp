#include "mood_clip.h"
#include "app_lock.h"
#include "video_player.h"
#include <Arduino.h>
#include <SD_MMC.h>

namespace mood_clip {
namespace {
SemaphoreHandle_t mutex() { static SemaphoreHandle_t m = xSemaphoreCreateMutex(); return m; }
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
void clearDir(const String &path) {
  for (int i=0; i<100; ++i) { char n[16]; snprintf(n,sizeof(n),"/%04d.jpg",i); SD_MMC.remove(path+n); }
  SD_MMC.remove(path+"/audio.wav"); SD_MMC.rmdir(path);
}
bool copy(File &source, const String &path, uint32_t length) {
  File out=SD_MMC.open(path, FILE_WRITE); if (!out) return false;
  uint8_t buf[2048]; bool ok=true;
  while (length && ok) {
    size_t n=std::min<size_t>(length,sizeof(buf));
    if (source.read(buf,n)!=n || out.write(buf,n)!=n) ok=false;
    length-=n; vTaskDelay(1);
  }
  out.close(); return ok;
}
}
bool install(const char *path, unsigned mood) {
  if (mood < 1 || mood > 5 || xSemaphoreTake(mutex(), 0) != pdTRUE) return false;
  bool result=false; File in=SD_MMC.open(path, FILE_READ);
  String root="/mindfulness", active=root+"/"+String(mood), temp=active+".new", backup=active+".old";
  do {
    if (!in || in.size() > 12*1024*1024) break;
    uint8_t head[12]; if (in.read(head,12)!=12 || memcmp(head,"BCV1",4)) break;
    uint32_t count=u32(head+4), audio=u32(head+8), sizes[100];
    if (!count || count>100 || audio<44 || audio>44+20*22050*2) break;
    uint64_t expected=12+count*4+audio;
    for (unsigned i=0;i<count;++i) { uint8_t b[4]; if(in.read(b,4)!=4) { expected=0; break; } sizes[i]=u32(b); if(!sizes[i] || sizes[i]>100000) { expected=0; break; } expected+=sizes[i]; }
    if (expected != in.size()) break;
    if (!SD_MMC.exists(root)) SD_MMC.mkdir(root);
    if (!SD_MMC.exists(active) && SD_MMC.exists(backup)) SD_MMC.rename(backup,active);
    clearDir(temp); if (!SD_MMC.mkdir(temp)) break;
    bool ok=true;
    for (unsigned i=0;i<count && ok;++i) {
      char name[16]; snprintf(name,sizeof(name),"/%04u.jpg",i);
      ok=copy(in,temp+name,sizes[i]);
    }
    if (ok) ok=copy(in,temp+"/audio.wav",audio);
    if (!ok) { clearDir(temp); break; }
    clearDir(backup);
    {
      app::Lock lock;
      if (video_player::playing()) { clearDir(temp); break; }
      bool old=SD_MMC.exists(active);
      if (old && !SD_MMC.rename(active,backup)) { clearDir(temp); break; }
      if (!SD_MMC.rename(temp,active)) { if(old) SD_MMC.rename(backup,active); clearDir(temp); break; }
      result=true;
    }
    if (result) clearDir(backup);
  } while(false);
  in.close(); xSemaphoreGive(mutex()); return result;
}
}
