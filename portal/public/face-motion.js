// The motion maths of a clock face, written once. The Faces page preview draws with it; the clock's renderer will
// implement the same formulas (they are spelled out in proto/clock.proto and portal/README.md, and the numbers in
// tests/js/face-motion.test.js are the ones to check a port against).
//
// Everything is a pure function of the time `t` in milliseconds since the face was shown. No rotation: LVGL would
// rotate in software, which is slow. Use a "frames" sprite sheet instead.
//
// Works as a plain <script> (window.FaceMotion) and as a CommonJS module (for node --test).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.FaceMotion = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const TAU = Math.PI * 2;
  const round = v => Math.floor(v + 0.5);                  // half up, like (int)(v + 0.5f) on the clock for v >= 0
  const mod = (a, n) => ((a % n) + n) % n;                  // always in [0, n), also for negative a
  const wave = u => 0.5 - 0.5 * Math.cos(TAU * u);          // 0 -> 1 -> 0 over u in [0, 1)

  // ---- PRNG: xorshift32, seeded per particle by an integer hash. Pure 32-bit integer maths, so C and JS agree bit
  // for bit: `x ^= x << 13; x ^= x >> 17; x ^= x << 5;` on a uint32_t.
  function mix32(seed, index) {
    let h = (seed ^ Math.imul(index + 1, 0x9E3779B1)) >>> 0;
    h = Math.imul(h ^ (h >>> 16), 0x85EBCA6B) >>> 0;
    h = Math.imul(h ^ (h >>> 13), 0xC2B2AE35) >>> 0;
    h = (h ^ (h >>> 16)) >>> 0;
    return h === 0 ? 1 : h;       // xorshift must not start at 0
  }
  function xorshift32(s) {
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    return s;
  }
  // The first `n` 16-bit draws (the top half of each xorshift output) for particle `index` of an emitter with `seed`.
  function draws(seed, index, n) {
    let s = mix32(seed >>> 0, index);
    const out = [];
    for (let i = 0; i < n; i++) { s = xorshift32(s); out.push(s >>> 16); }
    return out;
  }

  // ---- motions on a layer ----

  // Linear interpolation through the keyframes [{at: permille, v: percent}] at permille `x`, wrapping from the last
  // key back round to the first one at 1000.
  function fadeAt(keys, x) {
    const last = keys[keys.length - 1];
    if (x >= last.at) {
      const span = 1000 - last.at;
      return span ? last.v + (keys[0].v - last.v) * (x - last.at) / span : last.v;
    }
    let j = 0;
    while (keys[j + 1].at <= x) j++;
    const a = keys[j], b = keys[j + 1];
    return a.v + (b.v - a.v) * (x - a.at) / (b.at - a.at);
  }

  // A scattered layer's copy `copy` has three draws of its own: r[0], r[1] pick its place in the area, r[2] (ms) is how
  // far into their cycles its motions are. The same PRNG as the particles', seeded with the layer's scatter seed.
  function scatterDraws(layer, copy, t = 0) {
    const cycle = layer.scatter.period ? Math.floor(Math.max(0,t) / layer.scatter.period) : 0;
    return draws((layer.scatter.seed ^ Math.imul(cycle, 0x9e3779b1)) >>> 0, copy, 3);
  }

  // Where copy `copy` of a layer sits before its motions: in a row (x, y plus copy * stagger), or, scattered, at a
  // random place so that the whole frame (fw x fh) lies inside the area. Floats: round at the very end.
  function copyOrigin(layer, copy, fw, fh, t = 0) {
    if (layer.scatter) {
      const a = layer.scatter.area, r = scatterDraws(layer, copy, t);
      return { x: a.x + r[0] / 65536 * (a.w - fw), y: a.y + r[1] / 65536 * (a.h - fh) };
    }
    return { x: layer.x + copy * layer.stagger.x, y: layer.y + copy * layer.stagger.y };
  }

  // How a layer's copy `copy` looks at time t. Returns an offset from its place, an opacity (0-100), a scale in
  // percent about the sprite's centre and, for a sprite sheet, the frame number.
  function layerPose(layer, copy, t) {
    const ti = t + (layer.scatter ? (layer.scatter.period ? 0 : scatterDraws(layer, copy)[2]) : copy * ((layer.stagger && layer.stagger.phase) || 0));
    const pose = { dx: 0, dy: 0, opacity: 100, scale: layer.scale || 100, frame: 0 };
    for (const m of layer.motion || []) {
      const p = m.period, tm = ti + (m.phase || 0);
      const u = mod(tm, p) / p;
      switch (m.type) {
        case 'bob': pose.dy += m.amp * Math.sin(TAU * u); break;
        case 'sway': pose.dx += m.amp * Math.sin(TAU * u); break;
        case 'path': {
          const w = m.mode === 'loop' ? u : wave(u);
          pose.dx += m.dx * w; pose.dy += m.dy * w;
          break;
        }
        case 'fade': pose.opacity = pose.opacity * fadeAt(m.keys, u * 1000) / 100; break;
        case 'blink': if (mod(tm, p) >= m.on) pose.opacity = 0; break;
        case 'pulse': pose.scale = pose.scale * (m.min + (m.max - m.min) * wave(u)) / 100; break;
        case 'frames': pose.frame = mod(Math.floor(tm * m.fps / 1000), m.frames); break;
      }
    }
    return pose;
  }

  // Width of one frame of a layer's sprite (the whole sprite unless it has a frames motion).
  function frameCount(layer) {
    const m = (layer.motion || []).find(x => x.type === 'frames');
    return m ? m.frames : 1;
  }

  // Where to draw copy `copy` of a layer: the sprite's source rectangle and the destination rectangle, in screen
  // pixels (integers). `sprite` = {w, h} of the whole image.
  function layerPlacement(layer, sprite, copy, t) {
    const pose = layerPose(layer, copy, t);
    const n = frameCount(layer), fw = sprite.w / n, fh = sprite.h;
    const o = copyOrigin(layer, copy, fw, fh, t);
    const cx = o.x + pose.dx + fw / 2;
    const cy = o.y + pose.dy + fh / 2;
    const w = round(fw * pose.scale / 100), h = round(fh * pose.scale / 100);
    return {
      sx: pose.frame * fw, sy: 0, sw: fw, sh: fh,
      x: round(cx - w / 2), y: round(cy - h / 2), w, h,
      opacity: pose.opacity,
    };
  }

  // ---- particles ----

  // Per-particle constants from the seeded PRNG: six 16-bit draws r1..r6, in this order.
  function particleParams(em, i) {
    const r = draws(em.seed, i, 6);
    return {
      startFrac: r[0] / 65536,                       // where along its path it starts, or (twinkle) its y position
      xFrac: r[1] / 65536,                           // where across the area
      speed: em.speed_min + (em.speed_max - em.speed_min) * r[2] / 65536,    // px per second
      swayTurn: r[3] / 65536,                        // sway / twinkle phase, in turns
      period: em.period * (0.75 + 0.5 * r[4] / 65536),
      sprite: r[5] % em.sprites.length,              // index into em.sprites
    };
  }

  // Particle i of emitter `em` at time t. `sizes[k]` = {w, h} of em.sprites[k]. Returns the sprite index, the top-left
  // corner (integers; the area clips what sticks out) and an opacity (0-100).
  function particle(em, i, t, sizes) {
    const q = particleParams(em, i);
    const sz = sizes[q.sprite], a = em.area;
    const T = t + (em.phase || 0);
    const x0 = a.x + q.xFrac * (a.w - sz.w);
    if (em.kind === 'twinkle') {
      return {
        sprite: q.sprite, x: round(x0), y: round(a.y + q.startFrac * (a.h - sz.h)),
        opacity: 100 * wave(T / q.period + q.swayTurn - Math.floor(T / q.period + q.swayTurn)),
      };
    }
    const travel = a.h + sz.h;
    const d = mod(q.speed * T / 1000 + q.startFrac * travel, travel);
    const x = x0 + em.sway * Math.sin(TAU * (T / q.period + q.swayTurn));
    const y = em.kind === 'rise' ? a.y + a.h - d : a.y - sz.h + d;
    return { sprite: q.sprite, x: round(x), y: round(y), opacity: 100 };
  }

  return { TAU, round, mod, wave, mix32, xorshift32, draws, fadeAt, layerPose, copyOrigin, frameCount, layerPlacement, particleParams, particle };
});
