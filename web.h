#pragma once

// HTTP server (port 80) for both modes, serving one page:
//  - setup mode (captive portal on the Bhoomi-Clock hotspot): Wi-Fi picker + time zone
//  - settings mode (home network, http://bhoomi-clock.local): time zone
namespace web {

constexpr const char *HOSTNAME = "bhoomi-clock";  // -> bhoomi-clock.local

void begin();
void onOnline();  // (re)announce mDNS once we have an address on the home network
void loop();

}  // namespace web
