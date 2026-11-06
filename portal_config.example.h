// Where the clock finds the admin portal, and the secret it proves itself with.
//
// Copy this file to portal_config.h (gitignored) and fill it in. Without portal_config.h the sketch
// still builds, but the portal client stays off ("not configured" in the serial log).
//
//   PORTAL_URL           base URL, no trailing slash.
//                          https://clock-portal.<you>.workers.dev   (real deployment; TLS is checked
//                                                                    against the built-in CA bundle)
//                          http://192.168.1.20:8787                 (`npm run dev` on your Mac, LAN only;
//                                                                    the deployed portal is HTTPS only)
//   PORTAL_DEVICE_TOKEN  must equal the portal's DEVICE_TOKEN secret (portal/.dev.vars locally).
//   PORTAL_ACCESS_CLIENT_ID / PORTAL_ACCESS_CLIENT_SECRET
//                        the Cloudflare Access service token the deployed portal sits behind (Zero
//                        Trust, Access, Service credentials). Sent as CF-Access-Client-Id/-Secret.
//                        Leave empty for `npm run dev`.
#pragma once

#define PORTAL_URL ""
#define PORTAL_DEVICE_TOKEN ""
#define PORTAL_ACCESS_CLIENT_ID ""
#define PORTAL_ACCESS_CLIENT_SECRET ""
