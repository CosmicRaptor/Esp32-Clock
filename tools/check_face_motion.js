// The JS half of tools/check_face_motion.sh: prints the same lines as check_face_motion.cpp, from the portal's module.
const M = require('../portal/public/face-motion.js');
const mo = (type, period, phase = 0, extra = {}) => ({ type, period, phase, ...extra });
const L = { x: 0, y: 0, stagger: { x: 0, y: 0, phase: -250 }, motion: [
  mo('bob', 3200, 100, { amp: 4 }), mo('path', 8000, 0, { dx: -30, dy: 12, mode: 'pingpong' }),
  mo('fade', 2000, 300, { keys: [{ at: 0, v: 100 }, { at: 400, v: 20 }, { at: 800, v: 70 }] })] };
const L2 = { stagger: { x: 0, y: 0, phase: 0 }, motion: [
  mo('sway', 1000, 250, { amp: -6 }), mo('pulse', 1500, 0, { min: 90, max: 110 }), mo('blink', 1000, 900, { on: 150 })] };
const L3 = { stagger: { x: 0, y: 0, phase: 0 }, motion: [mo('frames', 667, 0, { frames: 4, fps: 6 }), mo('path', 1000, 0, { dx: 40, dy: 0, mode: 'loop' })] };
const times = [0, 137, 500, 1234, 2999, 9999, 123456];
const f = (v) => v.toFixed(3);
[L, L2, L3].forEach((l, li) => {
  for (let copy = 0; copy < 3; copy++) for (const t of times) {
    const p = M.layerPose(l, copy, t);
    console.log(`pose ${li} ${copy} ${t} ${f(p.dx)} ${f(p.dy)} ${f(p.opacity)} ${f(p.scale)} ${p.frame}`);
  }
});
for (const [seed, i] of [[1, 0], [1234, 7], [4294967295, 39]]) {
  console.log(`mix ${seed} ${i} ${M.mix32(seed, i)}`);
  console.log(`draws ${seed} ${i} ${M.draws(seed, i, 6).join(' ')}`);
}
['fall', 'rise', 'twinkle'].forEach((kind, ki) => {
  const E = { kind, sprites: [{}, {}, {}], area: { x: 20, y: 30, w: 200, h: 150 }, speed_min: 20, speed_max: 40, sway: 10, period: 4000, phase: 500, seed: 1234 };
  for (let i = 0; i < 6; i++) {
    const q = M.particleParams(E, i);
    for (const t of times) {
      const sizes = [0, 1, 2].map(k => ({ w: 8 + 2 * k, h: 10 + k }));
      const p = M.particle(E, i, t, sizes);
      console.log(`part ${ki} ${i} ${t} ${q.sprite} ${p.x} ${p.y} ${f(p.opacity)}`);
    }
  }
});
