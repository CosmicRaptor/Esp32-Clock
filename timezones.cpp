#include "timezones.h"

namespace tz {
namespace {

// POSIX rules from the tz database (2025). Sorted roughly west -> east.
const Zone ZONES[] = {
    {"Pacific/Honolulu", "Honolulu", "HST10"},
    {"America/Anchorage", "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "Phoenix", "MST7"},
    {"America/Denver", "Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "Mexico City", "CST6"},
    {"America/New_York", "New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Bogota", "Bogota", "<-05>5"},
    {"America/Lima", "Lima", "<-05>5"},
    {"America/Halifax", "Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/Santiago", "Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
    {"America/St_Johns", "St. John's", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo", "Sao Paulo", "<-03>3"},
    {"America/Argentina/Buenos_Aires", "Buenos Aires", "<-03>3"},
    {"Atlantic/Azores", "Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
    {"UTC", "UTC", "UTC0"},
    {"Europe/London", "London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
    {"Europe/Lisbon", "Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Africa/Lagos", "Lagos", "WAT-1"},
    {"Europe/Paris", "Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Africa/Johannesburg", "Johannesburg", "SAST-2"},
    {"Africa/Cairo", "Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Europe/Athens", "Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv", "Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Asia/Jerusalem", "Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Europe/Istanbul", "Istanbul", "<+03>-3"},
    {"Europe/Moscow", "Moscow", "MSK-3"},
    {"Asia/Riyadh", "Riyadh", "<+03>-3"},
    {"Africa/Nairobi", "Nairobi", "EAT-3"},
    {"Asia/Tehran", "Tehran", "<+0330>-3:30"},
    {"Asia/Dubai", "Dubai", "<+04>-4"},
    {"Asia/Karachi", "Karachi", "PKT-5"},
    {"Asia/Kolkata", "India", "IST-5:30"},
    {"Asia/Kathmandu", "Kathmandu", "<+0545>-5:45"},
    {"Asia/Dhaka", "Dhaka", "<+06>-6"},
    {"Asia/Yangon", "Yangon", "<+0630>-6:30"},
    {"Asia/Bangkok", "Bangkok", "<+07>-7"},
    {"Asia/Jakarta", "Jakarta", "WIB-7"},
    {"Asia/Ho_Chi_Minh", "Ho Chi Minh City", "<+07>-7"},
    {"Asia/Singapore", "Singapore", "<+08>-8"},
    {"Asia/Kuala_Lumpur", "Kuala Lumpur", "<+08>-8"},
    {"Asia/Manila", "Manila", "PST-8"},
    {"Asia/Shanghai", "China", "CST-8"},
    {"Asia/Hong_Kong", "Hong Kong", "HKT-8"},
    {"Asia/Taipei", "Taipei", "CST-8"},
    {"Australia/Perth", "Perth", "AWST-8"},
    {"Asia/Tokyo", "Tokyo", "JST-9"},
    {"Asia/Seoul", "Seoul", "KST-9"},
    {"Australia/Darwin", "Darwin", "ACST-9:30"},
    {"Australia/Adelaide", "Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "Brisbane", "AEST-10"},
    {"Australia/Sydney", "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Guam", "Guam", "ChST-10"},
    {"Pacific/Noumea", "Noumea", "<+11>-11"},
    {"Pacific/Fiji", "Fiji", "<+12>-12"},
    {"Pacific/Auckland", "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

}  // namespace

const Zone *find(const String &id) {
  for (const Zone &z : ZONES)
    if (id == z.id) return &z;
  return nullptr;
}

String listJson() {
  String out = "[";
  bool first = true;
  for (const Zone &z : ZONES) {
    if (!first) out += ',';
    first = false;
    out += "{\"id\":\"";
    out += z.id;
    out += "\",\"city\":\"";
    out += z.city;
    out += "\"}";
  }
  out += "]";
  return out;
}

}  // namespace tz
