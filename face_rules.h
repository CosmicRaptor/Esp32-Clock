#pragma once
// Which face shows when: the `when` rules of a face (portal/README.md, "Faces"). The same logic as
// portal/public/face-rules.js and portal/src/model.py; tools/check_face_rules.sh runs the shared test vectors
// (portal/tests/vectors/face_rules.json) through this one.
#include <stdint.h>
#include <time.h>

#include "clock.pb.h"

namespace facerules {

// Everything that is set must hold; nothing set at all never matches. `t` is the clock's local time.
inline bool matches(const bhoomi_clock_v1_When &w, const struct tm &t) {
  if (!w.hour_to && !w.months && !w.days && !w.dates_count) return false;
  if (w.hour_to) {
    bool in = w.hour_from < w.hour_to ? (uint32_t)t.tm_hour >= w.hour_from && (uint32_t)t.tm_hour < w.hour_to
                                      : (uint32_t)t.tm_hour >= w.hour_from || (uint32_t)t.tm_hour < w.hour_to;
    if (!in) return false;
  }
  if (w.months && !(w.months & (1u << t.tm_mon))) return false;
  if (w.days && !(w.days & (1u << t.tm_wday))) return false;
  if (w.dates_count) {
    bool any = false;
    for (pb_size_t i = 0; i < w.dates_count; i++) {
      const auto &d = w.dates[i];
      any = any || (d.month == (uint32_t)t.tm_mon + 1 && d.day == (uint32_t)t.tm_mday &&
                    (d.year == 0 || d.year == (uint32_t)t.tm_year + 1900));
    }
    if (!any) return false;
  }
  return true;
}

}  // namespace facerules
