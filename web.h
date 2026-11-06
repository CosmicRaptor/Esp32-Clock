#pragma once

// HTTP server (port 80), running in its own task, serving one page:
//  - setup mode (captive portal on the Bhoomi-Clock hotspot): Wi-Fi picker + time zone
//  - settings mode (home network, http://bhoomi-clock.local): time zone + photos/sounds on the SD card
//    + the cloud connection's status and keys (device token, Cloudflare Access service token)
namespace web {

constexpr const char *HOSTNAME = "bhoomi-clock";  // -> bhoomi-clock.local

void begin();     // starts the server task; call after Wi-Fi is initialised
void onOnline();  // announce mDNS once we have an address on the home network

}  // namespace web
