#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace otasign {
// Canonical three-component versions only; bound arithmetic on untrusted metadata.
inline bool version(const char *s, uint32_t out[3]) {
  for (int i = 0; i < 3; ++i) {
    if (*s < '0' || *s > '9' || (*s == '0' && s[1] >= '0' && s[1] <= '9')) return false;
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
      unsigned digit = *s++ - '0';
      if (n > (UINT32_MAX - digit) / 10) return false;
      n = n * 10 + digit;
    }
    out[i] = n;
    if (i < 2) { if (*s++ != '.') return false; }
  }
  return *s == 0;
}
inline bool newer(const char *candidate, const char *floor) {
  uint32_t a[3], b[3];
  if (!version(candidate, a) || !version(floor, b)) return false;
  for (int i = 0; i < 3; ++i) if (a[i] != b[i]) return a[i] > b[i];
  return false;
}
inline bool unhex(const char *s, uint8_t *out, size_t capacity, size_t &length) {
  size_t n = strlen(s);
  if (!n || n % 2 || n / 2 > capacity) return false;
  length = n / 2;
  for (size_t i = 0; i < n; ++i) {
    char c = s[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    if (i % 2) out[i / 2] |= v; else out[i / 2] = v << 4;
  }
  return true;
}
inline int manifest(char *out, size_t capacity, const char *v, const char *digest, uint32_t size) {
  return snprintf(out, capacity, "bhoomi-clock-ota-v1\n%s\n%s\n%lu\n", v, digest, (unsigned long)size);
}
}
