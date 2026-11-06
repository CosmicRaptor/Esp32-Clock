#include "web.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "page_html.h"
#include "settings.h"
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

void handleRoot() {
  if (redirectIfCaptive()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleInfo() {
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

void handleZones() {
  server.sendHeader("Cache-Control", "max-age=3600");
  server.send(200, "application/json", tz::listJson());
}

void handleScan() {
  if (!wifisetup::portalActive()) return sendJson("{\"scanning\":false,\"networks\":[]}");
  sendJson(wifisetup::scanJson(server.hasArg("refresh")));
}

void handleConnect() {
  if (!wifisetup::portalActive()) return sendJson("{\"ok\":false}", 409);
  String s = server.arg("ssid");
  String p = server.arg("pass");
  if (s.isEmpty() || s.length() > 32 || p.length() > 63) return sendJson("{\"ok\":false}", 400);
  if (server.hasArg("tz")) settings::setTimezone(server.arg("tz"));
  sendJson("{\"ok\":true}");
  wifisetup::connect(s, p);
}

void handleStatus() { sendJson(wifisetup::statusJson()); }

void handleTz() {
  bool ok = settings::setTimezone(server.arg("tz"));
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}", ok ? 200 : 400);
}

void handleNotFound() {
  if (redirectIfCaptive()) return;
  server.send(404, "text/plain", "Not found");
}

}  // namespace

void begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/zones", HTTP_GET, handleZones);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/tz", HTTP_POST, handleTz);
  server.onNotFound(handleNotFound);
  server.begin();
}

void onOnline() {
  if (mdnsUp) return;
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
    Serial.printf("[web] settings at http://%s.local/\n", HOSTNAME);
  }
}

void loop() { server.handleClient(); }

}  // namespace web
