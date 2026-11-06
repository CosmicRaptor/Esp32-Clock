#pragma once
// The motion maths of a clock face: the same formulas as portal/public/face-motion.js (documented in
// portal/README.md, "Motions" and "Particles"). Pure functions of the time `t` in milliseconds since the face was
// shown, with no LVGL in here, so tools/check_face_motion.sh can run them on a computer and compare with the JS.
#include <math.h>
#include <stdint.h>

#include "clock.pb.h"

namespace facemotion {

using Layer = bhoomi_clock_v1_Layer;
using Motion = bhoomi_clock_v1_Motion;
using Emitter = bhoomi_clock_v1_Emitter;

constexpr double TAU = 6.283185307179586;

inline int64_t pmod(int64_t a, int64_t n) {  // never negative, also for a negative a
  int64_t r = a % n;
  return r < 0 ? r + n : r;
}
inline int64_t floorDiv(int64_t a, int64_t n) { return (a - pmod(a, n)) / n; }
inline int roundHalfUp(double v) { return (int)floor(v + 0.5); }
inline double wave(double u) { return 0.5 - 0.5 * cos(TAU * u); }  // 0 -> 1 -> 0 over u in [0, 1)

// ---- particles: a seeded xorshift32, all 32-bit integer maths ----

inline uint32_t mix32(uint32_t seed, uint32_t index) {
  uint32_t h = seed ^ ((index + 1) * 0x9E3779B1u);
  h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
  h = (h ^ (h >> 13)) * 0xC2B2AE35u;
  h = h ^ (h >> 16);
  return h ? h : 1;
}
inline uint32_t xorshift32(uint32_t s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
// The six 16-bit draws r1..r6 of particle `index`.
inline void draws(uint32_t seed, uint32_t index, uint16_t r[6]) {
  uint32_t s = mix32(seed, index);
  for (int i = 0; i < 6; i++) {
    s = xorshift32(s);
    r[i] = (uint16_t)(s >> 16);
  }
}

// A scattered layer's copy has three draws of its own: r[0], r[1] pick its place in the area, r[2] (ms) is how far into
// their cycles its motions are (the same PRNG as the particles', seeded with scatter_seed).
inline int64_t copyPhase(const Layer &L, int copy) {
  if (L.scatter) {
    uint16_t r[6];
    draws(L.scatter_seed, (uint32_t)copy, r);
    return r[2];
  }
  return (int64_t)copy * L.stagger_phase_ms;
}

// Where copy `copy` sits before its motions: in a row, or at a random place with the whole frame inside the area.
inline void copyOrigin(const Layer &L, int copy, int fw, int fh, double &x, double &y) {
  if (L.scatter) {
    uint16_t r[6];
    draws(L.scatter_seed, (uint32_t)copy, r);
    x = L.scatter_area.x + r[0] / 65536.0 * ((double)L.scatter_area.w - fw);
    y = L.scatter_area.y + r[1] / 65536.0 * ((double)L.scatter_area.h - fh);
    return;
  }
  x = L.x + (double)copy * L.stagger_x;
  y = L.y + (double)copy * L.stagger_y;
}

// How a layer's copy looks at time t: an offset from its place, an opacity (0-100), a scale in percent about the
// sprite's centre and, for a sprite sheet, the frame number. Offsets add, opacities and scales multiply.
struct Pose {
  double dx = 0, dy = 0, opacity = 100, scale = 100;
  int frame = 0;
};

inline double fadeAt(const Motion &m, double x) {  // x: permille of the period
  const auto &k = m.keys;
  int n = m.keys_count;
  if (n < 2) return n ? k[0].pct : 100;
  const auto &last = k[n - 1];
  if (x >= last.at) {
    double span = 1000.0 - last.at;
    return span > 0 ? last.pct + ((double)k[0].pct - last.pct) * (x - last.at) / span : last.pct;
  }
  int j = 0;
  while (j + 1 < n - 1 && k[j + 1].at <= x) j++;
  double span = (double)k[j + 1].at - k[j].at;
  return span > 0 ? k[j].pct + ((double)k[j + 1].pct - k[j].pct) * (x - k[j].at) / span : k[j].pct;
}

inline Pose layerPose(const Layer &L, int copy, uint32_t t) {
  int64_t ti = (int64_t)t + copyPhase(L, copy);
  Pose p;
  p.scale = L.scale_pct ? L.scale_pct : 100;
  for (pb_size_t i = 0; i < L.motion_count; i++) {
    const Motion &m = L.motion[i];
    if (m.type == bhoomi_clock_v1_Motion_Type_NONE) continue;
    int64_t tm = ti + m.phase_ms;
    if (m.type == bhoomi_clock_v1_Motion_Type_FRAMES) {
      if (m.frames) p.frame = (int)pmod(floorDiv(tm * (int64_t)m.fps, 1000), m.frames);
      continue;
    }
    if (!m.period_ms) continue;
    int64_t into = pmod(tm, m.period_ms);
    double u = (double)into / m.period_ms;
    switch (m.type) {
      case bhoomi_clock_v1_Motion_Type_BOB: p.dy += m.amp * sin(TAU * u); break;
      case bhoomi_clock_v1_Motion_Type_SWAY: p.dx += m.amp * sin(TAU * u); break;
      case bhoomi_clock_v1_Motion_Type_PATH: {
        double w = m.loop ? u : wave(u);
        p.dx += m.dx * w;
        p.dy += m.dy * w;
        break;
      }
      case bhoomi_clock_v1_Motion_Type_FADE: p.opacity = p.opacity * fadeAt(m, u * 1000) / 100; break;
      case bhoomi_clock_v1_Motion_Type_BLINK:
        if (into >= (int64_t)m.on_ms) p.opacity = 0;
        break;
      case bhoomi_clock_v1_Motion_Type_PULSE:
        p.scale = p.scale * (m.min_pct + ((double)m.max_pct - m.min_pct) * wave(u)) / 100;
        break;
      default: break;
    }
  }
  return p;
}

inline int frameCount(const Layer &L) {
  for (pb_size_t i = 0; i < L.motion_count; i++)
    if (L.motion[i].type == bhoomi_clock_v1_Motion_Type_FRAMES && L.motion[i].frames) return (int)L.motion[i].frames;
  return 1;
}

struct ParticleParams {
  double startFrac, xFrac, speed, swayTurn, period;
  int sprite;
};

inline ParticleParams particleParams(const Emitter &E, int i) {
  uint16_t r[6];
  draws(E.seed, (uint32_t)i, r);
  ParticleParams q;
  q.startFrac = r[0] / 65536.0;
  q.xFrac = r[1] / 65536.0;
  q.speed = E.speed_min + ((double)E.speed_max - (double)E.speed_min) * r[2] / 65536.0;
  q.swayTurn = r[3] / 65536.0;
  q.period = E.period_ms * (0.75 + 0.5 * r[4] / 65536.0);
  q.sprite = E.sprites_count ? r[5] % E.sprites_count : 0;
  return q;
}

struct ParticlePos {
  int x, y;  // top-left, in screen pixels (the emitter's area clips what sticks out)
  double opacity;
};

// sw, sh: the size of the particle's sprite.
inline ParticlePos particlePos(const Emitter &E, const ParticleParams &q, int sw, int sh, uint32_t t) {
  const auto &a = E.area;
  double T = (double)t + E.phase_ms;
  double x0 = a.x + q.xFrac * ((double)a.w - sw);
  ParticlePos out;
  if (E.kind == bhoomi_clock_v1_Emitter_Kind_TWINKLE) {
    double v = q.period > 0 ? T / q.period + q.swayTurn : 0;
    v -= floor(v);
    out = {roundHalfUp(x0), roundHalfUp(a.y + q.startFrac * ((double)a.h - sh)), 100 * wave(v)};
    return out;
  }
  double travel = (double)a.h + sh;
  double d = fmod(q.speed * T / 1000.0 + q.startFrac * travel, travel);
  if (d < 0) d += travel;
  double x = x0 + (q.period > 0 ? E.sway * sin(TAU * (T / q.period + q.swayTurn)) : 0);
  double y = E.kind == bhoomi_clock_v1_Emitter_Kind_RISE ? a.y + a.h - d : a.y - sh + d;
  out = {roundHalfUp(x), roundHalfUp(y), 100};
  return out;
}

}  // namespace facemotion
