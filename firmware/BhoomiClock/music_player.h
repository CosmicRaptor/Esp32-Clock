#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace music {

constexpr uint32_t DEFAULT_ACCENT = 0x739CB3;
constexpr uint32_t MAX_SAMPLE_ADVANCE_MS = 45000;

// Pick the strongest hue family among sampled RGB565 pixels. Ignore neutral and
// near-black pixels so a dark border or white lettering does not set the tint.
inline uint32_t coverAccent(const uint16_t *pixels, size_t count) {
  struct Hue { unsigned weight, red, green, blue; } hues[12] = {};
  const size_t step = std::max(size_t(1), (count + 1023) / 1024);
  unsigned sampled = 0, colorful = 0;
  for (size_t i = 0; i < count; i += step) {
    ++sampled;
    const uint16_t pixel = pixels[i];
    const int r = (pixel >> 11) * 255 / 31;
    const int g = ((pixel >> 5) & 63) * 255 / 63;
    const int b = (pixel & 31) * 255 / 31;
    const int peak = std::max({r, g, b});
    const int spread = peak - std::min({r, g, b});
    if (peak < 48 || spread < 28) continue;
    ++colorful;
    const int hue = peak == r ? 60 * (g - b) / spread
                  : peak == g ? 120 + 60 * (b - r) / spread
                              : 240 + 60 * (r - g) / spread;
    Hue &bin = hues[((hue + 375) % 360) / 30];
    bin.weight += spread;
    bin.red += r * spread;
    bin.green += g * spread;
    bin.blue += b * spread;
  }
  if (!colorful || colorful * 12 < sampled) return DEFAULT_ACCENT;
  const auto *best = std::max_element(hues, hues + 12, [](const Hue &a, const Hue &b) { return a.weight < b.weight; });
  return ((best->red / best->weight) << 16) | ((best->green / best->weight) << 8) | (best->blue / best->weight);
}

inline uint32_t elapsedMs(uint32_t position, uint32_t duration, uint32_t sampleAge) {
  return std::min<uint64_t>(duration, uint64_t(position) + std::min(sampleAge, MAX_SAMPLE_ADVANCE_MS));
}

}  // namespace music
