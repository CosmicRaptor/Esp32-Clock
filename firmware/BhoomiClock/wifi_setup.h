#pragma once
#include <Arduino.h>

// Wi-Fi provisioning via a captive portal. Fully non-blocking: call loop() often.
// HTTP is served by web.cpp, which calls into the portal functions below.
//
// Boot:   saved credentials? -> try to join (20 s). Success -> normal STA mode.
//         no credentials / join failed -> open an access point + captive DNS.
// Portal: phone joins the AP, the OS pops the setup page, user picks a network.
//         Credentials are only saved after a successful join; the AP then shuts down.
//         If saved credentials exist, they are retried in the background every minute
//         (e.g. the router was just slow to come back after a power cut).
// Reset:  hold BOOT for 5 s at any time -> forget Wi-Fi and reboot into the portal.

namespace wifisetup {

enum class Phase {
  JoiningSaved,      // power-on, trying the stored network        (ssid = stored network)
  Portal,            // setup AP is up, waiting for the phone      (ssid = AP name)
  PortalConnecting,  // user submitted a network, trying it        (ssid = that network)
  PortalFailed,      // that attempt failed                        (ssid, detail = why)
  Online,            // connected                                  (ssid, detail = IP)
  Reconnecting,      // was online, link dropped, auto-retrying    (ssid)
};

struct Status {
  Phase phase;
  String ssid;
  String detail;
};

using Listener = void (*)(const Status &);

void begin(const char *apName, Listener onChange);
void loop();
const Status &status();
bool online();
void forget();  // erase stored credentials

// --- used by the web server while the portal is up ---
bool portalActive();
IPAddress apIp();
String scanJson(bool refresh);           // {"scanning":bool,"networks":[{ssid,rssi,open}]}
void connect(const String &ssid, const String &pass);
String statusJson();                     // {"state":"idle|connecting|connected|failed",...}

String jsonEscape(const String &s);

}  // namespace wifisetup
