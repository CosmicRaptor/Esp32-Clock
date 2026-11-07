#include "web.h"

#include <ESPmDNS.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "app_lock.h"
#include "page_html.h"
#include "portal_client.h"
#include "settings.h"
#include "storage.h"
#include "wifi_setup.h"

namespace web {
namespace {

WebServer server(80);
bool mdnsUp = false;

using wifisetup::jsonEscape;

void sendJson(const String &body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const String &msg) {
  sendJson("{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}", code);
}

// While the hotspot is up, phones probe random hosts (captive.apple.com, connectivitycheck...).
// Anything not addressed to us gets bounced to the setup page so the OS shows its portal sheet.
bool redirectIfCaptive() {
  if (!wifisetup::portalActive()) return false;
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  if (host == wifisetup::apIp().toString() || host == WiFi.localIP().toString() ||
      host == String(HOSTNAME) + ".local")
    return false;
  server.sendHeader("Location", String("http://") + wifisetup::apIp().toString() + "/", true);
  server.send(302, "text/plain", "");
  return true;
}

// ---------- page, settings, Wi-Fi ----------

void handleRoot() {
  if (redirectIfCaptive()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleInfo() {
  app::Lock lock;
  const tz::Zone *z = settings::timezone();
  char clock[16] = "";
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(clock, sizeof clock, "%H:%M", &t);
  }
  String out = String("{\"mode\":\"") + (wifisetup::portalActive() ? "setup" : "settings") +
               "\",\"tz\":\"" + (z ? z->id : "") + "\",\"clock\":\"" + clock + "\"}";
  sendJson(out);
}

void handleWeatherGet() {
  if (wifisetup::portalActive()) return sendError(409, "Not available during setup");
  app::Lock lock;
  sendJson(portal::weatherSettingsJson());
}

void handleWeatherSet() {
  if (wifisetup::portalActive()) return sendError(409, "Not available during setup");
  if (server.header("X-Clock") != "1") return sendError(403, "missing X-Clock header");
  char *endLat, *endLon;
  String a = server.arg("latitude"), b = server.arg("longitude");
  double lat = strtod(a.c_str(), &endLat), lon = strtod(b.c_str(), &endLon);
  if (a.isEmpty() || b.isEmpty() || *endLat || *endLon) return sendError(400, "Invalid coordinates");
  app::Lock lock;
  if (!portal::setWeatherLocation(server.arg("name"), lat, lon)) return sendError(400, "Invalid city");
  sendJson(portal::weatherSettingsJson(), 202);
}

void handleZones() {
  server.sendHeader("Cache-Control", "max-age=3600");
  server.send(200, "application/json", tz::listJson());
}

void handleScan() {
  app::Lock lock;
  if (!wifisetup::portalActive()) return sendJson("{\"scanning\":false,\"networks\":[]}");
  sendJson(wifisetup::scanJson(server.hasArg("refresh")));
}

void handleConnect() {
  app::Lock lock;
  if (!wifisetup::portalActive()) return sendError(409, "not in setup mode");
  String s = server.arg("ssid");
  String p = server.arg("pass");
  if (s.isEmpty() || s.length() > 32 || p.length() > 63) return sendError(400, "bad network");
  if (server.hasArg("tz")) settings::setTimezone(server.arg("tz"));
  sendJson("{\"ok\":true}");
  wifisetup::connect(s, p);
}

void handleStatus() {
  app::Lock lock;
  sendJson(wifisetup::statusJson());
}

void handleTz() {
  app::Lock lock;
  bool ok = settings::setTimezone(server.arg("tz"));
  if (ok) sendJson("{\"ok\":true}");
  else sendError(400, "unknown time zone");
}

// ---------- cloud portal keys ----------
// The device token and the Cloudflare Access service token expire or get rotated; these let them be
// replaced from the settings page without a reflash. Home network only. The keys are write-only:
// GET /portal returns the connection state and the last few characters of each key, never a value.
// Changes need an X-Clock header, which a plain form or a fetch from another site can't send without
// a CORS preflight (that this server never answers), so a web page she happens to open can't change them.

void handlePortalGet() {
  if (wifisetup::portalActive()) return sendError(409, "not available during setup");
  app::Lock lock;
  sendJson(portal::keysJson());
}

void handlePortalSet() {
  if (wifisetup::portalActive()) return sendError(409, "not available during setup");
  if (server.header("X-Clock") != "1") return sendError(403, "missing X-Clock header");
  static const char *const ARGS[portal::KEY_COUNT] = {"token", "accessId", "accessSecret"};
  app::Lock lock;
  bool reset = server.arg("reset") == "1";  // every key back to the built-in one
  String val[portal::KEY_COUNT];
  bool use[portal::KEY_COUNT] = {};
  int n = 0;
  for (int k = 0; k < portal::KEY_COUNT; k++) {  // check everything first: all or nothing
    val[k] = reset ? String() : server.arg(ARGS[k]);
    val[k].trim();
    use[k] = reset || val[k].length();  // a field left blank means "keep it"
    if (!use[k]) continue;
    n++;
    if (const char *why = portal::keyProblem((portal::Key)k, val[k])) return sendError(400, why);
  }
  if (!n) return sendError(400, "Nothing to save: fill in the key that changed.");
  for (int k = 0; k < portal::KEY_COUNT; k++) {
    String why;
    if (use[k] && !portal::setKey((portal::Key)k, val[k], &why)) return sendError(500, why);
  }
  sendJson(portal::keysJson());
}

// ---------- files on the SD card ----------
// Files are only reachable on the home network (not while the setup hotspot is up).

bool filesAllowed() {
  if (wifisetup::portalActive()) {
    sendError(409, "not available during setup");
    return false;
  }
  if (!storage::begin()) {  // also picks up a card inserted after boot
    sendError(503, "No SD card found. Insert one and try again.");
    return false;
  }
  return true;
}

// Resolves ?folder=&name= to a path, or sends an error.
bool argPath(String &path, String &name) {
  String folder = server.arg("folder");
  name = storage::cleanName(server.arg("name"));
  if (!storage::validFolder(folder) || name.isEmpty()) {
    sendError(400, "bad file name");
    return false;
  }
  path = storage::folderPath(folder) + "/" + name;
  return true;
}

void handleFiles() {
  if (!filesAllowed()) return;
  String folder = server.arg("folder");
  if (!storage::validFolder(folder)) return sendError(400, "bad folder");
  File dir = SD_MMC.open(storage::folderPath(folder));
  String out = "{\"ok\":true,\"total\":" + String((double)storage::totalBytes(), 0) +
               ",\"used\":" + String((double)storage::usedBytes(), 0) + ",\"files\":[";
  bool first = true;
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      // skip folders, macOS litter (._foo, .DS_Store) and unfinished uploads
      if (f.isDirectory() || n.startsWith(".") || n.endsWith(".part")) continue;
      if (!first) out += ',';
      first = false;
      out += "{\"name\":\"" + jsonEscape(n) + "\",\"size\":" + String((unsigned long)f.size()) + "}";
    }
  }
  out += "]}";
  sendJson(out);
}

String contentType(const String &name) {
  String n = name;
  n.toLowerCase();
  if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
  if (n.endsWith(".png")) return "image/png";
  if (n.endsWith(".mp3")) return "audio/mpeg";
  if (n.endsWith(".wav")) return "audio/wav";
  if (n.endsWith(".m4a") || n.endsWith(".aac")) return "audio/mp4";
  if (n.endsWith(".flac")) return "audio/flac";
  if (n.endsWith(".ogg") || n.endsWith(".opus")) return "audio/ogg";
  return "application/octet-stream";
}

void handleFile() {
  if (!filesAllowed()) return;
  String path, name;
  if (!argPath(path, name)) return;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return sendError(404, "not found");
  server.sendHeader("Cache-Control", "max-age=86400");
  server.streamFile(f, contentType(name));
  f.close();
}

void handleDelete() {
  if (!filesAllowed()) return;
  String path, name;
  if (!argPath(path, name)) return;
  if (!SD_MMC.exists(path)) return sendError(404, "not found");
  if (!SD_MMC.remove(path)) return sendError(500, "could not delete");
  storage::touch();
  Serial.printf("[sd] deleted %s\n", path.c_str());
  sendJson("{\"ok\":true}");
}

// Upload: POST /upload?folder=photos&name=x.jpg&size=N, multipart body with one file.
// Written to "<name>.part" through a RAM buffer, renamed when complete; an existing name gets
// a " (2)" suffix instead of being overwritten.
uint8_t *uploadBuf = nullptr;  // allocated once, in PSRAM

struct Upload {
  File file;
  String tmpPath, finalPath, finalName, error;
  uint8_t *buf = nullptr;  // = uploadBuf
  size_t used = 0;
  size_t total = 0;
  uint32_t started = 0;
} up;

constexpr size_t UPLOAD_BUF = 32 * 1024;

bool flushBuf() {
  if (!up.used) return true;
  bool ok = up.file.write(up.buf, up.used) == up.used;
  up.used = 0;
  return ok;
}

void abortUpload(const String &why) {
  if (up.error.isEmpty()) up.error = why;
  if (up.file) up.file.close();
  if (up.tmpPath.length() && SD_MMC.exists(up.tmpPath)) SD_MMC.remove(up.tmpPath);
  up.used = 0;
}

String uniqueName(const String &folder, const String &name) {
  if (!SD_MMC.exists(storage::folderPath(folder) + "/" + name)) return name;
  int dot = name.lastIndexOf('.');
  String base = dot > 0 ? name.substring(0, dot) : name;
  String ext = dot > 0 ? name.substring(dot) : "";
  for (int i = 2; i < 1000; i++) {
    String n = base + " (" + i + ")" + ext;
    if (!SD_MMC.exists(storage::folderPath(folder) + "/" + n)) return n;
  }
  return "";
}

void handleUploadData() {
  HTTPUpload &u = server.upload();
  switch (u.status) {
    case UPLOAD_FILE_START: {
      up = Upload();
      up.started = millis();
      if (wifisetup::portalActive() || !storage::begin()) return abortUpload("No SD card found.");
      String folder = server.arg("folder");
      String name = storage::cleanName(server.arg("name"));
      if (!storage::validFolder(folder) || name.isEmpty()) return abortUpload("bad file name");
      uint64_t want = strtoull(server.arg("size").c_str(), nullptr, 10);
      uint64_t free = storage::totalBytes() - storage::usedBytes();
      if (want + (1 << 20) > free) return abortUpload("The card is full.");
      up.finalName = uniqueName(folder, name);
      if (up.finalName.isEmpty()) return abortUpload("too many files with that name");
      up.finalPath = storage::folderPath(folder) + "/" + up.finalName;
      up.tmpPath = up.finalPath + ".part";
      if (!uploadBuf) uploadBuf = (uint8_t *)heap_caps_malloc(UPLOAD_BUF, MALLOC_CAP_SPIRAM);
      up.buf = uploadBuf;
      if (!up.buf) return abortUpload("out of memory");
      up.file = SD_MMC.open(up.tmpPath, FILE_WRITE);
      if (!up.file) return abortUpload("could not create file");
      Serial.printf("[sd] upload start %s\n", up.finalPath.c_str());
      break;
    }
    case UPLOAD_FILE_WRITE: {
      if (!up.file) return;  // already failed; drain the rest of the body
      size_t off = 0;
      while (off < u.currentSize) {
        size_t n = min(UPLOAD_BUF - up.used, u.currentSize - off);
        memcpy(up.buf + up.used, u.buf + off, n);
        up.used += n;
        off += n;
        if (up.used == UPLOAD_BUF && !flushBuf()) return abortUpload("write failed (card full?)");
      }
      up.total += u.currentSize;
      break;
    }
    case UPLOAD_FILE_END: {
      if (!up.file) return;
      if (!flushBuf()) return abortUpload("write failed (card full?)");
      up.file.close();
      if (!SD_MMC.rename(up.tmpPath, up.finalPath)) return abortUpload("could not save file");
      storage::touch();
      uint32_t ms = millis() - up.started;
      Serial.printf("[sd] saved %s (%u bytes, %u ms, %.0f KB/s)\n", up.finalPath.c_str(),
                    (unsigned)up.total, (unsigned)ms, ms ? up.total / 1.024 / ms : 0.0);
      break;
    }
    case UPLOAD_FILE_ABORTED:
      abortUpload("upload interrupted");
      break;
  }
}

void handleUploadDone() {
  if (up.error.length()) return sendError(up.error == "The card is full." ? 507 : 400, up.error);
  if (up.finalName.isEmpty()) return sendError(400, "no file received");
  sendJson("{\"ok\":true,\"name\":\"" + jsonEscape(up.finalName) + "\"}");
}

void handleNotFound() {
  if (redirectIfCaptive()) return;
  server.send(404, "text/plain", "Not found");
}

void task(void *) {
  server.begin();
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace

void begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/weather", HTTP_GET, handleWeatherGet);
  server.on("/weather", HTTP_POST, handleWeatherSet);
  server.on("/zones", HTTP_GET, handleZones);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/tz", HTTP_POST, handleTz);
  server.on("/files", HTTP_GET, handleFiles);
  server.on("/file", HTTP_GET, handleFile);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/portal", HTTP_GET, handlePortalGet);
  server.on("/portal", HTTP_POST, handlePortalSet);
  static const char *headers[] = {"X-Clock"};
  server.collectHeaders(headers, 1);
  server.onNotFound(handleNotFound);
  // Core 0 (with Wi-Fi); the main loop and LVGL run on core 1.
  xTaskCreatePinnedToCore(task, "web", 12 * 1024, nullptr, 1, nullptr, 0);
}

void onOnline() {
  if (mdnsUp) return;
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
    Serial.printf("[web] settings at http://%s.local/\n", HOSTNAME);
  }
}

}  // namespace web
