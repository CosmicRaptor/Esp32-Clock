#include "wifi_setup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

namespace wifisetup {
namespace {

constexpr uint8_t BOOT_PIN = 0;               // BOOT button, active low
constexpr uint32_t JOIN_TIMEOUT_MS = 20000;   // any single join attempt
constexpr uint32_t PORTAL_LINGER_MS = 30000;  // max AP time after success (phone may miss the news)
constexpr uint32_t PORTAL_SEEN_MS = 3000;     // AP time after the phone has seen "connected"
constexpr uint32_t PORTAL_RETRY_MS = 60000;   // background retry of saved creds while portal is open
constexpr uint32_t FORGET_HOLD_MS = 5000;

const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

struct Net {
  String ssid;
  int32_t rssi;
  bool open;
};

Preferences prefs;
DNSServer dns;

Listener listener = nullptr;
Status st;
String apName;
String savedSsid, savedPass;

bool portalUp = false;
uint32_t portalCloseAt = 0;  // 0 = not scheduled

bool joining = false;          // a WiFi.begin() is in flight that we are timing
bool joinIsCandidate = false;  // ...for credentials typed into the setup page
uint32_t joinStart = 0;
String candSsid, candPass;
uint32_t lastSavedRetry = 0;

volatile uint8_t lastDiscReason = 0;

std::vector<Net> nets;
bool scanning = false;

uint32_t bootDownSince = 0;

// ---------- helpers ----------

void setStatus(Phase p, const String &ssid, const String &detail = "") {
  if (st.phase == p && st.ssid == ssid && st.detail == detail) return;
  st = {p, ssid, detail};
  if (listener) listener(st);
}

String reasonText(uint8_t r) {
  switch (r) {
    case 2:    // AUTH_EXPIRE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return "Wrong password? The network rejected it.";
    case 201:  // NO_AP_FOUND
    case 210:  // NO_AP_FOUND_W_COMPATIBLE_SECURITY
    case 211:  // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
    case 212:  // NO_AP_FOUND_IN_RSSI_THRESHOLD
      return "Couldn't find that network. Is it 2.4 GHz and in range?";
    default:
      return "Couldn't connect (code " + String(r) + ").";
  }
}

void loadCreds() {
  prefs.begin("wifi", true);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
}

void saveCreds(const String &s, const String &p) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
  savedSsid = s;
  savedPass = p;
}

void startJoin(const String &s, const String &p, bool candidate) {
  WiFi.disconnect(false, false);
  lastDiscReason = 0;
  joining = true;
  joinIsCandidate = candidate;
  joinStart = millis();
  WiFi.begin(s.c_str(), p.c_str());
}

void collectScan(int n) {
  nets.clear();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    int32_t rssi = WiFi.RSSI(i);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    auto it = std::find_if(nets.begin(), nets.end(), [&](const Net &x) { return x.ssid == s; });
    if (it == nets.end()) nets.push_back({s, rssi, open});
    else if (rssi > it->rssi) it->rssi = rssi;
  }
  std::sort(nets.begin(), nets.end(), [](const Net &a, const Net &b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
  Serial.printf("[wifi] scan: %u networks\n", (unsigned)nets.size());
}

void startScan() {
  if (scanning || joining) return;  // scanning mid-join breaks the join
  int16_t r = WiFi.scanNetworks(true, true);
  if (r != WIFI_SCAN_FAILED) scanning = true;
}

// ---------- portal lifecycle ----------

void startPortal() {
  if (portalUp) return;
  Serial.printf("[wifi] starting setup portal \"%s\"\n", apName.c_str());

  // Scan in plain STA mode first (radio idle, not serving an AP) so the page opens with a list.
  // The first scan right after a mode change can fail, so retry.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(200);
  for (int i = 0; i < 3; i++) {
    int n = WiFi.scanNetworks(false, true);
    if (n >= 0) {
      collectScan(n);
      break;
    }
    delay(500);
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(apName.c_str());  // open network: nothing to type on the phone
  delay(100);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);  // every hostname resolves to us -> phone shows the captive sheet

  portalUp = true;
  portalCloseAt = 0;
  lastSavedRetry = millis();
  setStatus(Phase::Portal, apName);
}

void stopPortal() {
  if (!portalUp) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalUp = false;
  Serial.println("[wifi] setup portal closed");
}

void becameOnline() {
  joining = false;
  WiFi.setAutoReconnect(true);
  Serial.printf("[wifi] online: \"%s\" ip=%s rssi=%d dBm\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  setStatus(Phase::Online, WiFi.SSID(), WiFi.localIP().toString());
}

// A join we started has finished one way or the other.
void joinLoop() {
  if (!joining) return;
  wl_status_t ws = WiFi.status();
  if (ws == WL_CONNECTED) {
    if (joinIsCandidate) {
      saveCreds(candSsid, candPass);
      Serial.println("[wifi] credentials saved");
      portalCloseAt = millis() + PORTAL_LINGER_MS;
    } else if (portalUp) {
      portalCloseAt = millis() + 1000;  // background retry of saved network worked
    }
    becameOnline();
    return;
  }
  bool timedOut = millis() - joinStart > JOIN_TIMEOUT_MS;
  if (!timedOut && ws != WL_CONNECT_FAILED && ws != WL_NO_SSID_AVAIL) return;

  joining = false;
  WiFi.disconnect(false, false);  // stop the driver retrying on its own
  Serial.printf("[wifi] join failed (reason %u, status %d)\n", lastDiscReason, ws);
  if (joinIsCandidate) {
    // Weak links fail the handshake too, which would otherwise read as "wrong password".
    auto it = std::find_if(nets.begin(), nets.end(), [](const Net &n) { return n.ssid == candSsid; });
    String why = (it != nets.end() && it->rssi < -85)
                     ? "The signal is very weak here. Try moving the clock closer to the router."
                     : reasonText(lastDiscReason);
    setStatus(Phase::PortalFailed, candSsid, why);
  } else if (!portalUp) {
    startPortal();  // boot-time join of the saved network failed
  } else {
    lastSavedRetry = millis();  // background retry failed; stay in portal quietly
  }
}

void portalLoop() {
  dns.processNextRequest();

  if (scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      collectScan(n);
      scanning = false;
    } else if (n == WIFI_SCAN_FAILED) {
      scanning = false;
    }
  }

  uint32_t now = millis();
  if (portalCloseAt && (int32_t)(now - portalCloseAt) >= 0) {
    stopPortal();
    return;
  }

  // Router came back while we were sitting in the portal? Quietly retry the saved network,
  // but never while a phone is on the setup AP (a retry hops channels and drops them).
  if (!joining && !scanning && st.phase == Phase::Portal && !savedSsid.isEmpty() &&
      now - lastSavedRetry > PORTAL_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
    Serial.printf("[wifi] retrying saved network \"%s\"\n", savedSsid.c_str());
    lastSavedRetry = now;
    startJoin(savedSsid, savedPass, false);
  }
}

void checkForgetButton() {
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!bootDownSince) {
      bootDownSince = millis() | 1;
    } else if (millis() - bootDownSince > FORGET_HOLD_MS) {
      Serial.println("[wifi] BOOT held: forgetting Wi-Fi and rebooting");
      forget();
      delay(200);
      ESP.restart();
    }
  } else {
    bootDownSince = 0;
  }
}

}  // namespace

// ---------- public API ----------

String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof buf, "\\u%04x", c);
          o += buf;
        } else {
          o += c;
        }
    }
  }
  return o;
}

void begin(const char *name, Listener onChange) {
  apName = name;
  listener = onChange;
  pinMode(BOOT_PIN, INPUT_PULLUP);

  WiFi.persistent(false);  // we store credentials ourselves (only after they've been proven)
  WiFi.setHostname("bhoomi-clock");
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        lastDiscReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  loadCreds();
  if (savedSsid.isEmpty()) {
    Serial.println("[wifi] no saved network");
    startPortal();
    return;
  }
  Serial.printf("[wifi] joining saved network \"%s\"\n", savedSsid.c_str());
  WiFi.mode(WIFI_STA);
  startJoin(savedSsid, savedPass, false);
  setStatus(Phase::JoiningSaved, savedSsid);
}

void loop() {
  checkForgetButton();
  joinLoop();
  if (portalUp) portalLoop();

  if (!joining && !portalUp) {
    bool up = WiFi.status() == WL_CONNECTED;
    if (!up && st.phase == Phase::Online) {
      Serial.println("[wifi] connection lost, auto-reconnecting");
      setStatus(Phase::Reconnecting, st.ssid);
    } else if (up && st.phase == Phase::Reconnecting) {
      becameOnline();
    }
  }
}

const Status &status() { return st; }
bool online() { return WiFi.status() == WL_CONNECTED; }

void forget() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  savedSsid = "";
  savedPass = "";
}

bool portalActive() { return portalUp; }
IPAddress apIp() { return AP_IP; }

String scanJson(bool refresh) {
  if (refresh || nets.empty()) startScan();
  String out = "{\"scanning\":";
  out += scanning ? "true" : "false";
  out += ",\"networks\":[";
  for (size_t i = 0; i < nets.size(); i++) {
    if (i) out += ',';
    out += "{\"ssid\":\"" + jsonEscape(nets[i].ssid) + "\",\"rssi\":" + String(nets[i].rssi) +
           ",\"open\":" + (nets[i].open ? "true" : "false") + "}";
  }
  out += "]}";
  return out;
}

void connect(const String &s, const String &p) {
  candSsid = s;
  candPass = p;
  Serial.printf("[wifi] portal: trying \"%s\"\n", s.c_str());
  if (scanning) {  // a running scan would make the join fail
    WiFi.scanDelete();
    scanning = false;
  }
  startJoin(candSsid, candPass, true);
  setStatus(Phase::PortalConnecting, candSsid);
}

String statusJson() {
  const char *s = "idle";
  switch (st.phase) {
    case Phase::PortalConnecting: s = "connecting"; break;
    case Phase::Online: s = "connected"; break;
    case Phase::PortalFailed: s = "failed"; break;
    default: break;
  }
  String out = String("{\"state\":\"") + s + "\",\"ssid\":\"" + jsonEscape(candSsid) + "\"";
  if (st.phase == Phase::PortalFailed) out += ",\"reason\":\"" + jsonEscape(st.detail) + "\"";
  out += "}";
  // The phone now knows it worked: shut the hotspot shortly after instead of waiting out the
  // full linger (which only exists for phones that drop off during the channel switch).
  if (st.phase == Phase::Online && portalUp) {
    uint32_t soon = millis() + PORTAL_SEEN_MS;
    if (!portalCloseAt || (int32_t)(portalCloseAt - soon) > 0) portalCloseAt = soon;
  }
  return out;
}

}  // namespace wifisetup
