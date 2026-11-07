// Reads cases from stdin, one per line, and prints 1 or 0 for each: whether the `when` matches.
//   year month day hour weekday | hour_from hour_to months_mask days_mask n_dates { month day year }*
#include <stdio.h>

#include "../firmware/BhoomiClock/face_rules.h"

int main() {
  int year, month, day, hour, wday;
  while (scanf("%d %d %d %d %d", &year, &month, &day, &hour, &wday) == 5) {
    bhoomi_clock_v1_When w = {};
    unsigned hf, ht, mm, dm, n;
    if (scanf(" %u %u %u %u %u", &hf, &ht, &mm, &dm, &n) != 5) return 2;
    w.hour_from = hf; w.hour_to = ht; w.months = mm; w.days = dm; w.dates_count = n;
    for (unsigned i = 0; i < n; i++) {
      unsigned dmo, dd, dy;
      if (scanf(" %u %u %u", &dmo, &dd, &dy) != 3) return 2;
      w.dates[i].month = dmo; w.dates[i].day = dd; w.dates[i].year = dy;
    }
    struct tm t = {};
    t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day; t.tm_hour = hour; t.tm_wday = wday;
    printf("%d\n", facerules::matches(w, t) ? 1 : 0);
  }
}
