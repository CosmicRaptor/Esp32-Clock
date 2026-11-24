#pragma once
#include <stdint.h>
#include <time.h>

namespace dailyrestart {

inline uint32_t dateKey(const struct tm &local) {
  return (local.tm_year + 1900) * 10000u + (local.tm_mon + 1) * 100u + local.tm_mday;
}

// Check the same local day for up to an hour, allowing a busy clock to finish.
inline bool due(uint32_t scheduledMinute, const struct tm &local, uint32_t lastDate) {
  const uint32_t minute = local.tm_hour * 60u + local.tm_min;
  return scheduledMinute < 1440 && minute >= scheduledMinute && minute - scheduledMinute < 60 &&
         dateKey(local) > lastDate;
}

}  // namespace dailyrestart
