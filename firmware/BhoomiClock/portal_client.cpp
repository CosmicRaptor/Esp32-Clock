#include "portal_client.h"
#include "face_weather.h"
#include "face_cache_checks.h"
#include "mindfulness.h"
#include "volume_sync.h"
#include "mood_clip.h"
#include <math.h>

#include <FFat.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "cooperative_client.h"
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "ota_health.h"
#include "sound.h"
#include <mbedtls/platform.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "app_lock.h"
#include "face_rules.h"
#include "settings.h"
#include "storage.h"
#include "wifi_setup.h"

#if __has_include("portal_config.h")
#include "portal_config.h"
#else
#include "portal_config.example.h"
#endif
#ifndef PORTAL_ACCESS_CLIENT_ID  // older portal_config.h files: no Cloudflare Access in front
#define PORTAL_ACCESS_CLIENT_ID ""
#define PORTAL_ACCESS_CLIENT_SECRET ""
#endif

namespace portal {
namespace {

constexpr uint32_t POLL_MS = 30 * 1000;         // the one heartbeat: state + now playing. Mostly a bare 304
constexpr uint32_t BACKOFF_MIN_MS = 10 * 1000;  // after a failed poll: 10 s, doubling up to FAIL_MAX_MS
constexpr uint32_t FAIL_MAX_MS = 60 * 1000;
constexpr int PHOTO_TRIES_FAST = 6;             // photo pending (404 while KV propagates, or a bad download): every poll...
constexpr uint32_t PHOTO_SLOW_MS = 60 * 1000;   // ...then once a minute
constexpr uint32_t NP_STALE_MS = 2 * 60 * 1000; // no answer from the portal for this long: the strip is hidden, not stale
constexpr int32_t CONNECT_TIMEOUT_MS = 12000;   // weak Wi-Fi (~-90 dBm): TLS handshake is slow
constexpr uint16_t IO_TIMEOUT_MS = 15000;
constexpr size_t MAX_PHOTO_BYTES = 256 * 1024;  // a 320x240 JPEG is ~10-40 KB; refuse anything silly

// ---------- TLS memory ----------
// This core's mbedTLS keeps everything in internal RAM (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC), including
// two fixed 16 KB record buffers, so a handshake needs ~33 KB in one piece and the largest free block
// is only ~31 KB. Route the big allocations to PSRAM instead; small ones (bignum limbs, hash state,
// ...) stay internal where they are faster. mbedTLS frees through the same hook, and free() works on
// either heap.
constexpr size_t TLS_PSRAM_MIN = 1024;

void *tlsCalloc(size_t n, size_t size) {
  void *p = nullptr;
  if (n && size && n <= SIZE_MAX / size && n * size >= TLS_PSRAM_MIN)
    p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void tlsFree(void *p) { heap_caps_free(p); }

// ---- latest state (guarded by app::Lock) ----
State cur;                  // zeroed until the first successful decode
bool have = false;
uint8_t *photoBuf = nullptr;
size_t photoLen = 0;
char photoIdStr[13] = "";
uint32_t rev = 0;       // the portal's own things changed (note, photo, alarms)
uint32_t okAt = 0;      // millis() of the last answer from the portal (200 or 304); 0 = none yet
uint32_t npRev = 0;     // what is playing, or a cover, changed
uint8_t *coverBuf[2] = {nullptr, nullptr};  // 64x64 JPEGs for the left / right side of the strip
size_t coverLen[2] = {0, 0};
char coverIdStr[2][41] = {"", ""};

// ---- task-private ----
// Volume is guarded by app::Lock. Pending local edits survive reboot and beat cloud values
// until the server confirms the same pair; subsequent admin edits can then take effect.
VolumeState volumeState = {1, 18, 18, 0};
bool saveVolume(const VolumeState &v) {
  Preferences p; if (!p.begin("volume", false)) return false;
  bool ok = p.putBytes("state", &v, sizeof(v)) == sizeof(v); p.end(); return ok;
}
String moodNotes[5];
String etag;
bool gotTimeWarn = false;
bool quiet = false;  // don't log routine polls (the 30 s heartbeat would drown the serial monitor)

// ---- keys (guarded by app::Lock) ----
// The device token and the Cloudflare Access service token expire or get rotated, so they can be
// changed from the LAN settings page. A value saved there (NVS namespace "portal") wins over the
// built-in one from portal_config.h; saving "" goes back to the built-in one. Values never leave
// the clock except in the request headers: the settings page only ever sees the last few characters.
const char *const KEY_NVS[KEY_COUNT] = {"token", "cf_id", "cf_secret"};
const char *const KEY_JSON[KEY_COUNT] = {"token", "accessId", "accessSecret"};
const char *const KEY_BUILTIN[KEY_COUNT] = {PORTAL_DEVICE_TOKEN, PORTAL_ACCESS_CLIENT_ID, PORTAL_ACCESS_CLIENT_SECRET};
constexpr size_t KEY_MAX = 200;
String keyVal[KEY_COUNT];
bool keyCustom[KEY_COUNT] = {};
uint32_t keysRev = 1;  // bumped on every change; the task takes a fresh copy
int lastCode = 0;      // the last state poll: HTTP status, <0 = no connection, 0 = none yet
uint32_t lastAt = 0;   // millis() of it

// ---- task-private copy of the keys, used for every request ----
String active[KEY_COUNT];

bool https() { return strncmp(PORTAL_URL, "https://", 8) == 0; }

String urlEncode(const char *s) {
  String out;
  for (; *s; s++) {
    unsigned char c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else {
      char b[4];
      snprintf(b, sizeof b, "%%%02X", c);
      out += b;
    }
  }
  return out;
}

bool timeValid() { return time(nullptr) > 1700000000; }  // TLS needs a sane clock to check certificates

// An HTTPClient plus the right kind of socket. The task keeps one for good: a TLS handshake over weak
// Wi-Fi takes seconds, and we poll every 30 s.
struct Conn {
  CooperativeClient<WiFiClient> plain;
  CooperativeClient<WiFiClientSecure> secure;
  HTTPClient http;
  Conn() {
    if (https()) secure.useBuiltinCACertBundle();  // the ESP-IDF Mozilla bundle; validates chain + host name
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(IO_TIMEOUT_MS);
    http.setReuse(true);
    http.useHTTP10(false);
  }
  // Returns false if the URL couldn't be set up.
  bool open(const String &path) {
    String url = String(PORTAL_URL) + path;
    bool ok = https() ? http.begin(secure, url) : http.begin(plain, url);
    if (!ok) return false;
    http.addHeader("Authorization", "Bearer " + active[KEY_TOKEN]);
    if (active[KEY_ACCESS_ID].length()) {  // Cloudflare Access service token: lets the clock past the login page
      http.addHeader("CF-Access-Client-Id", active[KEY_ACCESS_ID]);
      http.addHeader("CF-Access-Client-Secret", active[KEY_ACCESS_SECRET]);
    }
    return true;
  }
  // After an answer that was read to the end: keep the socket for the next request (if the server allows).
  void finish() { http.end(); }
  // After anything else (error answers, half-read bodies): throw the socket away too, so stray bytes
  // can't be mistaken for the start of the next answer.
  void close() {
    http.end();
    secure.stop();
    plain.stop();
  }
  // GET with an optional If-None-Match. On a connection error (typically a kept-alive socket the
  // server closed meanwhile) it drops the socket and tries once more on a fresh one.
  int get(const String &path, const String &ifNoneMatch = "", bool wantEtag = false) {
    static const char *keys[] = {"ETag"};
    int code = -1;
    for (int attempt = 0; attempt < 2; attempt++) {
      if (!open(path)) return -1;
      if (ifNoneMatch.length()) http.addHeader("If-None-Match", ifNoneMatch);
      if (wantEtag) http.collectHeaders(keys, 1);
      code = http.GET();
      if (code >= 0) break;
      close();
    }
    return code;
  }
};

// Collects a response body into a fixed buffer; flags overflow instead of writing past the end.
class BufSink : public Stream {
 public:
  BufSink(uint8_t *b, size_t cap) : buf(b), cap(cap) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (len + n > cap) {
      overflow = true;
      return n;  // swallow, so the transfer finishes cleanly and we can report it
    }
    memcpy(buf + len, p, n);
    len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  uint8_t *buf;
  size_t cap, len = 0;
  bool overflow = false;
};

// HTTPClient's body loop uses delay(0), which need not run the idle task. Short read
// waits plus a real task delay keep TLS/image transfers cooperative on weak Wi-Fi.
// Keep HTTPClient's chunk decoding and enforce an overall body deadline as well.
int writeResponse(Conn &c, Stream &destination, uint32_t deadline = IO_TIMEOUT_MS) {
  class YieldingSink : public Stream {
   public:
    explicit YieldingSink(Stream &out, uint32_t deadline) : out(out), started(millis()), deadline(deadline) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *p, size_t n) override {
      vTaskDelay(1);
      if (millis() - started >= deadline) { setWriteError(); return 0; }
      size_t wrote = out.write(p, n);
      if (out.getWriteError()) setWriteError();
      return wrote;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
   private:
    Stream &out;
    uint32_t started, deadline;
  } sink(destination, deadline);
  Stream *socket = c.http.getStreamPtr();
  if (!socket) return -1;
  socket->setTimeout(100);
  int result = c.http.writeToStream(&sink);
  socket->setTimeout(IO_TIMEOUT_MS);
  return result;
}

uint8_t *bigAlloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return (uint8_t *)(p ? p : malloc(n));
}

void bump() { rev++; }  // call with the lock held

bool validId(const char *id) {
  size_t n = strlen(id);
  if (n == 0 || n > 12) return false;
  for (size_t i = 0; i < n; i++)
    if (!isxdigit((unsigned char)id[i])) return false;
  return true;
}

const char *statusName(int s) {
  return s == bhoomi_clock_v1_Listener_Status_PLAYING ? "playing" : s == bhoomi_clock_v1_Listener_Status_PAUSED ? "paused" : "off";
}

// ---------- saved copy, so a power cut doesn't lose anything ----------
// The raw DeviceState bytes go to the internal flash (FFat partition, no card needed); the photo goes
// to the SD card in /portal (kept apart from the user's /photos). Files are written to a temp name
// and renamed, so a cut mid-write leaves the previous copy intact.
constexpr const char *STATE_FILE = "/state.pb";
constexpr const char *SD_DIR = "/portal";
constexpr const char *PHOTO_FILE = "/portal/photo.jpg";
constexpr const char *PHOTO_ID_FILE = "/portal/photo.id";  // written last: marks photo.jpg as complete
bool flashUp = false;

bool flashBegin() {
  if (!flashUp) flashUp = FFat.begin(true);  // formats only our own (unused) partition if it was never set up
  if (!flashUp) Serial.println("[portal] flash storage unavailable; nothing will be saved across power cuts");
  return flashUp;
}

template <typename FS_>
bool writeAtomic(FS_ &fs, const char *path, const uint8_t *data, size_t len) {
  String tmp = String(path) + ".tmp";
  File f = fs.open(tmp, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(data, len) == len;
  f.close();
  if (ok) {
    if (fs.exists(path)) fs.remove(path);
    ok = fs.rename(tmp, path);
  }
  if (!ok) fs.remove(tmp);
  return ok;
}

template <typename FS_>
uint8_t *readAll(FS_ &fs, const char *path, size_t maxLen, size_t &len) {
  File f = fs.open(path, FILE_READ);
  if (!f || f.isDirectory()) return nullptr;
  len = f.size();
  uint8_t *buf = len && len <= maxLen ? bigAlloc(len) : nullptr;
  if (buf && f.read(buf, len) != len) {
    free(buf);
    buf = nullptr;
  }
  f.close();
  return buf;
}

void saveState(const uint8_t *body, size_t len) {
  if (flashBegin() && !writeAtomic(FFat, STATE_FILE, body, len)) Serial.println("[portal] could not save state to flash");
}

void forgetPhoto() {
  if (!storage::mounted()) return;
  SD_MMC.remove(PHOTO_ID_FILE);
  SD_MMC.remove(PHOTO_FILE);
}

void savePhoto(const uint8_t *jpg, size_t len, const char *id) {
  if (!storage::mounted()) return;  // no card: the photo is just re-downloaded after a reboot
  if (!SD_MMC.exists(SD_DIR)) SD_MMC.mkdir(SD_DIR);
  SD_MMC.remove(PHOTO_ID_FILE);
  if (writeAtomic(SD_MMC, PHOTO_FILE, jpg, len) &&
      writeAtomic(SD_MMC, PHOTO_ID_FILE, (const uint8_t *)id, strlen(id)))
    Serial.printf("[portal] photo %s saved to the SD card (%u bytes)\n", id, (unsigned)len);
  else
    Serial.println("[portal] could not save the photo to the SD card");
}

// At boot: bring back the last state and photo. The first poll afterwards is unconditional (no
// If-None-Match), so a portal that was reset can't be masked by a stale copy.
void loadSaved() {
  size_t len = 0;
  uint8_t *body = flashBegin() ? readAll(FFat, STATE_FILE, bhoomi_clock_v1_DeviceState_size + 64, len) : nullptr;
  if (!body) {
    Serial.println("[portal] no saved state");
    return;
  }
  pb_istream_t in = pb_istream_from_buffer(body, len);
  State *next = (State *)bigAlloc(sizeof(State));
  if (next) memset(next, 0, sizeof(State));
  if (!next || !pb_decode(&in, &bhoomi_clock_v1_DeviceState_msg, next)) {
    Serial.println("[portal] saved state is unreadable; ignoring it");
    free(body);
    free(next);
    return;
  }
  memcpy(&cur, next, sizeof(State));
  cur.has_nowplaying = false;  // whatever was playing at the last save is old news
  memset(&cur.nowplaying, 0, sizeof cur.nowplaying);
  have = true;
  Serial.printf("[portal] restored saved state v%u (%u bytes): note=%s, photo=%s, %u alarm(s)\n", (unsigned)cur.version,
                (unsigned)len, cur.has_note ? "yes" : "none", cur.has_photo ? cur.photo.id : "none",
                (unsigned)cur.alarms_count);
  free(body);
  free(next);

  if (cur.has_photo && storage::mounted()) {
    // The saved photo may be older than the saved state (a cut between the two): still show it
    // meanwhile; syncPhoto() sees the id differs and fetches the new one once online.
    size_t idLen = 0, jpgLen = 0;
    uint8_t *idBuf = readAll(SD_MMC, PHOTO_ID_FILE, 12, idLen);
    char savedId[13] = "";
    if (idBuf) {
      memcpy(savedId, idBuf, idLen);
      savedId[idLen] = 0;
    }
    free(idBuf);
    uint8_t *jpg = validId(savedId) ? readAll(SD_MMC, PHOTO_FILE, MAX_PHOTO_BYTES, jpgLen) : nullptr;
    bool same = strcmp(savedId, cur.photo.id) == 0;
    if (jpg && jpgLen > 4 && jpg[0] == 0xFF && jpg[1] == 0xD8 && (!same || jpgLen == cur.photo.size)) {
      photoBuf = jpg;
      photoLen = jpgLen;
      strlcpy(photoIdStr, savedId, sizeof photoIdStr);
      Serial.printf("[portal] restored photo %s from the SD card (%u bytes%s)\n", savedId, (unsigned)jpgLen,
                    same ? "" : ", older than the state");
    } else {
      free(jpg);
      Serial.println("[portal] no usable saved photo; it will be downloaded");
    }
  }
  bump();
}

// ---------- state ----------

void logState(const State &st, const String &tag, size_t bytes) {
  Serial.printf("[portal] state: 200 v%u etag=%s, %u bytes; note=%s%s%s; photo=%s; %u alarm(s)\n", (unsigned)st.version,
                tag.c_str(), (unsigned)bytes, st.has_note ? "\"" : "none", st.has_note ? st.note.text : "",
                st.has_note ? "\"" : "", st.has_photo ? st.photo.id : "none", (unsigned)st.alarms_count);
  for (pb_size_t i = 0; i < st.alarms_count; i++) {
    const Alarm &a = st.alarms[i];
    Serial.printf("[portal]   alarm %s %02u:%02u days=0x%02X", a.id, (unsigned)a.hour, (unsigned)a.minute, (unsigned)a.days);
    if (a.has_date) Serial.printf(" on %04u-%02u-%02u", (unsigned)a.date.year, (unsigned)a.date.month, (unsigned)a.date.day);
    Serial.printf(" \"%s\" sound=%s\n", a.label, a.sound[0] ? a.sound : "(beep)");
  }
}

enum class Poll { Changed, Unchanged, Failed };

Poll fetchState(Conn &c) {
  String q = "/api/device/state?fw=" + String(FW_VERSION) + "&rssi=" + String(WiFi.RSSI());
  {
    app::Lock lock;
    if (const tz::Zone *z = settings::timezone()) q += "&tz=" + urlEncode(z->id);
  }
  int code = c.get(q, etag, true);
  {
    app::Lock lock;
    lastCode = code;
    lastAt = millis() | 1;
  }
  if (code == HTTP_CODE_NOT_MODIFIED) {
    c.finish();
    {
      app::Lock lock;
      okAt = millis() | 1;
    }
    if (!quiet) Serial.printf("[portal] state: 304 not modified (%s)\n", etag.c_str());
    return Poll::Unchanged;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("[portal] state: HTTP %d%s\n", code, code < 0 ? (" " + c.http.errorToString(code)).c_str() : "");
    if (code == 401) Serial.println("[portal] the device token was rejected; update it on the settings page");
    if (code == 302 || code == 403)  // Access sends anything it doesn't let through to its login page
      Serial.println("[portal] Cloudflare Access refused the clock (service token expired?); update it on the settings page");
    if (code == 301) Serial.println("[portal] the portal is HTTPS only; PORTAL_URL must start with https://");
    if (code < 0 && https()) {  // say why a TLS connection failed (certificate, memory, ...)
      char why[100] = "";
      c.secure.lastError(why, sizeof why);
      Serial.printf("[portal] TLS: %s (free heap %u, largest block %u)\n", why[0] ? why : "no error recorded",
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
    c.close();
    return Poll::Failed;
  }

  const size_t cap = bhoomi_clock_v1_DeviceState_size + 64;
  uint8_t *body = bigAlloc(cap);
  State *next = (State *)bigAlloc(sizeof(State));
  if (!body || !next) {
    Serial.println("[portal] state: out of memory");
    free(body);
    free(next);
    c.close();
    return Poll::Failed;
  }
  BufSink sink(body, cap);
  int got = writeResponse(c, sink);
  String newTag = c.http.header("ETag");
  if (got >= 0 && !sink.overflow) c.finish();
  else c.close();

  Poll result = Poll::Failed;
  if (got < 0 || sink.overflow) {
    Serial.printf("[portal] state: bad body (%d bytes, overflow=%d)\n", got, sink.overflow);
  } else {
    memset(next, 0, sizeof(State));
    pb_istream_t in = pb_istream_from_buffer(body, sink.len);
    if (!pb_decode(&in, &bhoomi_clock_v1_DeviceState_msg, next)) {
      Serial.printf("[portal] state: decode failed: %s\n", PB_GET_ERROR(&in));
    } else {
      etag = newTag;
      // A 200 is either the portal's own things changing (version), or a new song. Only the former
      // is worth a flash write and a redraw of the note/photo; the latter just moves the strip.
      bool portalChanged = !have || next->version != cur.version;
      bool playingChanged = !have || memcmp(&next->nowplaying, &cur.nowplaying, sizeof next->nowplaying) != 0;
      if (portalChanged) saveState(body, sink.len);
      {
        app::Lock lock;
        memcpy(&cur, next, sizeof(State));
        have = true;
        okAt = millis() | 1;
        if (portalChanged) bump();
        if (playingChanged) npRev++;
      }
      const auto &np = next->nowplaying;
      if (playingChanged)
        Serial.printf("[portal] now playing: L=%s \"%s\" - %s | R=%s \"%s\" - %s\n", statusName(np.left.status), np.left.title,
                      np.left.artist, statusName(np.right.status), np.right.title, np.right.artist);
      if (portalChanged) logState(*next, newTag, sink.len);
      result = Poll::Changed;
    }
  }
  free(body);
  free(next);
  return result;
}

// ---------- photo ----------

enum class Photo { Ok, NotYet, Failed };

// Ensures the photo in `cur` is the one in memory. Keeps the old photo until the new one has
// arrived complete.
Photo syncPhoto(Conn &c) {
  char id[13] = "";
  uint32_t size = 0;
  {
    app::Lock lock;
    if (!cur.has_photo) {  // photo removed in the portal
      if (photoBuf) {
        free(photoBuf);
        photoBuf = nullptr;
        photoLen = 0;
        photoIdStr[0] = 0;
        bump();
        Serial.println("[portal] photo removed");
        forgetPhoto();
      }
      return Photo::Ok;
    }
    if (strcmp(photoIdStr, cur.photo.id) == 0) return Photo::Ok;  // already have it
    strlcpy(id, cur.photo.id, sizeof id);
    size = cur.photo.size;
  }
  if (!validId(id) || size < 4 || size > MAX_PHOTO_BYTES) {
    Serial.printf("[portal] photo: refusing id=\"%s\" size=%u\n", id, (unsigned)size);
    return Photo::Failed;
  }

  uint8_t *buf = bigAlloc(size);
  if (!buf) {
    Serial.printf("[portal] photo: out of memory for %u bytes\n", (unsigned)size);
    return Photo::Failed;
  }
  uint32_t t0 = millis();
  int code = c.get(String("/api/device/image/") + id);
  Photo result = Photo::Failed;
  bool clean = false;  // the answer was read to the end, so the socket can be kept
  if (code == HTTP_CODE_NOT_FOUND) {
    Serial.printf("[portal] photo %s: 404 (not propagated yet), will retry\n", id);
    result = Photo::NotYet;
  } else if (code != HTTP_CODE_OK) {
    Serial.printf("[portal] photo %s: HTTP %d\n", id, code);
  } else {
    BufSink sink(buf, size);
    int got = writeResponse(c, sink);
    if (got < 0 || sink.overflow || sink.len != size) {
      Serial.printf("[portal] photo %s: incomplete (%d of %u bytes, overflow=%d)\n", id, got, (unsigned)size,
                    sink.overflow);
    } else if (buf[0] != 0xFF || buf[1] != 0xD8) {
      Serial.printf("[portal] photo %s: not a JPEG\n", id);
    } else {
      {
        app::Lock lock;
        free(photoBuf);
        photoBuf = buf;
        photoLen = size;
        strlcpy(photoIdStr, id, sizeof photoIdStr);
        buf = nullptr;  // owned by the state now
        bump();
      }
      Serial.printf("[portal] photo %s: downloaded %u bytes in %u ms\n", id, (unsigned)size,
                    (unsigned)(millis() - t0));
      savePhoto(photoBuf, size, id);  // outside the lock: SD writes can take a while
      result = Photo::Ok;
      clean = true;
    }
  }
  if (clean) c.finish();
  else c.close();
  free(buf);
  return result;
}

// ---------- sounds ----------

uint32_t soundsSentGen = 0;
bool soundsEverSent = false;
uint32_t soundsSentHash = 0;

uint32_t fnv(uint32_t h, const char *s) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return (h ^ 0xFF) * 16777619u;
}

bool listSounds(std::vector<String> &names) {
  File dir = SD_MMC.open("/sounds");
  if (!dir || !dir.isDirectory()) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    // same filter as the settings page: no folders, macOS litter or unfinished uploads
    if (f.isDirectory() || n.startsWith(".") || n.endsWith(".part")) continue;
    if (n.length() >= sizeof(((bhoomi_clock_v1_SoundList *)0)->names[0])) continue;  // won't fit the schema
    names.push_back(n);
  }
  std::sort(names.begin(), names.end());
  return true;
}

uint32_t soundsTriedAt = 0;
bool reachable = false;  // the last state poll got an answer

bool soundsDue() {
  if (!reachable || !storage::mounted()) return false;
  if (soundsEverSent && storage::generation() == soundsSentGen) return false;
  return !soundsTriedAt || millis() - soundsTriedAt > 10000;  // don't hammer after a failure
}

void syncSounds(Conn &c) {
  if (!soundsDue()) return;
  soundsTriedAt = millis() | 1;
  uint32_t gen = storage::generation();

  std::vector<String> names;
  if (!listSounds(names)) return;
  uint32_t h = 2166136261u;
  for (auto &n : names) h = fnv(h, n.c_str());
  if (soundsEverSent && h == soundsSentHash) {  // folder touched, list unchanged
    soundsSentGen = gen;
    return;
  }

  auto *list = (bhoomi_clock_v1_SoundList *)bigAlloc(sizeof(bhoomi_clock_v1_SoundList));
  uint8_t *body = bigAlloc(bhoomi_clock_v1_SoundList_size);
  if (!list || !body) {
    free(list);
    free(body);
    return;
  }
  memset(list, 0, sizeof *list);
  const size_t maxN = sizeof(list->names) / sizeof(list->names[0]);
  for (auto &n : names) {
    if (list->names_count >= maxN) break;
    strlcpy(list->names[list->names_count++], n.c_str(), sizeof(list->names[0]));
  }
  pb_ostream_t out = pb_ostream_from_buffer(body, bhoomi_clock_v1_SoundList_size);
  if (!pb_encode(&out, &bhoomi_clock_v1_SoundList_msg, list)) {
    Serial.printf("[portal] sounds: encode failed: %s\n", PB_GET_ERROR(&out));
  } else if (c.open("/api/device/sounds")) {
    c.http.addHeader("Content-Type", "application/x-protobuf");
    int code = c.http.PUT(body, out.bytes_written);
    if (code == HTTP_CODE_NO_CONTENT) c.finish();  // no body to read
    else c.close();
    if (code == HTTP_CODE_NO_CONTENT || code == HTTP_CODE_OK) {
      Serial.printf("[portal] sounds: sent %u name(s), %u bytes -> %d\n", (unsigned)list->names_count,
                    (unsigned)out.bytes_written, code);
      soundsEverSent = true;
      soundsSentGen = gen;
      soundsSentHash = h;
    } else {
      Serial.printf("[portal] sounds: PUT failed, HTTP %d\n", code);
    }
  }
  free(list);
  free(body);
}

// ---------- covers for the now-playing strip ----------
// The song titles arrive inside the state; the cover is fetched like the photo: the state carries an
// art_id, a different id means a new JPEG to download. 64x64, about 3 KB.

constexpr uint32_t COVER_RETRY_MS = 15 * 1000;
constexpr size_t MAX_COVER_BYTES = 24 * 1024;
constexpr size_t ART_ID_LEN = 40;

uint32_t coverTriedAt[2] = {0, 0};  // task-private

bool validArtId(const char *id) {
  if (strlen(id) != ART_ID_LEN) return false;
  for (size_t i = 0; i < ART_ID_LEN; i++)
    if (!isxdigit((unsigned char)id[i])) return false;
  return true;
}

// Makes each side's downloaded cover match the art_id in the latest state.
void syncCovers(Conn &c) {
  for (int side = 0; side < 2; side++) {
    char want[ART_ID_LEN + 1] = "";
    {
      app::Lock lock;
      const Listener &l = side ? cur.nowplaying.right : cur.nowplaying.left;
      if (l.status != bhoomi_clock_v1_Listener_Status_OFF && validArtId(l.art_id)) strlcpy(want, l.art_id, sizeof want);
      if (!want[0]) {  // nothing to show on this side any more (or a song without a cover)
        if (coverBuf[side]) {
          free(coverBuf[side]);
          coverBuf[side] = nullptr;
          coverLen[side] = 0;
          coverIdStr[side][0] = 0;
          npRev++;
        }
        continue;
      }
      if (strcmp(coverIdStr[side], want) == 0) continue;  // already have it
    }
    if (coverTriedAt[side] && millis() - coverTriedAt[side] < COVER_RETRY_MS) continue;
    coverTriedAt[side] = millis() | 1;

    uint8_t *buf = bigAlloc(MAX_COVER_BYTES);
    if (!buf) continue;
    int code = c.get(String("/api/device/art/") + want);
    bool ok = false;
    if (code == HTTP_CODE_OK) {
      BufSink sink(buf, MAX_COVER_BYTES);
      int got = writeResponse(c, sink);
      if (got >= 0 && !sink.overflow && sink.len > 4 && buf[0] == 0xFF && buf[1] == 0xD8) {
        c.finish();
        app::Lock lock;
        free(coverBuf[side]);
        coverBuf[side] = buf;
        coverLen[side] = sink.len;
        strlcpy(coverIdStr[side], want, sizeof coverIdStr[side]);
        npRev++;
        buf = nullptr;  // owned by the state now
        ok = true;
        coverTriedAt[side] = 0;
        Serial.printf("[portal] cover %.8s: %u bytes (side %d)\n", want, (unsigned)sink.len, side);
      }
    }
    if (!ok) {
      Serial.printf("[portal] cover %.8s: failed (HTTP %d), will retry\n", want, code);
      c.close();
    }
    free(buf);
  }
}

// ---------- clock faces ----------
// The state lists the faces (id + rev). Each face is fetched when its rev is new, then the images it uses (a face
// is "ready" once all of them are here). Everything lives in PSRAM: a face is under 5 KB, a sprite is 3 bytes a
// pixel, and the portal limits a face to 1.5 MB of images. Images no face uses any more are dropped.

constexpr int MAX_FACES = 12;
constexpr int MAX_ASSETS = 128;
constexpr size_t MAX_ASSET_FILE = 300 * 1024;
constexpr size_t MAX_ASSET_TOTAL = 5 * 1024 * 1024;
constexpr uint32_t FACES_MORE_MS = 1000;        // poll again this soon while there is still something to fetch
constexpr uint32_t FACES_RETRY_MS = 15 * 1000;  // after a 404 (KV still propagating) or a bad download
constexpr uint32_t FACES_PASS_MS = 3000;        // work on faces for at most this long per poll, so the heartbeat goes on

struct FaceEntry {
  char id[13];
  char rev[13];  // the rev the state wants; `face->rev` is the one we hold (older until the new one arrives)
  char cachedRev[13]; // last complete on-card face, which may be older than the desired revision
  Face *face;    // nullptr until fetched
};
struct Asset {
  char hash[17];
  uint8_t *buf;
  size_t len;
};
FaceEntry faceList[MAX_FACES];
int faceN = 0;
Asset assets[MAX_ASSETS];
int assetN = 0;
size_t assetBytes = 0;
uint32_t facesRev = 0;
uint32_t facesRetryAt = 0;  // task-private

bool validHash(const char *h) {
  if (strlen(h) != 16) return false;
  for (int i = 0; i < 16; i++)
    if (!isxdigit((unsigned char)h[i])) return false;
  return true;
}

const Asset *findAsset(const char *hash) {
  for (int i = 0; i < assetN; i++)
    if (strcmp(assets[i].hash, hash) == 0) return &assets[i];
  return nullptr;
}

// Calls fn(ref, isBackground) for every image a face uses.
template <typename F>
void eachImage(const Face &f, F fn) {
  if (f.has_background) fn(f.background, true);
  if (f.has_icon && f.icon.has_sprite) fn(f.icon.sprite, false);
  for (pb_size_t i = 0; i < f.layers_count; i++)
    if (f.layers[i].has_sprite) fn(f.layers[i].sprite, false);
  for (pb_size_t i = 0; i < f.emitters_count; i++)
    for (pb_size_t k = 0; k < f.emitters[i].sprites_count; k++) fn(f.emitters[i].sprites[k], false);
  for (const auto *v : {f.has_cloudy ? &f.cloudy : nullptr, f.has_rain ? &f.rain : nullptr}) {
    if (!v) continue;
    if (v->has_background) fn(v->background, true);
    for (pb_size_t i = 0; i < v->layers_count; i++) if (v->layers[i].has_sprite) fn(v->layers[i].sprite, false);
    for (pb_size_t i = 0; i < v->emitters_count; i++)
      for (pb_size_t k = 0; k < v->emitters[i].sprites_count; k++) fn(v->emitters[i].sprites[k], false);
  }
}

bool faceReady(const Face &f) {
  bool ok = f.has_background && f.has_time;
  eachImage(f, [&](const bhoomi_clock_v1_SpriteRef &r, bool) { ok = ok && findAsset(r.hash); });
  return ok;
}

const FaceEntry *chooseEntry(const struct tm &t) {
  auto byId = [](const char *id) -> const FaceEntry * {
    for (int i = 0; id[0] && i < faceN; i++)
      if (strcmp(faceList[i].id, id) == 0) return &faceList[i];
    return nullptr;
  };
  if (const FaceEntry *e = byId(cur.pinned_face)) return e;
  for (int i = 0; i < faceN; i++)
    if (faceList[i].face && faceList[i].face->has_when && facerules::matches(faceList[i].face->when, t)) return &faceList[i];
  return byId(cur.default_face);
}

// Frees the images no held face uses. Lock held.
void dropUnusedAssets() {
  for (int i = assetN - 1; i >= 0; i--) {
    bool used = false;
    for (int k = 0; k < faceN && !used; k++)
      if (faceList[k].face) eachImage(*faceList[k].face, [&](const bhoomi_clock_v1_SpriteRef &r, bool) { used = used || strcmp(r.hash, assets[i].hash) == 0; });
    if (used) continue;
    assetBytes -= assets[i].len;
    free(assets[i].buf);
    assets[i] = assets[--assetN];
    facesRev++;
  }
}

// Makes faceList match the state's list. Lock held.
void reconcileFaces() {
  FaceEntry next[MAX_FACES];
  int n = 0;
  for (pb_size_t i = 0; i < cur.faces_count && n < MAX_FACES; i++) {
    const auto &ref = cur.faces[i];
    if (!validId(ref.id) || !validId(ref.rev)) continue;
    FaceEntry e = {};
    strlcpy(e.id, ref.id, sizeof e.id);
    strlcpy(e.rev, ref.rev, sizeof e.rev);
    for (int k = 0; k < faceN; k++)
      if (strcmp(faceList[k].id, e.id) == 0) {
        e.face = faceList[k].face;
        strlcpy(e.cachedRev, faceList[k].cachedRev, sizeof e.cachedRev);
        faceList[k].face = nullptr;  // moved
      }
    next[n++] = e;
  }
  bool changed = n != faceN;
  for (int k = 0; k < faceN; k++)
    if (faceList[k].face) {  // faces that are gone from the list
      free(faceList[k].face);
      changed = true;
    }
  for (int i = 0; !changed && i < n; i++) changed = strcmp(next[i].id, faceList[i].id) != 0 || strcmp(next[i].rev, faceList[i].rev) != 0;
  memcpy(faceList, next, sizeof(FaceEntry) * n);
  faceN = n;
  if (changed) facesRev++;
  dropUnusedAssets();
}

// Face cache is separate from the user's photos, sounds and videos. The state still selects
// the authoritative revisions. Face files carry a digest; assets are SHA-256 content addressed.
constexpr const char *FACE_CACHE_DIR = "/portal/face-cache";
constexpr const char *ASSET_CACHE_DIR = "/portal/face-assets";
constexpr size_t CACHE_LIMIT_BYTES = 32 * 1024 * 1024;
constexpr size_t FACE_CACHE_HEADER = 36;  // "BFC1" + SHA-256 of protobuf bytes
size_t diskCacheBytes = 0;
bool cacheInitialized = false;

String faceCachePath(const char *id) { return String(FACE_CACHE_DIR) + "/" + id + ".pb"; }
String assetCachePath(const char *hash) { return String(ASSET_CACHE_DIR) + "/" + hash + ".bin"; }
void digest256(const uint8_t *data, size_t len, uint8_t out[32]) {
  mbedtls_sha256_context ctx; mbedtls_sha256_init(&ctx); mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len); mbedtls_sha256_finish(&ctx, out); mbedtls_sha256_free(&ctx);
}
size_t cacheDirectoryBytes(const char *dirName) {
  size_t total = 0; File dir = SD_MMC.open(dirName);
  if (!dir || !dir.isDirectory()) return 0;
  for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) total += file.size();
    file.close();
  }
  dir.close(); return total;
}
bool prepareFaceCache() {
  if (!storage::mounted()) return false;
  if (!SD_MMC.exists(SD_DIR) && !SD_MMC.mkdir(SD_DIR)) return false;
  if (!SD_MMC.exists(FACE_CACHE_DIR) && !SD_MMC.mkdir(FACE_CACHE_DIR)) return false;
  if (!SD_MMC.exists(ASSET_CACHE_DIR) && !SD_MMC.mkdir(ASSET_CACHE_DIR)) return false;
  if (!cacheInitialized) {
    diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR);
    cacheInitialized = true;
  }
  return true;
}
// Stage a new checksummed definition separately; the last good face remains until every
// referenced image is safely cached too. A power loss can therefore never promote a partial face.
bool stageFaceCache(const char *id, const uint8_t *body, size_t len) {
  if (!validId(id) || !prepareFaceCache() || !len || len > bhoomi_clock_v1_Face_size + 64) return false;
  String path = faceCachePath(id) + ".next";
  File old = SD_MMC.open(path, FILE_READ); size_t oldLen = old ? old.size() : 0; old.close();
  if (diskCacheBytes - std::min(diskCacheBytes, oldLen) + len + FACE_CACHE_HEADER > CACHE_LIMIT_BYTES) return false;
  uint8_t *record = (uint8_t *)heap_caps_malloc(len + FACE_CACHE_HEADER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!record) return false;
  memcpy(record, "BFC1", 4); digest256(body, len, record+4); memcpy(record+FACE_CACHE_HEADER,body,len);
  SD_MMC.remove(path + ".tmp");
  bool ok = writeAtomic(SD_MMC,path.c_str(),record,len+FACE_CACHE_HEADER); free(record);
  diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR);
  return ok;
}
// Return only a complete, checksummed and decodable record for this id.
Face *readFaceCacheRecord(const String &path, const char *id) {
  size_t len = 0; uint8_t *data = readAll(SD_MMC, path.c_str(), bhoomi_clock_v1_Face_size + 64 + FACE_CACHE_HEADER, len);
  if (!data) return nullptr;
  bool ok = len > FACE_CACHE_HEADER && !memcmp(data, "BFC1", 4);
  uint8_t hash[32]; if (ok) { digest256(data+FACE_CACHE_HEADER, len-FACE_CACHE_HEADER, hash); ok = facecache::digestMatches(hash, data+4); }
  Face *f = ok ? (Face *)heap_caps_malloc(sizeof(Face), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  if (f) {
    memset(f, 0, sizeof(Face)); pb_istream_t in = pb_istream_from_buffer(data+FACE_CACHE_HEADER, len-FACE_CACHE_HEADER);
    ok = pb_decode(&in, &bhoomi_clock_v1_Face_msg, f) && !strcmp(f->id, id) && validId(f->rev) && f->has_background && f->has_time;
  } else ok = false;
  free(data); if (!ok) { free(f); return nullptr; } return f;
}
bool loadCachedFace(const char *id, const char *wantedRev, bool allowStale) {
  if (!validId(id) || !validId(wantedRev) || !prepareFaceCache()) return false;
  String path = faceCachePath(id), backup = path + ".bak";
  Face *f = readFaceCacheRecord(path, id);
  const bool primaryValid = f != nullptr;
  if (f && !allowStale && strcmp(f->rev, wantedRev)) { free(f); f=nullptr; }
  if (!f) {
    f=readFaceCacheRecord(backup,id);
    if (f && !allowStale && strcmp(f->rev,wantedRev)) { free(f); f=nullptr; }
    if (f && !primaryValid) {
      // Only replace a missing/corrupt primary after the backup has passed every check.
      SD_MMC.remove(path);
      if (SD_MMC.rename(backup,path)) Serial.printf("[face-cache] recovered backup %s\n", id);
    }
  }
  if (!f) return false;
  bool installed = false;
  {
    app::Lock lock;
    for (int i=0; i<faceN; ++i) if (!strcmp(faceList[i].id,id) && !strcmp(faceList[i].rev,wantedRev) &&
                                        (!faceList[i].face || !allowStale)) {
      free(faceList[i].face); faceList[i].face=f;
      strlcpy(faceList[i].cachedRev, f->rev, sizeof faceList[i].cachedRev);
      f=nullptr; ++facesRev; installed=true; break;
    }
  }
  if (installed) Serial.printf("[face-cache] restored face %s%s\n", id, allowStale ? " (last saved)" : "");
  free(f); return installed;
}
void writeAssetCache(const bhoomi_clock_v1_SpriteRef &r, const uint8_t *data, size_t len) {
  if (!validHash(r.hash) || !prepareFaceCache()) return;
  String path = assetCachePath(r.hash);
  if (SD_MMC.exists(path) || diskCacheBytes + len > CACHE_LIMIT_BYTES) return;
  SD_MMC.remove(path + ".tmp"); // discard a partial temp file from a previous power cut
  if (writeAtomic(SD_MMC, path.c_str(), data, len)) diskCacheBytes += len;
}
bool loadCachedAsset(const bhoomi_clock_v1_SpriteRef &r, bool background) {
  if (!validHash(r.hash) || r.size < 12 || r.size > MAX_ASSET_FILE || !prepareFaceCache()) return false;
  { app::Lock lock; if (findAsset(r.hash)) return true; if (assetN >= MAX_ASSETS || assetBytes + r.size > MAX_ASSET_TOTAL) return false; }
  String path = assetCachePath(r.hash); size_t len = 0;
  File cached = SD_MMC.open(path, FILE_READ);
  if (!cached || cached.isDirectory()) return false;
  size_t cachedSize = cached.size(); cached.close();
  if (cachedSize != r.size || cachedSize > MAX_ASSET_FILE) {
    SD_MMC.remove(path); diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR); return false;
  }
  uint8_t *data = readAll(SD_MMC, path.c_str(), MAX_ASSET_FILE, len); if (!data) return false;
  uint8_t hash[32]; digest256(data, len, hash);
  bool good = facecache::validAssetShape(data, len, r.size, r.w, r.h, background) && facecache::digestMatchesHex(hash, r.hash, 16);
  if (!good) { SD_MMC.remove(path); diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR); free(data); return false; }
  bool installed = false;
  {
    app::Lock lock;
    if (!findAsset(r.hash) && assetN < MAX_ASSETS && assetBytes + len <= MAX_ASSET_TOTAL) {
      Asset &a=assets[assetN++]; strlcpy(a.hash,r.hash,sizeof a.hash); a.buf=data; a.len=len;
      assetBytes += len; data=nullptr; ++facesRev; installed=true;
    }
  }
  if (installed) Serial.printf("[face-cache] restored image %s (%u bytes)\n", r.hash, (unsigned)len);
  free(data); return installed;
}

// Promote staged definitions only after the exact current revision and every image are
// present. The older definition and images remain usable if downloading is interrupted.
void commitReadyFaceCaches() {
  if (!prepareFaceCache()) return;
  Face *copy=(Face *)heap_caps_malloc(sizeof(Face), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!copy) return;
  for (int i=0; i<MAX_FACES; ++i) {
    char id[13] = {}, rev[13] = {}, cachedRev[13] = {}; bool ready=false;
    {
      app::Lock lock;
      if (i>=faceN) break;
      if (faceList[i].face && !strcmp(faceList[i].face->rev,faceList[i].rev) && faceReady(*faceList[i].face)) {
        strlcpy(id,faceList[i].id,sizeof id); strlcpy(rev,faceList[i].rev,sizeof rev);
        strlcpy(cachedRev,faceList[i].cachedRev,sizeof cachedRev);
        memcpy(copy,faceList[i].face,sizeof(Face)); ready=true;
      }
    }
    if (!ready || !strcmp(cachedRev,rev)) continue;
    String path=faceCachePath(id), staged=path+".next", backup=path+".bak";
    if (!SD_MMC.exists(staged)) {
      // A previous staging write may have failed (full card, reset, or transient I/O).
      uint8_t *buf=(uint8_t *)heap_caps_malloc(bhoomi_clock_v1_Face_size+64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!buf) continue;
      pb_ostream_t out=pb_ostream_from_buffer(buf,bhoomi_clock_v1_Face_size+64);
      bool encoded=pb_encode(&out,&bhoomi_clock_v1_Face_msg,copy);
      bool stagedOK=encoded && stageFaceCache(id,buf,out.bytes_written); free(buf);
      if (!stagedOK) continue;
    }
    bool allOnDisk=true;
    eachImage(*copy,[&](const bhoomi_clock_v1_SpriteRef &r, bool) {
      if (!allOnDisk || !validHash(r.hash)) { allOnDisk=false; return; }
      File f=SD_MMC.open(assetCachePath(r.hash), FILE_READ);
      if (!f || f.isDirectory() || f.size()!=r.size) allOnDisk=false;
      f.close();
    });
    if (!allOnDisk) continue;
    // Confirm the staged record itself still has the exact revision and checksum.
    size_t n=0; uint8_t *data=readAll(SD_MMC,staged.c_str(),bhoomi_clock_v1_Face_size+64+FACE_CACHE_HEADER,n);
    if (!data) continue;
    uint8_t digest[32]; bool valid=n>FACE_CACHE_HEADER && !memcmp(data,"BFC1",4);
    if (valid) { digest256(data+FACE_CACHE_HEADER,n-FACE_CACHE_HEADER,digest); valid=facecache::digestMatches(digest,data+4); }
    Face *stagedFace=valid ? (Face *)heap_caps_malloc(sizeof(Face), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (stagedFace) {
      memset(stagedFace,0,sizeof(Face)); pb_istream_t in=pb_istream_from_buffer(data+FACE_CACHE_HEADER,n-FACE_CACHE_HEADER);
      valid=pb_decode(&in,&bhoomi_clock_v1_Face_msg,stagedFace) && !strcmp(stagedFace->id,id) && !strcmp(stagedFace->rev,rev);
    } else valid=false;
    free(stagedFace); free(data);
    if (!valid) { SD_MMC.remove(staged); continue; }
    if (!facecache::promote(SD_MMC,path,staged,backup)) continue;
    { app::Lock lock; for (int k=0;k<faceN;++k) if (!strcmp(faceList[k].id,id) && !strcmp(faceList[k].rev,rev))
        strlcpy(faceList[k].cachedRev,rev,sizeof faceList[k].cachedRev); }
    Serial.printf("[face-cache] saved complete face %s rev %s\n",id,rev);
    vTaskDelay(1);
  }
  free(copy);
  diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR);
}

// Verify the on-disk last-good face before deleting any of its older artwork.
bool cacheHasFaceRevision(const char *id, const char *rev) {
  Face *f=readFaceCacheRecord(faceCachePath(id),id);
  bool ok=f && !strcmp(f->rev,rev); free(f); return ok;
}

// Prune only when the cache has grown and all current definitions are safely committed.
// This preserves the previous face and artwork throughout a partially downloaded update.
void pruneFaceCache() {
  if (!prepareFaceCache() || diskCacheBytes < CACHE_LIMIT_BYTES / 2) return;
  for (int i=0; i<MAX_FACES; ++i) {
    char id[13]={}, rev[13]={};
    { app::Lock lock;
      if (i>=faceN) { if (i==0) return; break; }
      if (!faceList[i].face || strcmp(faceList[i].face->rev, faceList[i].rev)) return;
      strlcpy(id,faceList[i].id,sizeof id); strlcpy(rev,faceList[i].rev,sizeof rev);
    }
    if (!cacheHasFaceRevision(id,rev)) return;
  }
  File dir = SD_MMC.open(ASSET_CACHE_DIR); if (!dir || !dir.isDirectory()) return;
  for (File file=dir.openNextFile(); file; file=dir.openNextFile()) {
    String name=file.name(); bool isDir=file.isDirectory(); file.close();
    int slash=name.lastIndexOf('/'); if (slash>=0) name=name.substring(slash+1);
    if (isDir) continue;
    if (name.endsWith(".bin.tmp")) {
      String h=name.substring(0,name.length()-8);
      if (validHash(h.c_str())) SD_MMC.remove(String(ASSET_CACHE_DIR)+"/"+name);
      continue;
    }
    if (!name.endsWith(".bin")) continue;
    String h=name.substring(0,name.length()-4); if (!validHash(h.c_str())) continue;
    bool used=false;
    { app::Lock lock; for (int i=0; i<faceN && !used; ++i) if (faceList[i].face)
        eachImage(*faceList[i].face, [&](const bhoomi_clock_v1_SpriteRef &r, bool) { if (h == r.hash) used=true; }); }
    if (!used) SD_MMC.remove(String(ASSET_CACHE_DIR)+"/"+name);
    vTaskDelay(1);
  }
  dir.close();
  File faceDir=SD_MMC.open(FACE_CACHE_DIR);
  if (faceDir && faceDir.isDirectory()) {
    for (File file=faceDir.openNextFile(); file; file=faceDir.openNextFile()) {
      String name=file.name(); bool isDir=file.isDirectory(); file.close();
      int slash=name.lastIndexOf('/'); if (slash>=0) name=name.substring(slash+1);
      int dot=name.indexOf('.'); if (isDir || dot<1) continue;
      String id=name.substring(0,dot), ext=name.substring(dot);
      if (!validId(id.c_str()) || (ext!=".pb" && ext!=".pb.bak" && ext!=".pb.next" && ext!=".pb.next.tmp")) continue;
      bool used=false; { app::Lock lock; for (int i=0;i<faceN;++i) if (id==faceList[i].id) used=true; }
      if (!used) SD_MMC.remove(String(FACE_CACHE_DIR)+"/"+name);
      vTaskDelay(1);
    }
    faceDir.close();
  }
  diskCacheBytes = cacheDirectoryBytes(FACE_CACHE_DIR) + cacheDirectoryBytes(ASSET_CACHE_DIR);
}

// Restore off the main loop so SD reads never delay the clock, touch, or alarms.
void restoreFaceCache() {
  if (!prepareFaceCache()) return;
  { app::Lock lock; reconcileFaces(); }
  for (int i=0; i<MAX_FACES; ++i) {
    char id[13] = {}, rev[13] = {};
    { app::Lock lock; if (i>=faceN) break; strlcpy(id,faceList[i].id,sizeof id); strlcpy(rev,faceList[i].rev,sizeof rev); }
    loadCachedFace(id, rev, true); vTaskDelay(1);
  }
  Face *copy = (Face *)heap_caps_malloc(sizeof(Face), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!copy) return;
  for (int pass=0; pass<2; ++pass) for (int i=0; i<MAX_FACES; ++i) {
    bool found=false;
    {
      app::Lock lock;
      if (i>=faceN) break;
      time_t now=time(nullptr); struct tm t; const FaceEntry *chosen=nullptr;
      if (timeValid()) { localtime_r(&now,&t); chosen=chooseEntry(t); }
      if (faceList[i].face && ((pass==0) == (chosen==&faceList[i]))) { memcpy(copy,faceList[i].face,sizeof(Face)); found=true; }
    }
    if (found) eachImage(*copy, [&](const bhoomi_clock_v1_SpriteRef &ref, bool bg) { loadCachedAsset(ref,bg); vTaskDelay(1); });
  }
  free(copy);
  pruneFaceCache();
}

enum class Got { Ok, NotYet, Failed };

Got fetchFace(Conn &c, const char *id, const char *rev) {
  uint8_t *body = bigAlloc(bhoomi_clock_v1_Face_size + 64);
  Face *f = (Face *)bigAlloc(sizeof(Face));
  if (!body || !f) {
    free(body);
    free(f);
    return Got::Failed;
  }
  int code = c.get(String("/api/device/face/") + id);
  Got result = Got::Failed;
  bool clean = false;
  if (code == HTTP_CODE_NOT_FOUND) {
    result = Got::NotYet;
  } else if (code == HTTP_CODE_OK) {
    BufSink sink(body, bhoomi_clock_v1_Face_size + 64);
    int got = writeResponse(c, sink);
    memset(f, 0, sizeof(Face));
    pb_istream_t in = pb_istream_from_buffer(body, sink.len);
    if (got < 0 || sink.overflow) {
      Serial.printf("[faces] %s: bad body\n", id);
    } else if (!pb_decode(&in, &bhoomi_clock_v1_Face_msg, f)) {
      Serial.printf("[faces] %s: decode failed: %s\n", id, PB_GET_ERROR(&in));
      clean = true;
    } else if (strcmp(f->id, id) != 0 || !f->has_background || !f->has_time) {
      Serial.printf("[faces] %s: not a usable face\n", id);
      clean = true;
    } else if (strcmp(f->rev, rev) != 0) {
      // KV may return an older face while its new state is propagating. Retain last good.
      Serial.printf("[faces] %s: revision not yet available; retrying\n", id);
      result = Got::NotYet; clean = true;
    } else {
      {
        app::Lock lock;
        for (int i = 0; i < faceN; i++)
          if (strcmp(faceList[i].id, id) == 0 && strcmp(faceList[i].rev, rev) == 0) {
            free(faceList[i].face);
            faceList[i].face = f;
            f = nullptr;
            facesRev++;
            result = Got::Ok;
            break;
          }
      }
      if (result == Got::Ok) stageFaceCache(id, body, sink.len);
      clean = true;
      Serial.printf("[faces] %s rev %s: fetched (%u bytes)\n", id, rev, (unsigned)sink.len);
    }
  } else {
    Serial.printf("[faces] %s: HTTP %d\n", id, code);
  }
  if (clean) c.finish();
  else c.close();
  free(body);
  free(f);
  return result;
}

Got fetchAsset(Conn &c, const bhoomi_clock_v1_SpriteRef &r, bool background) {
  if (!validHash(r.hash) || r.size < 12 || r.size > MAX_ASSET_FILE || assetBytes + r.size > MAX_ASSET_TOTAL || assetN >= MAX_ASSETS) {
    Serial.printf("[faces] image %s: refused (size %u, %u cached)\n", r.hash, (unsigned)r.size, (unsigned)assetBytes);
    return Got::Failed;
  }
  uint8_t *buf = bigAlloc(r.size);
  if (!buf) return Got::Failed;
  uint32_t t0 = millis();
  int code = c.get(String("/api/device/asset/") + r.hash);
  Got result = Got::Failed;
  bool clean = false;
  if (code == HTTP_CODE_NOT_FOUND) {
    result = Got::NotYet;
  } else if (code != HTTP_CODE_OK) {
    Serial.printf("[faces] image %s: HTTP %d\n", r.hash, code);
  } else {
    BufSink sink(buf, r.size);
    int got = writeResponse(c, sink);
    bool good = got >= 0 && !sink.overflow && facecache::validAssetShape(buf, sink.len, r.size, r.w, r.h, background);
    uint8_t hash[32];
    if (good) { digest256(buf, sink.len, hash); good = facecache::digestMatchesHex(hash, r.hash, 16); }
    if (!good) {
      Serial.printf("[faces] image %s: incomplete or wrong (%d of %u bytes)\n", r.hash, got, (unsigned)r.size);
    } else {
      writeAssetCache(r, buf, sink.len);
      app::Lock lock;
      Asset &a = assets[assetN++];
      strlcpy(a.hash, r.hash, sizeof a.hash);
      a.buf = buf;
      a.len = r.size;
      assetBytes += r.size;
      buf = nullptr;
      facesRev++;
      result = Got::Ok;
      Serial.printf("[faces] image %s: %u bytes in %u ms\n", r.hash, (unsigned)r.size, (unsigned)(millis() - t0));
    }
    clean = got >= 0 && !sink.overflow;
  }
  if (clean) c.finish();
  else c.close();
  free(buf);
  return result;
}

// One pass of work: match the list to the state, fetch what is missing (the face on screen first), within a time
// budget. Returns true if there is more to do right away.
bool syncFaces(Conn &c) {
  struct tm t;
  bool haveTime = false;
  {
    app::Lock lock;
    reconcileFaces();
    time_t now = time(nullptr);
    if (timeValid()) {
      localtime_r(&now, &t);
      haveTime = true;
    }
  }
  if (facesRetryAt && (int32_t)(millis() - facesRetryAt) < 0) return false;
  uint32_t started = millis();
  bool failed = false;

  // 1. the faces themselves
  for (int i = 0; i < MAX_FACES && !failed; i++) {
    char id[13], rev[13];
    {
      app::Lock lock;
      if (i >= faceN || (faceList[i].face && strcmp(faceList[i].face->rev, faceList[i].rev) == 0)) continue;
      strlcpy(id, faceList[i].id, sizeof id);
      strlcpy(rev, faceList[i].rev, sizeof rev);
    }
    if (millis() - started > FACES_PASS_MS) return true;
    if (!loadCachedFace(id, rev, false)) failed = fetchFace(c, id, rev) != Got::Ok;
  }

  if (!failed) { commitReadyFaceCaches(); pruneFaceCache(); }
  // 2. their images: the chosen face first, then the rest in order
  for (int pass = 0; pass < 2 && !failed; pass++) {
    for (int i = 0; i < MAX_FACES && !failed; i++) {
      bhoomi_clock_v1_SpriteRef need = {};
      bool background = false, found = false;
      {
        app::Lock lock;
        if (i >= faceN || !faceList[i].face) continue;
        const FaceEntry *first = haveTime ? chooseEntry(t) : nullptr;
        if ((pass == 0) != (first == &faceList[i])) continue;
        eachImage(*faceList[i].face, [&](const bhoomi_clock_v1_SpriteRef &r, bool bg) {
          if (!found && !findAsset(r.hash)) {
            need = r;
            background = bg;
            found = true;
          }
        });
      }
      while (found && !failed) {
        if (millis() - started > FACES_PASS_MS) return true;
        if (!loadCachedAsset(need, background)) failed = fetchAsset(c, need, background) != Got::Ok;
        if (failed) break;
        commitReadyFaceCaches();
        found = false;
        app::Lock lock;
        if (i >= faceN || !faceList[i].face) break;
        eachImage(*faceList[i].face, [&](const bhoomi_clock_v1_SpriteRef &r, bool bg) {
          if (!found && !findAsset(r.hash)) {
            need = r;
            background = bg;
            found = true;
          }
        });
      }
    }
  }
  if (failed) facesRetryAt = (millis() + FACES_RETRY_MS) | 1;  // 404s while KV propagates, or a bad download: not too often
  else facesRetryAt = 0;
  return false;
}

// ---------- the task ----------

bool noTokenWarn = false;

bool ready() {
  if (active[KEY_TOKEN].isEmpty()) {
    if (!noTokenWarn) Serial.println("[portal] no device token: set one on the settings page");
    noTokenWarn = true;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) return false;
  {
    app::Lock lock;
    if (!wifisetup::online() || wifisetup::portalActive()) return false;
  }
  if (https() && !timeValid()) {
    if (!gotTimeWarn) Serial.println("[portal] waiting for NTP before the first HTTPS request");
    gotTimeWarn = true;
    return false;
  }
  return true;
}

constexpr uint32_t WEATHER_MS = 15 * 60 * 1000;
bhoomi_clock_v1_Weather weatherState = bhoomi_clock_v1_Weather_init_zero;
String weatherPending, weatherPendingName;
uint32_t weatherNextAt = 0;
int weatherSaveCode = 0;

bool weatherDue() {
  app::Lock lock;
  return !weatherNextAt || (int32_t)(millis() - weatherNextAt) >= 0;
}

void syncWeather(Conn &c) {
  String pending;
  {
    app::Lock lock;
    if (weatherNextAt && (int32_t)(millis() - weatherNextAt) < 0) return;
    weatherNextAt = (millis() + WEATHER_MS) | 1;
    pending = weatherPending;
  }
  if (pending.length()) {
    int code = c.open("/api/device/weather/location?" + pending) ? c.http.PUT((uint8_t *)nullptr, 0) : -1;
    c.close();
    app::Lock lock;
    weatherSaveCode = code;
    if (code == 204 && weatherPending == pending) {
      weatherPending = "";
      weatherPendingName = "";
    } else if (weatherPending == pending) {
      weatherNextAt = (millis() + 60 * 1000) | 1;
      return;
    }
  }
  int code = c.get("/api/device/weather");
  if (code != 200) { c.close(); return; }
  uint8_t body[bhoomi_clock_v1_Weather_size + 32];
  BufSink sink(body, sizeof body);
  int got = writeResponse(c, sink);
  if (got < 0 || sink.overflow) { c.close(); return; }
  c.finish();
  bhoomi_clock_v1_Weather next = bhoomi_clock_v1_Weather_init_zero;
  pb_istream_t in = pb_istream_from_buffer(body, sink.len);
  if (!pb_decode(&in, bhoomi_clock_v1_Weather_fields, &next)) return;
  app::Lock lock;
  if (weatherPending.isEmpty()) weatherState = next;
}

// Firmware is streamed directly into the inactive slot; never buffered in RAM or FFat.
class OtaSink : public Stream {
 public:
  esp_ota_handle_t handle;
  size_t count = 0, expected;
  bool failed = false;
  mbedtls_sha256_context sha;
  OtaSink(esp_ota_handle_t h, size_t size) : handle(h), expected(size) {
    mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
  }
  ~OtaSink() { mbedtls_sha256_free(&sha); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (failed || n > expected - count || esp_ota_write(handle, p, n) != ESP_OK) { failed = true; return 0; }
    mbedtls_sha256_update(&sha, p, n); count += n; return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

uint32_t managementAt = 0, diagnosticAt = 0, diagnosticInterval = 3600;
int otaCode = 0;
String failedRelease;

String runningHash() {
  uint8_t hash[32]; char hex[65];
  if (esp_partition_get_sha256(esp_ota_get_running_partition(), hash) != ESP_OK) return "";
  // This hash is the ESP image digest, not the whole upload hash; used for device identity only.
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
  return hex;
}

void manage(Conn &c) {
  uint32_t now = millis();
  if (managementAt && (int32_t)(now - managementAt) < 0) return;
  managementAt = (now + 60000) | 1;
  Preferences prefs;
  if (!prefs.begin("clock-ota", false)) return;
  String installed = prefs.getString("installed", "");
  // A pending release survived in NVS but is not running: the bootloader rolled it back.
  String attempted = prefs.getString("attempted", "");
  String target = prefs.getString("target", "");
  if (attempted.length() && target != runningHash()) {
    failedRelease = attempted;
    prefs.putString("failed", failedRelease);
    prefs.remove("attempted");
  } else if (attempted.length() && !otahealth::pending()) {
    installed = attempted; prefs.putString("installed", installed); prefs.remove("attempted"); diagnosticAt = 0;
  }
  failedRelease = prefs.getString("failed", "");
  int code = c.get("/api/device/management");
  bhoomi_clock_v1_Management cfg = bhoomi_clock_v1_Management_init_zero;
  uint8_t buffer[128]; BufSink sink(buffer, sizeof(buffer));
  bool got = false;
  if (code == 200 && writeResponse(c, sink) >= 0 && !sink.overflow) {
    auto in = pb_istream_from_buffer(buffer, sink.len);
    got = pb_decode(&in, bhoomi_clock_v1_Management_fields, &cfg);
  }
  c.close();
  if (got) {
    uint32_t interval = cfg.diagnostic_interval;
    if (interval == 0 || interval == 300 || interval == 900 || interval == 1800 || interval == 3600 || interval == 21600) {
      if (interval != diagnosticInterval) diagnosticAt = 0;
      diagnosticInterval = interval;
    }
  }
  if (diagnosticInterval && (!diagnosticAt || (int32_t)(now - diagnosticAt) >= 0)) {
    diagnosticAt = (now + diagnosticInterval * 1000) | 1;
    String body = "{\"installed\":\"" + installed + "\",\"ota_state\":" + String(otahealth::pending() ? 1 : 0) + ",\"firmware\":\"" + String(FW_VERSION) + "\",\"running\":\"" + runningHash() +
      "\",\"failed\":\"" + failedRelease + "\",\"uptime\":" + String((uint32_t)(esp_timer_get_time() / 1000000)) +
      ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"min_heap\":" + String(ESP.getMinFreeHeap()) +
      ",\"largest_block\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) +
      ",\"psram\":" + String(ESP.getFreePsram()) + ",\"reset\":" + String((int)esp_reset_reason()) +
      ",\"rssi\":" + String(WiFi.RSSI()) + ",\"poll_code\":" + String(lastCode) + ",\"ota_code\":" + String(otaCode) + "}";
    if (c.open("/api/device/diagnostics")) {
      c.http.addHeader("Content-Type", "application/json");
      int sent = c.http.PUT((uint8_t *)body.c_str(), body.length());
      if (sent != 200) diagnosticAt = (now + 300000) | 1;
      c.close();
    }
  }
  String release = cfg.release;
  if (!got || release.length() != 64 || release == installed || release == failedRelease ||
      otahealth::pending() || attempted.length() || sound::playing()) { prefs.end(); return; }
  for (char ch : release) if (!isxdigit((unsigned char)ch)) { prefs.end(); return; }
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot || !cfg.size || cfg.size > slot->size) { otaCode = -10; prefs.end(); return; }
  Serial.printf("[ota] downloading %u bytes into %s\n", (unsigned)cfg.size, slot->label);
  code = c.get("/api/device/firmware/" + release);
  if (code != 200 || c.http.getSize() != (int)cfg.size) { otaCode = code == 200 ? -11 : code; c.close(); prefs.end(); return; }
  esp_ota_handle_t handle;
  if (esp_ota_begin(slot, cfg.size, &handle) != ESP_OK) { otaCode = -12; c.close(); prefs.end(); return; }
  OtaSink image(handle, cfg.size);
  int read = c.http.writeToStream(&image);
  c.close();
  uint8_t digest[32]; char hex[65];
  mbedtls_sha256_finish(&image.sha, digest);
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (read < 0 || image.failed || image.count != cfg.size || release != hex) {
    esp_ota_abort(handle); otaCode = -13; prefs.end(); return;
  }
  if (esp_ota_end(handle) != ESP_OK) { otaCode = -14; prefs.end(); return; }
  uint8_t ih[32]; char ihx[65];
  if (esp_partition_get_sha256(slot, ih) != ESP_OK) { otaCode = -15; prefs.end(); return; }
  for (int i = 0; i < 32; i++) snprintf(ihx + i * 2, 3, "%02x", ih[i]);
  if (sound::playing()) { otaCode = -17; prefs.end(); return; }
  // Persist the candidate before selecting it, so a rolled-back image is never retried forever.
  bool saved = prefs.putString("attempted", release) && prefs.putString("target", ihx);
  prefs.end();
  if (!saved || esp_ota_set_boot_partition(slot) != ESP_OK) { otaCode = -16; return; }
  Serial.println("[ota] verified firmware; rebooting into trial image");
  delay(200); ESP.restart();
}

void syncVolume(Conn &c) {
  VolumeState local;
  { app::Lock lock; local = volumeState; }
  if (local.pending) {
    String path = "/api/device/volume?alarm=" + String(local.alarm) + "&video=" + String(local.video);
    int code = c.open(path) ? c.http.PUT((uint8_t *)nullptr, 0) : -1;
    c.close();
    if (code != 204) return;
  }
  int code = c.get("/api/device/volume");
  if (code != 200) { c.close(); return; }
  uint8_t body[32] = {}; BufSink sink(body, sizeof(body)-1);
  int got = writeResponse(c, sink); c.close();
  if (got < 0 || sink.overflow) return;
  body[sink.len] = 0; int alarm, video; char extra;
  if (sscanf((char *)body, "%d %d %c", &alarm, &video, &extra) != 2 ||
      alarm < 0 || alarm > 21 || video < 0 || video > 21) return;
  app::Lock lock;
  // A local edit made during either request wins and will be sent on the next poll.
  VolumeState next;
  if (!volumeSyncResult(volumeState, local, alarm, video, next)) return;
  if (memcmp(&next, &volumeState, sizeof(next)) && saveVolume(next)) {
    volumeState = next; sound::setLevels(next.alarm, next.video);
  }
}

void syncMoodNotes(Conn &c) {
  int code = c.get("/api/device/mood-notes");
  if (code != 200) { c.close(); return; }
  uint8_t body[1024] = {}; BufSink sink(body, sizeof(body)-1);
  int got = writeResponse(c, sink); c.close();
  if (got < 0 || sink.overflow) return;
  body[sink.len] = 0; String values[5]; unsigned seen = 0;
  char *save = nullptr;
  for (char *line = strtok_r((char *)body, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
    if (line[0] < '1' || line[0] > '5' || line[1] != ' ') return;
    int idx = line[0]-'1'; if (seen & (1u << idx)) return;
    uint8_t decoded[121] = {}; size_t len = 0;
    if (mbedtls_base64_decode(decoded, 120, &len, (const uint8_t *)(line+2), strlen(line+2)) != 0 || len > 120) return;
    decoded[len] = 0; values[idx] = (const char *)decoded; seen |= 1u << idx;
  }
  if (seen != 31) return;
  Preferences p; if (!p.begin("mood-notes", false)) return;
  for (int i=0; i<5; ++i) {
    app::Lock lock;
    if (values[i] != moodNotes[i]) {
      String key = "m" + String(i+1);
      if (values[i].isEmpty()) { p.remove(key.c_str()); moodNotes[i] = ""; }
      else if (p.putString(key.c_str(), values[i]) > 0) moodNotes[i] = values[i];
    }
  }
  p.end();
}

// Slow, content-addressed SD-card transfers; a failed or interrupted transfer keeps the old clip.
void syncMoodVideos(Conn &c) {
  static uint32_t next = 0;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = (millis() + 5 * 60 * 1000) | 1;
  if (!storage::mounted()) return;
  int code = c.get("/api/device/mood-videos");
  if (code != 200) { c.close(); return; }
  uint8_t manifest[512] = {};
  BufSink sink(manifest, sizeof(manifest)-1);
  int got = writeResponse(c, sink); c.close();
  if (got < 0 || sink.overflow) return;
  manifest[sink.len] = 0;
  char *save = nullptr;
  for (char *line = strtok_r((char *)manifest, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
    unsigned mood, size; char hash[65] = {};
    if (sscanf(line, "%u %64s %u", &mood, hash, &size) != 3 || mood < 1 || mood > 5 ||
        strlen(hash) != 64 || size < 16 || size > 12 * 1024 * 1024) continue;
    bool hex = true; for (size_t i=0; i<64; ++i) if (!isxdigit((unsigned char)hash[i]) || isupper((unsigned char)hash[i])) hex = false;
    if (!hex) continue;
    Preferences prefs; if (!prefs.begin("mood-videos", false)) return;
    String key = "m" + String(mood), previous = prefs.getString(key.c_str(), ""); prefs.end();
    String activePath = "/mindfulness/" + String(mood) + "/0000.jpg";
    if (previous == hash && SD_MMC.exists(activePath)) { Serial.printf("[mindfulness] mood %u already installed %.12s\n", mood, hash); continue; }
    Serial.printf("[mindfulness] downloading mood %u: %u bytes %.12s\n", mood, size, hash);
    if (!SD_MMC.exists("/mood-staging")) SD_MMC.mkdir("/mood-staging");
    String temp = "/mood-staging/cloud-" + String(mood) + ".part";
    SD_MMC.remove(temp);
    File file = SD_MMC.open(temp, FILE_WRITE); if (!file) return;
    code = c.get(String("/api/device/mood-video/") + mood + "/" + hash);
    int count = code == 200 ? writeResponse(c, file, 180000) : -1;
    c.close(); file.close();
    if (count < 0 || (unsigned)count != size) { Serial.printf("[mindfulness] mood %u download failed: HTTP %d, received %d of %u bytes\n", mood, code, count, size); SD_MMC.remove(temp); return; }
    file = SD_MMC.open(temp, FILE_READ); if (!file) return;
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
    uint8_t buf[2048], digest[32]; size_t n;
    while ((n = file.read(buf, sizeof buf)) != 0) { mbedtls_sha256_update(&sha, buf, n); vTaskDelay(1); }
    file.close(); mbedtls_sha256_finish(&sha, digest); mbedtls_sha256_free(&sha);
    char actual[65]; for(int i=0;i<32;i++) snprintf(actual+i*2, 3, "%02x", digest[i]);
    if (strcmp(actual,hash)) { Serial.printf("[mindfulness] mood %u hash mismatch\n", mood); SD_MMC.remove(temp); return; }
    if (!mood_clip::install(temp.c_str(), mood)) { Serial.printf("[mindfulness] mood %u install failed or player busy; retry later\n", mood); SD_MMC.remove(temp); return; }
    SD_MMC.remove(temp);
    if (prefs.begin("mood-videos", false)) { prefs.putString(key.c_str(), hash); prefs.end(); }
    Serial.printf("[mindfulness] installed video for mood %u\n", mood);
    next = (millis() + POLL_MS) | 1;
    return; // At most one large transfer per interval.
  }
}

void syncMood(Conn &c) {
  mindfulness::Record r;
  { app::Lock lock; if (!mindfulness::peek(r)) return; }
  if (!c.open("/api/device/mood")) return;
  c.http.addHeader("Content-Type", "application/json");
  String body = String("{\"at\":") + r.at + ",\"mood\":" + r.mood + "}";
  int code = c.http.PUT((uint8_t *)body.c_str(), body.length());
  c.close();
  if (code == 200) { app::Lock lock; mindfulness::acknowledge(r); }
}

void task(void *) {
  uint32_t nextAt = 0;  // millis() deadline; 0 = as soon as we're ready
  uint32_t backoff = BACKOFF_MIN_MS;
  int photoTries = 0;
  uint32_t photoRetryAt = 0;  // 0 = whenever the state says there's a new photo
  uint32_t lastLog = 0;
  uint32_t haveRev = 0;  // keysRev of `active`
  Conn c;  // kept for good: the socket stays open between polls
  bool restoredFaces = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!restoredFaces && storage::mounted()) { restoreFaceCache(); restoredFaces = true; }
    {
      app::Lock lock;
      if (haveRev != keysRev) {
        bool first = haveRev == 0;
        haveRev = keysRev;
        for (int k = 0; k < KEY_COUNT; k++) active[k] = keyVal[k];
        noTokenWarn = false;
        if (!first) {  // new keys from the settings page: try them right away, on a fresh connection
          c.close();
          etag = "";
          nextAt = 0;
          backoff = BACKOFF_MIN_MS;
          lastLog = 0;
          Serial.println("[portal] keys changed on the settings page; reconnecting");
        }
      }
    }
    if (!ready()) continue;
    // Own deadline, even while the state heartbeat is backing off.
    manage(c);
    if (weatherDue()) syncWeather(c);
    if (nextAt && (int32_t)(millis() - nextAt) < 0) {
      // Between polls, still pick up a changed /sounds folder promptly.
      if (soundsDue()) syncSounds(c);
      continue;
    }

    quiet = lastLog && millis() - lastLog < 60 * 1000;  // log the routine poll once a minute, not every 30 s
    if (!quiet) {
      lastLog = millis() | 1;
      Serial.printf("[portal] poll (rssi %d, heap %u, min heap %u, psram %u)\n", WiFi.RSSI(), (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getFreePsram());
    }
    Poll p = fetchState(c);
    reachable = p != Poll::Failed;
    uint32_t wait = POLL_MS;
    if (p == Poll::Failed) {
      c.close();
      wait = backoff;
      backoff = std::min<uint32_t>(backoff * 2, FAIL_MAX_MS);
    } else {
      backoff = BACKOFF_MIN_MS;
      if (!photoRetryAt || (int32_t)(millis() - photoRetryAt) >= 0) {
        Photo ph = syncPhoto(c);
        if (ph == Photo::Ok) {
          photoTries = 0;
          photoRetryAt = 0;
        } else {  // retry on every poll for a while, then once a minute
          photoRetryAt = ++photoTries <= PHOTO_TRIES_FAST ? 0 : (millis() + PHOTO_SLOW_MS) | 1;
        }
      }
      syncWeather(c);
      syncCovers(c);
      if (syncFaces(c)) wait = FACES_MORE_MS;  // more faces or images to fetch: don't wait out the whole interval
      syncSounds(c);
      syncVolume(c);
      syncMood(c);
      syncMoodNotes(c);
      syncMoodVideos(c);
    }
    nextAt = (millis() + wait) | 1;  // after the work above: on a weak signal it can take seconds
  }
}

void loadKeys() {
  Preferences prefs;
  prefs.begin("portal", false);  // read-write: creates the namespace, so a read-only open doesn't log an error
  for (int k = 0; k < KEY_COUNT; k++) {
    String saved = prefs.getString(KEY_NVS[k], "");
    keyCustom[k] = saved.length() > 0;
    keyVal[k] = keyCustom[k] ? saved : String(KEY_BUILTIN[k]);
  }
  prefs.end();
  Serial.print("[portal] keys:");
  for (int k = 0; k < KEY_COUNT; k++)
    Serial.printf(" %s=%s", KEY_JSON[k], keyVal[k].isEmpty() ? "none" : keyCustom[k] ? "settings" : "built-in");
  Serial.println();
}

}  // namespace

bool configured() { return PORTAL_URL[0] != 0; }
bool haveState() { return have; }
const State &state() { return cur; }
const char *note() { return have && cur.has_note && cur.note.text[0] ? cur.note.text : nullptr; }
const uint8_t *photo(size_t *size) {
  if (size) *size = photoLen;
  return photoBuf;
}
const char *photoId() { return photoIdStr; }
uint32_t revision() { return rev; }

bool nowPlayingFresh() { return have && okAt && millis() - okAt < NP_STALE_MS; }
const NowPlaying &nowPlaying() { return cur.nowplaying; }
const uint8_t *cover(int side, size_t *size) {
  side = side ? 1 : 0;
  if (size) *size = coverLen[side];
  return coverBuf[side];
}
const char *coverId(int side) { return coverIdStr[side ? 1 : 0]; }
uint32_t nowPlayingRevision() { return npRev; }

const Face *chooseFace(const struct tm &t, bool *ready) {
  const FaceEntry *e = chooseEntry(t);
  if (ready) *ready = !e || (e->face && faceReady(*e->face));
  return e && e->face && faceReady(*e->face) ? e->face : nullptr;
}
const uint8_t *asset(const char *hash, size_t *len) {
  const Asset *a = findAsset(hash);
  if (len) *len = a ? a->len : 0;
  return a ? a->buf : nullptr;
}
uint32_t facesRevision() { return facesRev; }
const char *moodNote(unsigned mood) { return mood >= 1 && mood <= 5 ? moodNotes[mood-1].c_str() : ""; }

// ---- keys (the LAN settings page; app::Lock held by the caller) ----

const char *keyProblem(Key k, const String &v) {
  if (v.length() > KEY_MAX) return "That's too long.";
  for (size_t i = 0; i < v.length(); i++)
    if (v[i] <= ' ' || v[i] > '~' || v[i] == '"' || v[i] == '\\') return "That has spaces or odd characters in it.";
  bool looksLikeId = v.endsWith(".access");
  if (k == KEY_ACCESS_ID && v.length() && !looksLikeId) return "A Client ID ends in \".access\".";
  if (k != KEY_ACCESS_ID && looksLikeId) return "That looks like the Client ID, not this one.";
  return nullptr;
}

bool setKey(Key k, const String &value, String *error) {
  if (k < 0 || k >= KEY_COUNT) return false;
  if (const char *why = keyProblem(k, value)) {
    if (error) *error = why;
    return false;
  }
  Preferences prefs;
  prefs.begin("portal", false);
  bool ok = value.isEmpty() ? (prefs.remove(KEY_NVS[k]) || !prefs.isKey(KEY_NVS[k]))
                            : prefs.putString(KEY_NVS[k], value) == value.length();
  prefs.end();
  if (!ok) {
    if (error) *error = "Couldn't save it on the clock.";
    return false;
  }
  keyCustom[k] = value.length() > 0;
  keyVal[k] = keyCustom[k] ? value : String(KEY_BUILTIN[k]);
  keysRev++;
  lastCode = 0;  // "connecting" until the next poll with the new key answers, not the old key's result
  lastAt = 0;
  Serial.printf("[portal] %s %s from the settings page\n", KEY_JSON[k], keyCustom[k] ? "changed" : "reset to built-in");
  return true;
}

String keysJson() {
  const char *state = "waiting";
  if (!PORTAL_URL[0]) state = "off";
  else if (keyVal[KEY_TOKEN].isEmpty()) state = "nokey";
  else if (lastCode == 200 || lastCode == 304) state = "ok";
  else if (lastCode == 302 || lastCode == 403) state = "access";
  else if (lastCode == 401) state = "token";
  else if (lastCode == 301) state = "https";
  else if (lastCode < 0) state = "offline";
  else if (lastCode > 0) state = "error";
  String out = String("{\"url\":\"") + PORTAL_URL + "\",\"state\":\"" + state + "\",\"code\":" + lastCode +
               ",\"ago\":" + (lastAt ? String((millis() - lastAt) / 1000) : String("null")) +
               ",\"okAgo\":" + (okAt ? String((millis() - okAt) / 1000) : String("null")) + ",\"keys\":{";
  for (int k = 0; k < KEY_COUNT; k++) {
    const String &v = keyVal[k];
    // 4 characters, so you can tell which key is in use (the start of a Client ID, which always ends
    // in ".access"; the end of the others); nothing at all for short values
    String hint = v.length() < 16      ? String()
                  : k == KEY_ACCESS_ID ? v.substring(0, 4) + "\u2026"
                                       : String("\u2026") + v.substring(v.length() - 4);
    out += String(k ? "," : "") + "\"" + KEY_JSON[k] + "\":{\"set\":" + (v.length() ? "true" : "false") +
           ",\"custom\":" + (keyCustom[k] ? "true" : "false") + ",\"builtin\":" + (KEY_BUILTIN[k][0] ? "true" : "false") +
           ",\"hint\":\"" + hint + "\"}";
  }
  return out + "}}";
}

bool healthy() { app::Lock lock; return lastCode == 200 || lastCode == 304; }

int weatherFaceGroup() {
  time_t now = time(nullptr);
  if (!weatherState.available || weatherPending.length() || now < weatherState.observed_at || now - weatherState.observed_at > 3 * 3600) return faceweather::BASE;
  return faceweather::group(weatherState.code);
}

void weatherText(char *out, size_t cap) {
  out[0] = 0;
  time_t now = time(nullptr);
  if (!weatherState.available || weatherPending.length() || now < weatherState.observed_at || now - weatherState.observed_at > 3 * 3600) return;
  int c = weatherState.code;
  const char *icon = c == 0 ? (weatherState.is_day ? "☀" : "☾") :
                     c <= 2 ? "⛅" : c == 3 ? "☁" : c <= 48 ? "≋" :
                     c >= 95 ? "ϟ" : (c >= 71 && c <= 77) || c == 85 || c == 86 ? "❄" : "☂";
  snprintf(out, cap, " %s %ld°C", icon, (long)weatherState.temperature_c);
}

String volumeSettingsJson() {
  return String("{\"alarm\":") + volumeState.alarm + ",\"video\":" + volumeState.video +
         ",\"pending\":" + (volumeState.pending ? "true" : "false") + "}";
}
bool setLocalVolume(int alarm, int video) {
  if (alarm < 0 || alarm > 21 || video < 0 || video > 21) return false;
  VolumeState next = {1, (uint8_t)alarm, (uint8_t)video, 1};
  if (!saveVolume(next)) return false;
  volumeState = next; sound::setLevels(next.alarm, next.video); return true;
}

String weatherSettingsJson() {
  return "{\"city\":\"" + wifisetup::jsonEscape(weatherState.city) + "\",\"pending\":" +
    (weatherPending.length() ? "true" : "false") + ",\"pendingCity\":\"" + wifisetup::jsonEscape(weatherPendingName) +
    "\",\"code\":" + weatherSaveCode + "}";
}

bool setWeatherLocation(const String &name, double lat, double lon) {
  if (name.length() > 320 || !isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) return false;
  weatherPending = "name=" + urlEncode(name.c_str()) + "&latitude=" + String(lat, 5) + "&longitude=" + String(lon, 5);
  weatherPendingName = name;
  weatherSaveCode = 0;
  weatherState.available = false;
  weatherNextAt = 0;
  return true;
}

void begin() {
  loadSaved();  // before anything else, so the face has content even if the portal is unreachable
  loadKeys();
  Preferences savedVolume;
  if (savedVolume.begin("volume", false)) {
    VolumeState v = {};
    if (savedVolume.getBytesLength("state") == sizeof(v) && savedVolume.getBytes("state", &v, sizeof(v)) == sizeof(v) &&
        v.version == 1 && v.alarm <= 21 && v.video <= 21 && v.pending <= 1) volumeState = v;
    savedVolume.end();
  }
  sound::setLevels(volumeState.alarm, volumeState.video);
  Preferences notes;
  if (notes.begin("mood-notes", false)) {
    for (int i=0; i<5; ++i) { String key = "m" + String(i+1); moodNotes[i] = notes.getString(key.c_str(), ""); }
    notes.end();
  }
  if (!configured()) {
    Serial.println("[portal] no PORTAL_URL (copy portal_config.example.h to portal_config.h); client is off");
    return;
  }
  if (https()) mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  Serial.printf("[portal] polling %s every %u s\n", PORTAL_URL, (unsigned)(POLL_MS / 1000));
  // Core 0 with Wi-Fi and the web server; stack is generous because mbedTLS handshakes run in it.
  xTaskCreatePinnedToCore(task, "portal", 16 * 1024, nullptr, 1, nullptr, 0);
}

}  // namespace portal
