#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The disk cache uses a SHA-256 envelope for protobufs. Artwork is content-addressed by
// the first 16 hexadecimal characters of its SHA-256, as in portal/src/model.py.
namespace facecache {
inline bool digestMatchesHex(const uint8_t digest[32], const char *hex, size_t chars) {
  if (!hex || chars > 64 || strlen(hex) != chars) return false;
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < chars; ++i)
    if (hex[i] != digits[(i & 1) ? (digest[i/2] & 15) : (digest[i/2] >> 4)]) return false;
  return true;
}
inline bool digestMatches(const uint8_t a[32], const uint8_t b[32]) { return memcmp(a, b, 32) == 0; }
inline bool validAssetShape(const uint8_t *data, size_t len, uint32_t expectedSize,
                            uint32_t width, uint32_t height, bool background) {
  if (!data || len < 12 || len != expectedSize) return false;
  if (background) return data[0] == 0xff && data[1] == 0xd8;
  uint32_t w = data[4] | (uint32_t(data[5]) << 8), h = data[6] | (uint32_t(data[7]) << 8);
  return data[0] == 0x19 && data[1] == 0x14 && w == width && h == height &&
         uint64_t(len) == 12 + uint64_t(3) * w * h;
}
}

namespace facecache {
// The caller validates the staged data first. On failed promotion, preserve/restore the old file.
template <typename FS, typename Path>
bool promote(FS &fs, const Path &path, const Path &staged, const Path &backup) {
  if (!fs.exists(path) && fs.exists(backup) && !fs.rename(backup, path)) return false;
  if (fs.exists(backup) && !fs.remove(backup)) return false;
  const bool hadOld = fs.exists(path);
  if (hadOld && !fs.rename(path, backup)) return false;
  if (!fs.rename(staged, path)) {
    if (hadOld) fs.rename(backup, path); // if this fails, the backup is recovered on next boot
    return false;
  }
  if (hadOld) fs.remove(backup);
  return true;
}
}
