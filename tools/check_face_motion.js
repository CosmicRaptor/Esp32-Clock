// The JS half of tools/check_face_motion.sh: prints the same lines as check_face_motion.cpp, from the portal's module.
const M = require('../portal/public/face-motion.js');
const mo = (type, period, phase = 0, extra = {}) => ({ type, period, phase, ...extra });
const L = { x: 5, y: 9, stagger: { x: 7, y: -3, phase: -250 }, motion: [
  mo('bob', 3200, 100, { amp: 4 }), mo('path', 8000, 0, { dx: -30, dy: 12, mode: 'pingpong' }),
  mo('fade', 2000, 300, { keys: [{ at: 0, v: 100 }, { at: 400, v: 20 }, { at: 800, v: 70 }] })] };
const L2 = { stagger: { x: 0, y: 0, phase: 0 }, motion: [
  mo('sway', 1000, 250, { amp: -6 }), mo('pulse', 1500, 0, { min: 90, max: 110 }), mo('blink', 1000, 900, { on: 150 })] };
const L3 = { stagger: { x: 0, y: 0, phase: 0 }, motion: [mo('frames', 667, 0, { frames: 4, fps: 6 }), mo('path', 1000, 0, { dx: 40, dy: 0, mode: 'loop' })] };
const L4 = { scale: 60, scatter: { area: { x: 10, y: 20, w: 200, h: 150 }, seed: 4242 }, stagger: { x: 0, y: 0, phase: 0 }, motion: [
  mo('bob', 2000, 0, { amp: 3 }), mo('pulse', 900, 100, { min: 90, max: 120 })] };
for (let copy = 0; copy < 5; copy++) {
  const a = M.copyOrigin(L4, copy, 24, 22), b = M.copyOrigin(L, copy, 24, 22);
  console.log(`origin scatter ${copy} ${a.x.toFixed(3)} ${a.y.toFixed(3)}`);
  console.log(`origin row ${copy} ${b.x.toFixed(3)} ${b.y.toFixed(3)}`);
}
const times = [0, 137, 500, 1234, 2999, 9999, 123456];
const f = (v) => v.toFixed(3);
[L, L2, L3, L4].forEach((l, li) => {
  for (let copy = 0; copy < 5; copy++) for (const t of times) {
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

const flash={scatter:{area:{x:30,y:10,w:250,h:100},seed:719,period:1000},motion:[{type:'blink',period:1000,on:250,phase:0}]};
for(const t of [0,100,999,1000,1100,1999,2000,123456,4294967000]) {
  const o=M.copyOrigin(flash,0,11,42,t),p=M.layerPose(flash,0,t);
  console.log(`flash ${t} ${o.x.toFixed(3)} ${o.y.toFixed(3)} ${p.opacity.toFixed(3)}`);
}
