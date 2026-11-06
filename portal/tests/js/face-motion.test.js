'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const M = require('../../public/face-motion.js');

const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) < eps, `${a} is not ${b}`);
const layer = (motion, extra = {}) => ({ x: 0, y: 0, stagger: { x: 0, y: 0, phase: 0 }, motion, ...extra });

test('bob: a sine in y, starting at the middle', () => {
  const l = layer([{ type: 'bob', period: 2000, phase: 0, amp: 4 }]);
  near(M.layerPose(l, 0, 0).dy, 0);
  near(M.layerPose(l, 0, 500).dy, 4);
  near(M.layerPose(l, 0, 1000).dy, 0);
  near(M.layerPose(l, 0, 1500).dy, -4);
  near(M.layerPose(l, 0, 2500).dy, 4);     // periodic
  near(M.layerPose(l, 0, 500).dx, 0);
});

test('sway moves x, and the phase shifts it in time', () => {
  const l = layer([{ type: 'sway', period: 1000, phase: 250, amp: -6 }]);
  near(M.layerPose(l, 0, 0).dx, -6);       // sin(2*pi*0.25) = 1
  near(M.layerPose(l, 0, 0).dy, 0);
});

test('path: ping-pong follows a cosine ease, loop is linear and jumps back', () => {
  const pp = layer([{ type: 'path', period: 1000, phase: 0, dx: 40, dy: -20, mode: 'pingpong' }]);
  near(M.layerPose(pp, 0, 0).dx, 0);
  near(M.layerPose(pp, 0, 250).dx, 20);
  near(M.layerPose(pp, 0, 250).dy, -10);
  near(M.layerPose(pp, 0, 500).dx, 40);
  near(M.layerPose(pp, 0, 750).dx, 20);
  const lp = layer([{ type: 'path', period: 1000, phase: 0, dx: 40, dy: 0, mode: 'loop' }]);
  near(M.layerPose(lp, 0, 250).dx, 10);
  near(M.layerPose(lp, 0, 999).dx, 39.96);
  near(M.layerPose(lp, 0, 1000).dx, 0);
});

test('fade: linear between keyframes, wrapping from the last back to the first', () => {
  const l = layer([{ type: 'fade', period: 1000, phase: 0, keys: [{ at: 0, v: 100 }, { at: 500, v: 20 }] }]);
  near(M.layerPose(l, 0, 0).opacity, 100);
  near(M.layerPose(l, 0, 250).opacity, 60);
  near(M.layerPose(l, 0, 500).opacity, 20);
  near(M.layerPose(l, 0, 750).opacity, 60);       // back up to 100 over the second half
  const three = [{ at: 0, v: 0 }, { at: 200, v: 100 }, { at: 800, v: 100 }];
  near(M.fadeAt(three, 100), 50);
  near(M.fadeAt(three, 500), 100);
  near(M.fadeAt(three, 900), 50);                 // 800 -> 1000 goes from 100 down to the first key's 0
  near(M.fadeAt([{ at: 0, v: 30 }, { at: 999, v: 80 }], 1000), 30);       // at the very end it is back at the first key
});

test('blink: visible for the first `on` ms of each period', () => {
  const l = layer([{ type: 'blink', period: 1000, phase: 0, on: 150 }]);
  assert.equal(M.layerPose(l, 0, 0).opacity, 100);
  assert.equal(M.layerPose(l, 0, 149).opacity, 100);
  assert.equal(M.layerPose(l, 0, 150).opacity, 0);
  assert.equal(M.layerPose(l, 0, 999).opacity, 0);
  assert.equal(M.layerPose(l, 0, 1100).opacity, 100);
  const shifted = layer([{ type: 'blink', period: 1000, phase: 900, on: 150 }]);   // starts 900 ms into its cycle
  assert.equal(M.layerPose(shifted, 0, 0).opacity, 0);
  assert.equal(M.layerPose(shifted, 0, 100).opacity, 100);
});

test('pulse: scale from min to max about the centre', () => {
  const l = layer([{ type: 'pulse', period: 1000, phase: 0, min: 90, max: 110 }]);
  near(M.layerPose(l, 0, 0).scale, 90);
  near(M.layerPose(l, 0, 250).scale, 100);
  near(M.layerPose(l, 0, 500).scale, 110);
});

test('frames: floor(t * fps / 1000) mod frames', () => {
  const l = layer([{ type: 'frames', period: 667, phase: 0, frames: 4, fps: 6 }]);
  assert.equal(M.layerPose(l, 0, 0).frame, 0);
  assert.equal(M.layerPose(l, 0, 166).frame, 0);
  assert.equal(M.layerPose(l, 0, 167).frame, 1);
  assert.equal(M.layerPose(l, 0, 1000).frame, 2);        // 6 mod 4
  assert.equal(M.layerPose(l, 0, 2000).frame, 0);        // 12 mod 4
});

test('stacked motions: offsets add, opacities multiply, scales multiply', () => {
  const l = layer([
    { type: 'bob', period: 2000, phase: 0, amp: 4 }, { type: 'bob', period: 2000, phase: 0, amp: 3 },
    { type: 'fade', period: 1000, phase: 0, keys: [{ at: 0, v: 50 }, { at: 500, v: 50 }] },
    { type: 'fade', period: 1000, phase: 0, keys: [{ at: 0, v: 50 }, { at: 500, v: 50 }] },
  ]);
  const p = M.layerPose(l, 0, 500);
  near(p.dy, 7);
  near(p.opacity, 25);
});

test('copies are shifted in time by i * stagger phase (also backwards)', () => {
  const l = layer([{ type: 'bob', period: 2000, phase: 0, amp: 4 }], { stagger: { x: 30, y: 0, phase: 500 } });
  near(M.layerPose(l, 0, 0).dy, 0);
  near(M.layerPose(l, 1, 0).dy, 4);                       // 500 ms ahead
  near(M.layerPose(l, 2, 0).dy, 0);
  const back = layer([{ type: 'bob', period: 2000, phase: 0, amp: 4 }], { stagger: { x: 0, y: 0, phase: -500 } });
  near(M.layerPose(back, 1, 0).dy, -4);                   // a negative time still lands in [0, period)
});

test('placement: scale about the centre, frames pick the cell, copies are offset', () => {
  const l = layer([{ type: 'pulse', period: 1000, phase: 0, min: 150, max: 160 }], { x: 10, y: 10 });
  const p = M.layerPlacement(l, { w: 20, h: 20 }, 0, 0);
  assert.deepEqual([p.x, p.y, p.w, p.h], [5, 5, 30, 30]);
  const sheet = layer([{ type: 'frames', period: 500, phase: 0, frames: 4, fps: 8 }], { x: 100, y: 50 });
  const f = M.layerPlacement(sheet, { w: 80, h: 20 }, 0, 130);       // frame floor(130 * 8 / 1000) = 1
  assert.deepEqual([f.sx, f.sw, f.sh, f.x, f.y, f.w, f.h], [20, 20, 20, 100, 50, 20, 20]);
  const row = layer([], { x: 10, y: 10, stagger: { x: 30, y: 5, phase: 0 } });
  const c = M.layerPlacement(row, { w: 20, h: 20 }, 3, 0);
  assert.deepEqual([c.x, c.y], [100, 25]);
});

test('rounding is half up, also for negative numbers', () => {
  assert.equal(M.round(2.5), 3);
  assert.equal(M.round(-2.5), -2);
  assert.equal(M.round(-2.51), -3);
});

test('PRNG: pinned values (a port must produce the same)', () => {
  // Checked against an independent implementation. mix32(seed, index) -> the xorshift32 start state.
  assert.equal(M.mix32(1, 0), 2980047484);
  assert.equal(M.mix32(1234, 7), 232054465);
  assert.equal(M.mix32(4294967295, 39), 3743230108);
  assert.deepEqual(M.draws(1, 0, 6), [55001, 3692, 11333, 12611, 43924, 54468]);
  assert.deepEqual(M.draws(1, 1, 6), [42619, 60436, 2442, 36018, 17416, 1129]);
  assert.deepEqual(M.draws(1234, 7, 6), [22426, 53730, 11624, 49171, 14600, 33329]);
  assert.deepEqual(M.draws(4294967295, 39, 6), [15324, 24416, 27980, 23327, 7000, 48366]);
});

const snow = (extra = {}) => ({
  kind: 'fall', sprites: [{}], count: 20, area: { x: 20, y: 30, w: 200, h: 150 },
  speed_min: 20, speed_max: 40, sway: 10, period: 4000, phase: 0, seed: 1, ...extra,
});
const sizes = [{ w: 10, h: 10 }];

test('particle params come from the six draws in order', () => {
  const q = M.particleParams(snow({ sprites: [{}, {}, {}] }), 0);
  near(q.startFrac, 55001 / 65536);
  near(q.xFrac, 3692 / 65536);
  near(q.speed, 20 + 20 * 11333 / 65536);
  near(q.swayTurn, 12611 / 65536);
  near(q.period, 4000 * (0.75 + 0.5 * 43924 / 65536));
  assert.equal(q.sprite, 54468 % 3);
});

test('a falling particle: x from the draw plus sway, y down at its speed, wrapping', () => {
  const em = snow();
  const q = M.particleParams(em, 0);
  const travel = 150 + 10;
  for (const t of [0, 1000, 12345]) {
    const d = (((q.speed * t / 1000 + q.startFrac * travel) % travel) + travel) % travel;
    const x = 20 + q.xFrac * (200 - 10) + 10 * Math.sin(2 * Math.PI * (t / q.period + q.swayTurn));
    const p = M.particle(em, 0, t, sizes);
    assert.equal(p.x, M.round(x));
    assert.equal(p.y, M.round(30 - 10 + d));
    assert.equal(p.opacity, 100);
  }
});

test('a rising particle mirrors it', () => {
  const em = snow({ kind: 'rise' });
  const q = M.particleParams(em, 3);
  const d = (q.speed * 2 + q.startFrac * 160) % 160;
  assert.equal(M.particle(em, 3, 2000, sizes).y, M.round(30 + 150 - d));
});

test('particles stay in their column band and travel the whole area', () => {
  const em = snow({ count: 40, sway: 0 });
  for (let i = 0; i < 40; i++) {
    let minY = Infinity, maxY = -Infinity;
    for (let t = 0; t < 40000; t += 50) {
      const p = M.particle(em, i, t, sizes);
      assert.ok(p.x >= 20 && p.x <= 20 + 200 - 10, `x ${p.x}`);
      assert.ok(p.y >= 30 - 10 && p.y <= 30 + 150, `y ${p.y}`);
      minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y);
    }
    assert.ok(minY <= 30 && maxY >= 30 + 150 - 15, `particle ${i} only covered ${minY}..${maxY}`);
  }
});

test('particles are deterministic, differ by seed and index, and the phase shifts them in time', () => {
  const a = M.particle(snow(), 5, 777, sizes), b = M.particle(snow(), 5, 777, sizes);
  assert.deepEqual(a, b);
  assert.notDeepEqual(M.particle(snow({ seed: 2 }), 5, 777, sizes), a);
  assert.notDeepEqual(M.particle(snow(), 6, 777, sizes), a);
  assert.deepEqual(M.particle(snow({ phase: 500 }), 5, 277, sizes), M.particle(snow(), 5, 777, sizes));
});

test('twinkle: fixed position, opacity swells 0 -> 100 -> 0 over its period', () => {
  const em = snow({ kind: 'twinkle', area: { x: 0, y: 0, w: 320, h: 120 } });
  const q = M.particleParams(em, 2);
  const first = M.particle(em, 2, 0, sizes);
  for (let t = 0; t < 20000; t += 137) {
    const p = M.particle(em, 2, t, sizes);
    assert.equal(p.x, first.x);
    assert.equal(p.y, first.y);
    assert.ok(p.opacity >= 0 && p.opacity <= 100);
  }
  // opacity = 100 * (0.5 - 0.5 cos(2 pi (t / period + turn)))
  near(M.particle(em, 2, 0, sizes).opacity, 100 * (0.5 - 0.5 * Math.cos(2 * Math.PI * q.swayTurn)), 1e-9);
  assert.ok(first.x >= 0 && first.x <= 310 && first.y >= 0 && first.y <= 110);
});
