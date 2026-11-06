#include "portal_client.h"

#include <FFat.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "app_lock.h"
#include "settings.h"
#include "storage.h"
#include "wifi_setup.h"

#if __has_include("portal_config.h")
#include "portal_config.h"
#else
#include "portal_config.example.h"
#endif

namespace portal {
namespace {

constexpr uint32_t POLL_MS = 10 * 1000;         // the one heartbeat: state + now playing. Mostly a bare 304
constexpr uint32_t BACKOFF_MIN_MS = 10 * 1000;  // after a failed poll: 10 s, doubling up to FAIL_MAX_MS
constexpr uint32_t FAIL_MAX_MS = 60 * 1000;
constexpr int PHOTO_TRIES_FAST = 6;             // photo pending (404 while KV propagates, or a bad download): every poll...
constexpr uint32_t PHOTO_SLOW_MS = 60 * 1000;   // ...then once a minute
constexpr uint32_t NP_STALE_MS = 2 * 60 * 1000; // no answer from the portal for this long: the strip is hidden, not stale
constexpr int32_t CONNECT_TIMEOUT_MS = 12000;   // weak Wi-Fi (~-90 dBm): TLS handshake is slow
constexpr uint16_t IO_TIMEOUT_MS = 15000;
constexpr size_t MAX_PHOTO_BYTES = 256 * 1024;  // a 320x240 JPEG is ~10-40 KB; refuse anything silly

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
String etag;
bool gotTimeWarn = false;
bool quiet = false;  // don't log routine polls (the 10 s heartbeat would drown the serial monitor)

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
// Wi-Fi takes seconds, and we poll every 10 s.
struct Conn {
  WiFiClient plain;
  WiFiClientSecure secure;
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
    if (ok) http.addHeader("Authorization", String("Bearer ") + PORTAL_DEVICE_TOKEN);
    return ok;
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
    if (code == 401 || code == 403) Serial.println("[portal] the device token was rejected; check portal_config.h");
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
  int got = c.http.writeToStream(&sink);
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
    int got = c.http.writeToStream(&sink);
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
      int got = c.http.writeToStream(&sink);
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

// ---------- the task ----------

bool ready() {
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

void task(void *) {
  uint32_t nextAt = 0;  // millis() deadline; 0 = as soon as we're ready
  uint32_t backoff = BACKOFF_MIN_MS;
  int photoTries = 0;
  uint32_t photoRetryAt = 0;  // 0 = whenever the state says there's a new photo
  uint32_t lastLog = 0;
  Conn c;  // kept for good: the socket stays open between polls
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!ready()) continue;
    if (nextAt && (int32_t)(millis() - nextAt) < 0) {
      // Between polls, still pick up a changed /sounds folder promptly.
      if (soundsDue()) syncSounds(c);
      continue;
    }

    quiet = lastLog && millis() - lastLog < 60 * 1000;  // log the routine poll once a minute, not every 10 s
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
      syncCovers(c);
      syncSounds(c);
    }
    nextAt = (millis() + wait) | 1;  // after the work above: on a weak signal it can take seconds
  }
}

}  // namespace

bool configured() { return PORTAL_URL[0] && PORTAL_DEVICE_TOKEN[0]; }
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

void begin() {
  loadSaved();  // before anything else, so the face has content even if the portal is unreachable
  if (!configured()) {
    Serial.println("[portal] not configured (copy portal_config.example.h to portal_config.h); client is off");
    return;
  }
  Serial.printf("[portal] polling %s every %u s\n", PORTAL_URL, (unsigned)(POLL_MS / 1000));
  // Core 0 with Wi-Fi and the web server; stack is generous because mbedTLS handshakes run in it.
  xTaskCreatePinnedToCore(task, "portal", 16 * 1024, nullptr, 1, nullptr, 0);
}

}  // namespace portal
