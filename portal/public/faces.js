// Bhoomi clock admin page: faces. Plain JS, no build step. All text goes in via textContent.
// The motion maths is in face-motion.js, the "which face now" rules in face-rules.js, image conversion in face-assets.js.
'use strict';

const $ = id => document.getElementById(id);
const el = (tag, cls, text) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const SCREEN_W = 320, SCREEN_H = 240;
const TEXT_LINE = 1.219, TEXT_BASELINE = 0.968;      // Montserrat Medium: line height and baseline, in em
const MONTHS_SHORT = FaceRules.MONTHS.map(m => m.slice(0, 3));
const DAYS_SHORT = FaceRules.DAYS.map(d => d.slice(0, 3));
const DAY_ORDER = [1, 2, 3, 4, 5, 6, 0];             // shown Monday first; values match C's tm_wday

let faces = [];                  // saved faces, in priority order
let settings = { default: '', pinned: '' };
let budgets = {};                // face id -> budget report
let limits = { layers: 8, emitters: 3, motions: 3, copies: 12, fade_keys: 4, budget_px: 30000 };
let device = null;
let skew = 0;                    // server clock minus ours, seconds
const assetMeta = {};            // hash -> {kind, w, h, bytes}
const localImg = {};             // hash -> canvas, for what was uploaded in this tab (shown before the server round trip)
const loaded = {};               // hash -> HTMLImageElement

// ---------------------------------------------------------------- api

async function api(path, opts = {}) {
  const r = await fetch(path, { credentials: 'same-origin', ...opts });
  let j = {};
  try { j = await r.json(); } catch (e) { /* empty body */ }
  if (r.status === 401) { location.href = '/'; throw new Error('Please log in.'); }
  if (!r.ok) throw new Error(j.error || `Something went wrong (${r.status}).`);
  return j;
}
const sendJson = (path, method, body) =>
  api(path, { method, headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });

function toast(text, bad) {
  const t = $('toast');
  t.textContent = text;
  t.className = 'toast show' + (bad ? ' bad' : '');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.className = 'toast' + (bad ? ' bad' : ''); }, bad ? 4500 : 2600);
}

async function busy(btn, fn) {
  btn.disabled = true;
  try { return await fn(); }
  catch (e) { toast(e.message, true); }
  finally { btn.disabled = false; }
}

// ---------------------------------------------------------------- the clock's time

function nowSec() { return Date.now() / 1000 + skew; }
function validTz(tz) {
  try { new Intl.DateTimeFormat('en', { timeZone: tz }); return true; } catch (e) { return false; }
}
function clockTz() {
  return device && device.tz && validTz(device.tz) ? device.tz : Intl.DateTimeFormat().resolvedOptions().timeZone;
}
let nowCache = { sec: -1, parts: null };
function clockParts() {            // the clock's local date and time, recomputed once a second
  const sec = Math.floor(nowSec());
  if (nowCache.sec !== sec) nowCache = { sec, parts: FaceRules.localParts(new Date(sec * 1000), clockTz()) };
  return nowCache.parts;
}

// ---------------------------------------------------------------- assets

function register(meta) { assetMeta[meta.hash] = { kind: meta.kind, w: meta.w, h: meta.h, bytes: meta.bytes }; }
function registerAssets(map) { for (const [h, m] of Object.entries(map || {})) assetMeta[h] = m; }
const spriteInfo = h => assetMeta[h] || { kind: 'sprite', w: 40, h: 40, bytes: 0 };
const assetUrl = h => `/api/assets/${h}.${assetMeta[h] && assetMeta[h].kind === 'bg' ? 'jpg' : 'png'}`;

// Something drawable for an asset, or null while it loads.
function drawable(h) {
  if (localImg[h]) return localImg[h];
  let img = loaded[h];
  if (!img) {
    img = loaded[h] = new Image();
    img.onload = requestDraw;
    img.onerror = () => { img.failed = true; };
    img.src = assetUrl(h);
  }
  return img.complete && img.naturalWidth && !img.failed ? img : null;
}

// ---------------------------------------------------------------- boot

async function load() {
  let j;
  try { j = await api('/api/faces'); } catch (e) { toast(e.message, true); return; }
  skew = j.now - Date.now() / 1000;
  device = j.device;
  limits = j.limits;
  adopt(j);
  if ($('editView').hidden) showList();
}

function adopt(j) {
  faces = j.faces;
  settings = j.settings;
  budgets = j.budgets;
  for (const f of faces) registerAssets(f.assets);
}

async function refresh() {
  const j = await api('/api/faces');
  device = j.device;
  limits = j.limits;
  adopt(j);
}

// ---------------------------------------------------------------- list

function describeWhen(w) {
  const bits = [];
  const hh = n => String(n % 24).padStart(2, '0') + ':00';
  if (w.hours) bits.push(`${hh(w.hours.from)}–${hh(w.hours.to)}`);
  if (w.months.length) bits.push(w.months.length === 12 ? 'every month' : w.months.map(m => MONTHS_SHORT[m - 1]).join(', '));
  if (w.weekdays.length) {
    const key = [...w.weekdays].sort().join(',');
    bits.push(key === '0,1,2,3,4,5,6' ? 'every day' : key === '1,2,3,4,5' ? 'weekdays' : key === '0,6' ? 'weekends'
      : DAY_ORDER.filter(d => w.weekdays.includes(d)).map(d => DAYS_SHORT[d]).join(', '));
  }
  for (const d of w.dates) bits.push(`${d.day} ${MONTHS_SHORT[d.month - 1]}${d.year ? ' ' + d.year : ''}`);
  return bits.join(' · ');
}

// Which face the clock would show right now, and why.
function activeNow() {
  const list = faces.map(f => ({ id: f.id, when: f.when }));
  const id = FaceRules.pick(list, settings.default, settings.pinned, clockParts());
  if (!id) return { id: null };
  const why = id === settings.pinned ? 'pinned' : faces.find(f => f.id === id && FaceRules.matches(f.when, clockParts())) ? 'rules' : 'fallback';
  return { id, why };
}

function renderActive() {
  const p = $('active');
  p.textContent = '';
  const a = activeNow();
  const tz = device && device.tz && validTz(device.tz) ? device.tz.replace(/_/g, ' ') : null;
  if (!faces.length) { p.hidden = true; return; }
  p.hidden = false;
  p.append('On the clock right now: ');
  if (!a.id) p.append(el('b', '', "its built-in face"), el('small', '', '(no face fits, and there is no fallback)'));
  else {
    const f = faces.find(x => x.id === a.id);
    p.append(el('b', '', f.name), el('small', '', { pinned: '(pinned)', rules: '(its rules fit right now)', fallback: '(the fallback: no face’s rules fit)' }[a.why]));
  }
  const t = clockParts();
  p.append(el('small', 'when', `It is ${FaceRules.formatTime(t)} on ${FaceRules.formatDate('long', t)}${tz ? ' in ' + tz : ' (the clock hasn’t reported its time zone yet: using this browser’s)'}.`));
}

function budgetClass(r) { return r > 1 ? 'over' : r > 0.6 ? 'mid' : ''; }

function renderList() {
  const ul = $('faces');
  ul.textContent = '';
  $('facesEmpty').hidden = faces.length > 0;
  $('facesHint').hidden = faces.length === 0;
  const a = activeNow();
  faces.forEach((f, i) => {
    const li = el('li', f.id === a.id ? 'is-active' : '');
    const img = el('img', 'f-thumb');
    img.alt = '';
    img.loading = 'lazy';
    img.src = assetUrl(f.background);
    const main = el('div', 'f-main');
    const rules = describeWhen(f.when);
    main.append(el('div', 'f-name', f.name), el('div', 'f-sub', rules || 'No rules'));
    const badges = el('div', 'badges');
    if (f.id === a.id) badges.append(el('span', 'badge now', 'On the clock'));
    if (f.id === settings.pinned) badges.append(el('span', 'badge', '📌 Pinned'));
    if (f.id === settings.default) badges.append(el('span', 'badge', 'Fallback'));
    if (!rules && f.id !== settings.default && f.id !== settings.pinned) badges.append(el('span', 'badge warn', 'Only shows when pinned'));
    main.append(badges);
    const b = budgets[f.id];
    if (b) {
      const m = el('div', 'mini-meter ' + budgetClass(b.ratio), '');
      m.title = `Redraw budget: ${Math.round(b.ratio * 100)}%`;
      m.append(el('i'));
      m.firstChild.style.width = Math.min(100, b.ratio * 100) + '%';
      main.append(m);
    }
    li.append(img, main);

    const acts = el('div', 'f-actions');
    const edit = el('button', 'btn ghost small', 'Edit');
    edit.onclick = () => openEditor(f);
    const pin = el('button', 'btn ghost small', f.id === settings.pinned ? 'Unpin' : '📌 Pin');
    pin.onclick = () => busy(pin, async () => {
      await setSettings({ pinned: f.id === settings.pinned ? '' : f.id });
      toast(f.id === settings.pinned ? `${f.name} is pinned: the clock shows it until you unpin it` : 'Unpinned: back to the rules');
    });
    const up = el('button', 'btn ghost small', '▲');
    up.title = 'Earlier in the list: wins over the faces after it';
    up.setAttribute('aria-label', 'Move up');
    up.disabled = i === 0;
    up.onclick = () => busy(up, () => move(i, -1));
    const down = el('button', 'btn ghost small', '▼');
    down.title = 'Later in the list';
    down.setAttribute('aria-label', 'Move down');
    down.disabled = i === faces.length - 1;
    down.onclick = () => busy(down, () => move(i, 1));
    const menu = el('details', 'menu');
    menu.append(el('summary', 'btn ghost small', '⋯'));
    const pop = el('div', 'menu-pop');
    const dup = el('button', '', 'Duplicate');
    dup.onclick = () => busy(dup, async () => {
      menu.open = false;
      await sendJson(`/api/faces/${f.id}/duplicate`, 'POST', {});
      await refresh(); showList();
      toast('Duplicated');
    });
    const fb = el('button', '', f.id === settings.default ? 'Stop using as fallback' : 'Use as fallback');
    fb.onclick = () => busy(fb, async () => {
      menu.open = false;
      await setSettings({ default: f.id === settings.default ? '' : f.id });
      toast(f.id === settings.default ? `${f.name} is the fallback when no face's rules fit` : 'No fallback: the clock uses its built-in face');
    });
    const del = el('button', 'danger', 'Delete');
    del.onclick = () => busy(del, async () => {
      menu.open = false;
      if (!confirm(`Delete “${f.name}”?`)) return;
      await api(`/api/faces/${f.id}`, { method: 'DELETE' });
      await refresh(); showList();
      toast('Deleted');
    });
    pop.append(dup, fb, del);
    menu.append(pop);
    acts.append(edit, pin, el('span', 'grow'), up, down, menu);
    li.append(acts);
    ul.append(li);
  });
  renderActive();
}

async function setSettings(body) {
  const j = await sendJson('/api/face-settings', 'PUT', body);
  settings = { default: j.state.faces.default, pinned: j.state.faces.pinned };
  const order = j.state.faces.list.map(e => e.id);
  faces.sort((a, b) => order.indexOf(a.id) - order.indexOf(b.id));
  renderList();
}
async function move(i, d) {
  const ids = faces.map(f => f.id);
  [ids[i], ids[i + d]] = [ids[i + d], ids[i]];
  await setSettings({ order: ids });
}

document.addEventListener('click', e => {
  document.querySelectorAll('details.menu[open]').forEach(d => { if (!d.contains(e.target)) d.open = false; });
});
setInterval(() => { if (!$('listView').hidden) renderActive(); }, 20000);

function showList() {
  $('editView').hidden = true;
  $('listView').hidden = false;
  document.title = "Faces · Bhoomi's clock";
  renderList();
}

$('newFace').onclick = () => {
  if (faces.length >= limits.faces) { toast(`That's a lot of faces! The limit is ${limits.faces}.`, true); return; }
  openEditor(null);
};

// ---------------------------------------------------------------- the editor's draft

let draft = null;          // the face being edited (same shape as the server's, minus id/rev/assets)
let draftId = '';          // '' while it is a new face
let savedJson = '';        // the draft as last saved, to tell whether anything changed
let sel = { kind: 'bg' };  // what the inspector shows: bg | time | date | icon | layer {i} | particles {i}
let lastCheck = null;      // the server's verdict on the draft: {ok, errors, budget}

const newFace = () => ({
  name: '', background: null,
  time: { x: 160, y: 24, size: 72, color: '#ffffff', shadow: '#000000', align: 'center' },
  date: { x: 160, y: 118, size: 20, color: '#ffffff', shadow: '#000000', align: 'center', format: 'long' },
  icon: null, layers: [], particles: [], nowplaying: null, when: FaceRules.emptyWhen(),
});
// The Spotify strip's usual look on the clock (and what "Customise" starts from).
const NP_DEFAULT = { show: true, x: 8, y: 8, w: 304, bg: '#000000', bg_pct: 60, title: '#ffffff', artist: '#ffb3c0' };
const NP_H = 46;
const payload = () => { const { assets, rev, id, ...rest } = draft; return rest; };
const snapshot = () => JSON.stringify(payload());
const isDirty = () => draft && snapshot() !== savedJson;

function openEditor(face) {
  draft = face ? JSON.parse(JSON.stringify(face)) : newFace();
  delete draft.assets; delete draft.rev; delete draft.id;
  draftId = face ? face.id : '';
  savedJson = snapshot();
  sel = { kind: 'bg' };
  lastCheck = null;
  $('listView').hidden = true;
  $('editView').hidden = false;
  $('editTitle').textContent = face ? face.name : 'New face';
  document.title = (face ? face.name : 'New face') + " · Bhoomi's clock";
  $('fName').value = draft.name;
  t0 = performance.now(); tPaused = 0;
  renderAll();
  runCheck();
  window.scrollTo(0, 0);
}

function renderAll() {
  renderElements(); renderInspector(); renderLibrary(); renderWhen(); renderMeter(); updateSave();
  $('stageEmpty').hidden = !!draft.background;
  requestDraw();
}

function leaveEditor() {
  if (isDirty() && !confirm('Throw away your changes to this face?')) return;
  draft = null;
  showList();
}
$('editBack').onclick = leaveEditor;
$('editRevert').onclick = () => { if (confirm('Throw away your changes to this face?')) openEditor(draftId ? faces.find(f => f.id === draftId) : null); };
window.addEventListener('beforeunload', e => { if (draft && isDirty()) { e.preventDefault(); e.returnValue = ''; } });

function updateSave() {
  const dirty = isDirty();
  $('editSave').disabled = !dirty && !!draftId;
  $('editSave').textContent = draftId ? 'Save' : 'Create face';
  $('editRevert').hidden = !dirty || !draftId;
}

// Something in the draft changed. `structural`: the lists (elements, inspector) need to be rebuilt.
function changed(structural) {
  if (structural) { renderElements(); renderInspector(); renderLibrary(); }
  $('stageEmpty').hidden = !!draft.background;
  updateSave();
  scheduleCheck();
  requestDraw();
}

$('fName').oninput = () => { draft.name = $('fName').value; updateSave(); };

$('editSave').onclick = () => busy($('editSave'), async () => {
  if (!draft.name.trim()) { toast('Give the face a name first.', true); $('fName').focus(); return; }
  if (!draft.background) { toast('Choose a background first.', true); return; }
  const j = draftId ? await sendJson(`/api/faces/${draftId}`, 'PUT', payload()) : await sendJson('/api/faces', 'POST', payload());
  await refresh();
  const saved = faces.find(f => f.id === j.face.id) || j.face;
  draftId = saved.id;
  draft = JSON.parse(JSON.stringify(saved));
  delete draft.assets; delete draft.rev; delete draft.id;
  savedJson = snapshot();
  $('editTitle').textContent = saved.name;
  renderAll();
  toast('Saved ✨ The clock picks it up within about 10 seconds.');
});

// ---------------------------------------------------------------- budget meter (the server does the maths)

let checkTimer = 0, checkSeq = 0;
function scheduleCheck() {
  clearTimeout(checkTimer);
  $('meter').classList.add('stale');
  checkTimer = setTimeout(runCheck, 300);
}
async function runCheck() {
  if (!draft) return;
  if (!draft.background) { lastCheck = null; renderMeter(); return; }
  const n = ++checkSeq;
  try {
    const r = await sendJson('/api/face-check', 'POST', payload());
    if (n !== checkSeq) return;
    lastCheck = r;
  } catch (e) { if (n === checkSeq) lastCheck = { ok: false, errors: [e.message], budget: null }; }
  renderMeter();
}

function renderMeter() {
  const m = $('meter');
  m.classList.remove('stale', 'mid', 'over');
  const c = lastCheck, b = c && c.budget;
  const ratio = b ? b.ratio : 0;
  const cls = budgetClass(ratio);
  if (cls) m.classList.add(cls);
  $('meterFill').style.width = Math.min(125, ratio * 100) / 1.25 + '%';
  m.setAttribute('aria-valuenow', String(Math.round(ratio * 100)));
  $('meterText').textContent = b ? `${Math.round(ratio * 100)}% used` : (draft && draft.background ? '…' : 'nothing moving yet');
  const ul = $('problems');
  ul.textContent = '';
  for (const e of (c && c.errors) || []) ul.append(el('li', '', e));
  const items = $('meterItems');
  items.textContent = '';
  for (const it of (b && b.items) || []) {
    const li = el('li');
    li.append(el('span', '', it.what), el('span', '', `${Math.round(it.px / b.limit * 100)}%`));
    items.append(li);
  }
  if (b && !b.items.length) items.append(el('li', '', 'Nothing moves.'));
  $('meterMem').textContent = b
    ? `About ${Math.round(b.px).toLocaleString()} of ${b.limit.toLocaleString()} pixels redrawn per tick (${limits.anim_fps || 15} ticks a second). Images take ${Math.round(b.bytes / 1000)} of ${Math.round(b.bytes_limit / 1000)} KB of the clock's memory.` : '';
}

// ---------------------------------------------------------------- elements list

const MOTION_NAMES = { bob: 'Bob', sway: 'Sway', path: 'Path', fade: 'Fade', blink: 'Blink', pulse: 'Pulse', frames: 'Frames' };

function selIs(kind, i) { return sel.kind === kind && (i === undefined || sel.i === i); }
function select(kind, i) { sel = { kind, i }; renderElements(); renderInspector(); renderLibrary(); requestDraw(); }

function layerName(l, i) { return l.name || `Sprite ${i + 1}`; }

function renderElements() {
  const ul = $('elements');
  ul.textContent = '';
  const row = (kind, i, icon, name, sub) => {
    const li = el('li', selIs(kind, i) ? 'sel' : '');
    li.tabIndex = 0;
    li.append(icon, el('span', 'e-name', name));
    if (sub) li.append(el('span', 'e-sub', sub));
    li.onclick = () => select(kind, i);
    li.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); select(kind, i); } };
    ul.append(li);
  };
  const swatch = t => el('span', 'swatch', t);
  const thumb = h => { const i = el('img'); i.alt = ''; i.src = assetUrl(h); return i; };
  // front-most first, like a layers panel
  const np = draft.nowplaying;
  row('np', undefined, swatch('♪'), 'Now playing', !np ? 'usual look' : np.show ? 'custom' : 'hidden');
  if (draft.icon) row('icon', undefined, thumb(draft.icon.sprite), 'Icon', motionSummary(draft.icon));
  row('date', undefined, swatch('Aa'), 'Date', draft.date ? '' : 'hidden');
  row('time', undefined, swatch('12'), 'Time', '');
  for (let i = draft.particles.length - 1; i >= 0; i--) {
    const p = draft.particles[i];
    row('particles', i, swatch({ fall: '❄', rise: '🫧', twinkle: '✦' }[p.kind] || '·'), `Particles ${i + 1}`, `${p.kind} · ${p.count}`);
  }
  for (let i = draft.layers.length - 1; i >= 0; i--) {
    const l = draft.layers[i];
    row('layer', i, thumb(l.sprite), layerName(l, i), (l.copies > 1 ? `×${l.copies}${l.scatter ? ' scattered' : ''} ` : '') + (l.scale !== 100 ? `${l.scale}% ` : '') + motionSummary(l));
  }
  row('bg', undefined, draft.background ? thumb(draft.background) : swatch('🖼'), 'Background', draft.background ? '' : 'choose one');
}
const motionSummary = l => (l.motion || []).map(m => MOTION_NAMES[m.type]).join(', ');

// ---------------------------------------------------------------- inspector: small form helpers

let bound = [];     // number inputs showing a value of the draft, refreshed while dragging

function field(label, input, cls = '') {
  const d = el('div', 'f ' + cls);
  const id = 'in' + Math.random().toString(36).slice(2, 8);
  input.id = id;
  const l = el('label', 'lbl', label);
  l.htmlFor = id;
  d.append(l, input);
  return d;
}
function numField(label, obj, key, o = {}) {
  const i = el('input');
  i.type = 'number';
  if (o.min !== undefined) i.min = o.min;
  if (o.max !== undefined) i.max = o.max;
  i.step = o.step || 1;
  i.value = obj[key];
  i.oninput = () => {
    const v = i.valueAsNumber;
    if (Number.isFinite(v)) { obj[key] = Math.round(v); if (o.after) o.after(); changed(false); }
  };
  i.onchange = () => {
    let v = i.valueAsNumber;
    if (!Number.isFinite(v)) v = obj[key];
    v = Math.round(v);
    if (o.min !== undefined) v = Math.max(o.min, v);
    if (o.max !== undefined) v = Math.min(o.max, v);
    obj[key] = v; i.value = v;
    if (o.after) o.after();
    changed(!!o.structural);
  };
  bound.push({ obj, key, input: i });
  return field(label, i, o.cls);
}
function selectField(label, options, obj, key, o = {}) {
  const s = el('select');
  for (const [v, text] of options) { const op = el('option', '', text); op.value = v; s.append(op); }
  s.value = obj[key];
  s.onchange = () => { obj[key] = s.value; if (o.after) o.after(); changed(!!o.structural); };
  return field(label, s, o.cls);
}
function checkField(label, get, set, cls = '') {
  const d = el('div', 'f ' + cls);
  const l = el('label', 'check');
  const c = el('input');
  c.type = 'checkbox'; c.checked = get();
  c.onchange = () => set(c.checked);
  l.append(c, el('span', '', label));
  d.append(l);
  return d;
}
function colorField(label, obj, key, o = {}) {
  const i = el('input');
  i.type = 'color'; i.value = obj[key];
  i.oninput = () => { obj[key] = i.value; changed(false); };
  return field(label, i, o.cls);
}
function syncBound() { for (const b of bound) if (document.activeElement !== b.input) b.input.value = b.obj[b.key]; }

// ---------------------------------------------------------------- inspector

const target = () => sel.kind === 'layer' ? draft.layers[sel.i] : sel.kind === 'icon' ? draft.icon
  : sel.kind === 'time' ? draft.time : sel.kind === 'date' ? draft.date : sel.kind === 'particles' ? draft.particles[sel.i]
  : sel.kind === 'np' ? draft.nowplaying : null;
// The rectangle that moves when you drag it: a particle effect's area, a scattered layer's area, else the thing itself.
const areaOf = t => !t ? null : t.area ? t.area : t.scatter ? t.scatter.area : null;

function nudgePad() {
  const pad = el('div', 'nudge');
  const b = (txt, dx, dy, label) => {
    const x = el('button', '', txt);
    x.type = 'button';
    x.setAttribute('aria-label', label);
    x.onclick = () => nudge(dx, dy);
    return x;
  };
  const blank = () => el('span', 'blank');
  pad.append(blank(), b('↑', 0, -1, 'Up 1 pixel'), blank(), b('←', -1, 0, 'Left 1 pixel'), b('↓', 0, 1, 'Down 1 pixel'), b('→', 1, 0, 'Right 1 pixel'));
  return pad;
}
function nudge(dx, dy) {
  const t = target();
  if (!t || sel.kind === 'bg') return;
  const a = areaOf(t) || t;
  a.x += dx; a.y += dy;
  syncBound(); changed(false);
}

function posFields(obj, o = {}) {
  const wrap = el('div', 'pos-row');
  const f = el('div', 'fields');
  f.append(numField('x', obj, 'x', { min: -1000, max: 1000 }), numField('y', obj, 'y', { min: -1000, max: 1000 }));
  wrap.append(f, nudgePad());
  return wrap;
}

function renderInspector() {
  const box = $('inspector');
  box.textContent = '';
  bound = [];
  const head = (title, ...btns) => {
    const h = el('h2');
    h.append(el('span', '', title));
    if (btns.length) { const g = el('span', 'actions tight'); g.append(...btns); h.append(g); }
    box.append(h);
  };
  const small = (text, fn, cls = '') => { const b = el('button', 'btn ghost small ' + cls, text); b.type = 'button'; b.onclick = fn; return b; };

  if (sel.kind === 'bg') {
    head('Background');
    box.append(el('p', 'hint first', draft.background
      ? 'The picture behind everything, with the moving parts painted out. Use “Choose background” under Images to replace it.'
      : 'Start by choosing a background under Images: a 320×240 picture with nothing that moves painted on it.'));
  } else if (sel.kind === 'time' || sel.kind === 'date') {
    const isDate = sel.kind === 'date', slot = draft[sel.kind];
    if (isDate && !slot) {
      head('Date');
      box.append(el('p', 'hint first', 'The date is hidden on this face.'));
      box.append(small('Show the date', () => { draft.date = newFace().date; changed(true); }));
      return;
    }
    head(isDate ? 'Date' : 'Time', ...(isDate ? [small('Hide', () => { draft.date = null; sel = { kind: 'bg' }; changed(true); })] : []));
    box.append(posFields(slot));
    const f = el('div', 'fields');
    const size = numField('Size (px)', slot, 'size', { min: limits.text_min || 10, max: limits.text_max || 96, after: () => sizeRange && (sizeRange.value = slot.size) });
    const range = el('input'); range.type = 'range';
    range.min = limits.text_min || 10; range.max = limits.text_max || 96; range.value = slot.size;
    const sizeRange = range;
    range.oninput = () => { slot.size = +range.value; syncBound(); changed(false); };
    f.append(size, field('Slide', range), colorField('Colour', slot, 'color'),
      checkField('Shadow', () => !!slot.shadow, on => { slot.shadow = on ? '#000000' : ''; changed(true); }));
    if (slot.shadow) f.append(colorField('Shadow colour', slot, 'shadow'));
    f.append(selectField('Align', [['left', 'Left edge at x'], ['center', 'Centred on x'], ['right', 'Right edge at x']], slot, 'align'));
    if (isDate) f.append(selectField('Format', [['long', 'Friday, 7 November'], ['short', 'Fri, 7 Nov'], ['day_month', '7 November'], ['weekday', 'Friday'], ['numeric', '07/11/2026']], slot, 'format'));
    box.append(f);
    box.append(el('p', 'hint', 'y is the top of the line. The font is Montserrat Medium, like on the clock.'));
  } else if (sel.kind === 'layer' || sel.kind === 'icon') {
    const isIcon = sel.kind === 'icon', l = target();
    const i = sel.i;
    const btns = [];
    if (!isIcon) {
      btns.push(small('▲', () => reorderLayer(i, 1)), small('▼', () => reorderLayer(i, -1)));
      btns[0].title = 'Bring forward'; btns[1].title = 'Send backward';
      btns[0].disabled = i === draft.layers.length - 1; btns[1].disabled = i === 0;
      btns.push(small('Copy', () => { if (draft.layers.length >= limits.layers) return toast(`At most ${limits.layers} layers.`, true); draft.layers.splice(i + 1, 0, JSON.parse(JSON.stringify(l))); select('layer', i + 1); changed(true); }));
    }
    btns.push(small('Delete', () => {
      if (isIcon) draft.icon = null; else draft.layers.splice(i, 1);
      sel = { kind: 'bg' }; changed(true);
    }, 'danger'));
    head(isIcon ? 'Icon' : layerName(l, i), ...btns);
    const info = spriteInfo(l.sprite);
    const sp = el('div', 'sprite-pick');
    const im = el('img'); im.alt = ''; im.src = assetUrl(l.sprite);
    sp.append(im, el('div', 'meta', `${frameWidth(l)}×${info.h} px${frameCount(l) > 1 ? `, ${frameCount(l)} frames` : ''}. Click another sprite under Images to swap it.`));
    box.append(sp);
    if (!isIcon) { const n = el('input'); n.maxLength = 24; n.value = l.name || ''; n.placeholder = `Sprite ${i + 1}`; n.oninput = () => { l.name = n.value; changed(false); }; n.onchange = () => renderElements(); box.append(field('Name', n)); }
    if (isIcon) box.append(el('p', 'hint', `The icon is drawn above the time and date. Up to ${limits.icon || 64}×${limits.icon || 64} px.`));
    const sheet = frameCount(l) > 1;
    const sf = el('div', 'fields');
    if (!sheet) sf.append(numField('Size (%)', l, 'scale', { min: limits.scale_min || 10, max: limits.scale_max || 200, structural: true }));
    sf.append(numField('Copies', l, 'copies', { min: 1, max: limits.copies, structural: true }));
    box.append(sf);
    if (sheet) box.append(el('p', 'hint', 'A sprite sheet can’t be resized here: upload it smaller (“fit within”).'));
    else if (l.scale !== 100) box.append(el('p', 'hint', `Drawn at ${l.scale}% of its size, about its centre. Software scaling costs the clock a little more redraw time.`));
    if (l.copies > 1 || l.scatter) {
      box.append(checkField('Spawn the copies at random places', () => !!l.scatter, on => {
        l.scatter = on ? { area: { x: 0, y: 0, w: SCREEN_W, h: SCREEN_H }, seed: 1 + Math.floor(Math.random() * 99999) } : null;
        changed(true);
      }));
    }
    if (l.scatter) {
      const a = l.scatter.area;
      const af = el('div', 'fields');
      af.append(numField('Area x', a, 'x', { min: -1000, max: 1000 }), numField('Area y', a, 'y', { min: -1000, max: 1000 }),
        numField('Width', a, 'w', { min: 1, max: 1000 }), numField('Height', a, 'h', { min: 1, max: 1000 }));
      box.append(af);
      const row2 = el('div', 'actions left');
      row2.append(small('Whole screen', () => { Object.assign(a, { x: 0, y: 0, w: SCREEN_W, h: SCREEN_H }); syncBound(); changed(false); }),
        small('🎲 Shuffle', () => { l.scatter.seed = 1 + Math.floor(Math.random() * 99999); changed(false); }), nudgePad());
      box.append(row2, el('p', 'hint', 'Each copy lands somewhere inside this area, and its motions start at a random point of their cycle. Drag the area on the preview; drag its corner to resize.'));
    } else {
      box.append(posFields(l));
      if (l.copies > 1) {
        const sf2 = el('div', 'fields three');
        sf2.append(numField('Step x', l.stagger, 'x', { min: -320, max: 320 }), numField('Step y', l.stagger, 'y', { min: -240, max: 240 }),
          numField('Delay (ms)', l.stagger, 'phase', { min: -60000, max: 60000 }));
        box.append(sf2, el('p', 'hint', 'Each copy sits one step further along and its motions start a little later.'));
      }
    }
    box.append(el('div', 'sub-h', 'Motion'));
    l.motion.forEach((m, mi) => box.append(motionCard(l, mi)));
    if (l.motion.length < limits.motions) {
      const add = el('select');
      add.append(Object.assign(el('option', '', '+ Add a motion…'), { value: '' }));
      for (const [k, name] of Object.entries(MOTION_NAMES)) if (k !== 'frames' || !l.motion.some(m => m.type === 'frames')) add.append(Object.assign(el('option', '', name + ' — ' + MOTION_HELP[k]), { value: k }));
      add.onchange = () => { if (add.value) { l.motion.push(defaultMotion(add.value, l)); changed(true); } };
      const wrap = el('div', 'f'); wrap.append(add); wrap.style.marginTop = '10px';
      box.append(wrap);
    } else box.append(el('p', 'hint', `At most ${limits.motions} motions per sprite.`));
    box.append(el('p', 'hint', 'No rotation: it is slow on the clock. For a wobble or a flap, use a sprite sheet with Frames.'));
  } else if (sel.kind === 'np') {
    const np = draft.nowplaying;
    head('Now playing', ...(np ? [small('Usual look', () => { draft.nowplaying = null; changed(true); })] : []));
    if (!np) {
      box.append(el('p', 'hint first', 'The Spotify strip (the two songs playing) has its usual look on this face: a dark pill at the top. It only appears while somebody is playing. The preview shows a stand-in while this is selected.'));
      box.append(small('Customise', () => { draft.nowplaying = { ...NP_DEFAULT }; changed(true); }));
    } else {
      box.append(checkField('Show it on this face', () => np.show, on => { np.show = on; changed(true); }));
      if (np.show) {
        box.append(posFields(np));
        const f = el('div', 'fields');
        f.append(numField('Width', np, 'w', { min: limits.np_w_min || 200, max: limits.np_w_max || 320 }),
          checkField('Dark pill behind it', () => !!np.bg, on => { np.bg = on ? '#000000' : ''; changed(true); }));
        if (np.bg) f.append(colorField('Pill colour', np, 'bg'), numField('Pill opacity (%)', np, 'bg_pct', { min: 0, max: 100 }));
        f.append(colorField('Song colour', np, 'title'), colorField('Artist colour', np, 'artist'));
        box.append(f, el('p', 'hint', `The strip is ${NP_H} px high and always on top. It sits at x, y; the first person's song on the left half, the second's on the right.`));
      }
    }
  } else if (sel.kind === 'particles') {
    const p = target(), i = sel.i;
    head(`Particles ${i + 1}`, small('Delete', () => { draft.particles.splice(i, 1); sel = { kind: 'bg' }; changed(true); }, 'danger'));
    const f = el('div', 'fields');
    f.append(selectField('Kind', [['fall', 'Falls (snow, petals, leaves)'], ['rise', 'Rises (bubbles, sparks)'], ['twinkle', 'Twinkles (stars)']], p, 'kind', { structural: true }),
      numField('How many', p, 'count', { min: 1, max: limits.particles_each || 40 }));
    box.append(f);
    box.append(el('div', 'sub-h', 'Sprites'));
    const chips = el('div', 'sprite-pick');
    chips.style.flexWrap = 'wrap';
    p.sprites.forEach((h, k) => {
      const w = el('button', 'btn ghost small', '');
      w.type = 'button';
      const im = el('img'); im.alt = ''; im.src = assetUrl(h); im.style.cssText = 'width:28px;height:28px;object-fit:contain';
      w.append(im, ' ✕');
      w.title = 'Remove';
      w.onclick = () => { if (p.sprites.length > 1) { p.sprites.splice(k, 1); changed(true); } else toast('Keep at least one sprite.', true); };
      chips.append(w);
    });
    box.append(chips, el('p', 'hint', `Click sprites under Images to add them (up to ${limits.emitter_sprites || 3}, at most ${limits.particle_sprite || 48}×${limits.particle_sprite || 48} px).`));
    box.append(el('div', 'sub-h', 'Where'));
    const af = el('div', 'fields');
    af.append(numField('x', p.area, 'x', { min: -1000, max: 1000 }), numField('y', p.area, 'y', { min: -1000, max: 1000 }),
      numField('Width', p.area, 'w', { min: 1, max: 1000 }), numField('Height', p.area, 'h', { min: 1, max: 1000 }));
    box.append(af);
    const pr = el('div', 'actions left');
    pr.append(small('Whole screen', () => { Object.assign(p.area, { x: 0, y: 0, w: SCREEN_W, h: SCREEN_H }); syncBound(); changed(false); }), nudgePad());
    box.append(pr, el('p', 'hint', 'Drag the area on the preview; drag its corner to resize. Particles are cut off at its edges.'));
    box.append(el('div', 'sub-h', 'Movement'));
    const mf = el('div', 'fields');
    if (p.kind !== 'twinkle') mf.append(numField('Slowest (px/s)', p, 'speed_min', { min: 0, max: 400 }), numField('Fastest (px/s)', p, 'speed_max', { min: 0, max: 400 }),
      numField('Sway (px)', p, 'sway', { min: 0, max: 60 }));
    mf.append(numField(p.kind === 'twinkle' ? 'Twinkle cycle (ms)' : 'Sway cycle (ms)', p, 'period', { min: 500, max: 60000 }),
      numField('Start offset (ms)', p, 'phase', { min: 0, max: 1000000 }),
      selectField('Drawn', [['front', 'In front of the sprites'], ['back', 'Behind the sprites']], p, 'depth'),
      numField('Pattern (seed)', p, 'seed', { min: 0, max: 4294967295 }));
    box.append(mf);
    box.append(small('🎲 Shuffle', () => { p.seed = 1 + Math.floor(Math.random() * 99999); renderInspector(); changed(false); }));
  }
}

const MOTION_HELP = { bob: 'up and down', sway: 'side to side', path: 'drift from A to B', fade: 'opacity keyframes', blink: 'appear briefly', pulse: 'grow and shrink', frames: 'flip through a sprite sheet' };

function frameCount(l) { const m = (l.motion || []).find(x => x.type === 'frames'); return m ? m.frames : 1; }
function frameWidth(l) { return Math.floor(spriteInfo(l.sprite).w / frameCount(l)); }

function defaultMotion(type, layer) {
  const base = { period: 3000, phase: 0 };
  switch (type) {
    case 'bob': return { type, ...base, amp: 4 };
    case 'sway': return { type, ...base, period: 4000, amp: 6 };
    case 'path': return { type, ...base, period: 8000, dx: 30, dy: 0, mode: 'pingpong' };
    case 'fade': return { type, ...base, keys: [{ at: 0, v: 100 }, { at: 500, v: 30 }] };
    case 'blink': return { type, ...base, period: 4000, on: 150 };
    case 'pulse': return { type, ...base, period: 1500, min: 92, max: 108 };
    case 'frames': {
      const w = spriteInfo(layer.sprite).w;
      const n = [4, 2, 3, 5, 6, 8, 7, 9, 10, 12, 16].find(k => w % k === 0 && w / k >= 4) || 2;
      return { type, period: Math.round(1000 * n / 6), phase: 0, frames: n, fps: 6 };
    }
  }
}

function reorderLayer(i, d) {
  const j = i + d;
  if (j < 0 || j >= draft.layers.length) return;
  [draft.layers[i], draft.layers[j]] = [draft.layers[j], draft.layers[i]];
  sel = { kind: 'layer', i: j };
  changed(true);
}

function motionCard(layer, mi) {
  const m = layer.motion[mi];
  const card = el('div', 'motion');
  const head = el('div', 'm-head');
  const type = el('select');
  for (const [k, name] of Object.entries(MOTION_NAMES)) {
    if (k === 'frames' && m.type !== 'frames' && layer.motion.some(x => x.type === 'frames')) continue;
    const o = el('option', '', name); o.value = k; type.append(o);
  }
  type.value = m.type;
  type.setAttribute('aria-label', 'Motion type');
  type.onchange = () => { layer.motion[mi] = defaultMotion(type.value, layer); changed(true); };
  const rm = el('button', 'btn ghost small danger', 'Remove');
  rm.type = 'button';
  rm.onclick = () => { layer.motion.splice(mi, 1); changed(true); };
  head.append(type, rm);
  card.append(head);
  const f = el('div', 'fields');
  const timing = () => {
    if (m.type === 'frames') return;
    f.append(numField('Cycle (ms)', m, 'period', { min: 200, max: 60000, after: () => { /* keep the blink within its cycle */ if (m.on >= m.period) m.on = m.period - 1; } }),
      numField('Start offset (ms)', m, 'phase', { min: 0, max: 1000000 }));
  };
  switch (m.type) {
    case 'bob': f.append(numField('Distance up and down (px)', m, 'amp', { min: -80, max: 80, cls: 'full' })); timing(); break;
    case 'sway': f.append(numField('Distance left and right (px)', m, 'amp', { min: -80, max: 80, cls: 'full' })); timing(); break;
    case 'path':
      f.append(numField('Across (px)', m, 'dx', { min: -320, max: 320 }), numField('Down (px)', m, 'dy', { min: -240, max: 240 }),
        selectField('Style', [['pingpong', 'There and back, smoothly'], ['loop', 'Drift, then jump back']], m, 'mode', { cls: 'full' }));
      timing(); break;
    case 'blink': f.append(numField('Visible for (ms)', m, 'on', { min: 1, max: 59999, cls: 'full' })); timing(); break;
    case 'pulse': f.append(numField('Smallest (%)', m, 'min', { min: 50, max: 200 }), numField('Biggest (%)', m, 'max', { min: 50, max: 200 })); timing(); break;
    case 'frames': {
      const sync = () => { m.period = Math.round(1000 * m.frames / Math.max(1, m.fps)); };
      f.append(numField('Frames', m, 'frames', { min: 2, max: 16, after: sync }), numField('Frames per second', m, 'fps', { min: 1, max: limits.anim_fps || 15, after: sync }),
        numField('Start offset (ms)', m, 'phase', { min: 0, max: 1000000 }));
      break;
    }
    case 'fade': timing(); break;
  }
  card.append(f);
  if (m.type === 'fade') card.append(keysEditor(m));
  if (m.type === 'frames') {
    const w = spriteInfo(layer.sprite).w;
    if (w % m.frames) card.append(el('p', 'warnline', `This sprite is ${w} px wide: that doesn't split into ${m.frames} equal frames.`));
    else card.append(el('p', 'hint', `One frame is ${w / m.frames} px wide. Frames sit side by side in the PNG.`));
  }
  return card;
}

function keysEditor(m) {
  const wrap = el('div', 'keys');
  m.keys.forEach((k, ki) => {
    const row = el('div', 'key');
    const at = el('div', 'f');
    const atIn = el('input'); atIn.type = 'number'; atIn.min = 0; atIn.max = 100; atIn.value = Math.round(k.at / 10);
    atIn.disabled = ki === 0;
    atIn.onchange = () => { k.at = clamp(Math.round(+atIn.value * 10), 0, 1000); changed(true); };
    const atL = el('label', 'lbl', ki === 0 ? 'At start' : 'At (% of cycle)');
    at.append(atL, atIn);
    const v = el('div', 'f');
    const vIn = el('input'); vIn.type = 'number'; vIn.min = 0; vIn.max = 100; vIn.value = k.v;
    vIn.oninput = () => { if (Number.isFinite(vIn.valueAsNumber)) { k.v = clamp(Math.round(vIn.valueAsNumber), 0, 100); changed(false); } };
    v.append(el('label', 'lbl', 'Opacity (%)'), vIn);
    const rm = el('button', 'btn ghost small danger', '✕');
    rm.type = 'button'; rm.setAttribute('aria-label', 'Remove keyframe');
    rm.disabled = ki === 0 || m.keys.length <= 2;
    rm.onclick = () => { m.keys.splice(ki, 1); changed(true); };
    row.append(at, v, rm);
    wrap.append(row);
  });
  if (m.keys.length < (limits.fade_keys || 4)) {
    const add = el('button', 'btn ghost small', '+ Keyframe');
    add.type = 'button';
    add.onclick = () => {
      const last = m.keys[m.keys.length - 1];
      m.keys.push({ at: Math.min(1000, last.at + Math.max(10, Math.round((1000 - last.at) / 2))), v: 100 });
      m.keys.sort((a, b) => a.at - b.at);
      changed(true);
    };
    wrap.append(add);
  }
  wrap.append(el('p', 'hint', 'Opacity runs through the keyframes over one cycle, then loops back to the first.'));
  return wrap;
}

// ---------------------------------------------------------------- library of images

function renderLibrary() {
  const ul = $('library');
  ul.textContent = '';
  const used = new Set();
  for (const l of draft.layers.concat(draft.icon ? [draft.icon] : [])) used.add(l.sprite);
  for (const p of draft.particles) p.sprites.forEach(h => used.add(h));
  const known = Object.entries(assetMeta).filter(([, m]) => m.kind === 'sprite');
  known.sort((a, b) => (localImg[b[0]] ? 1 : 0) - (localImg[a[0]] ? 1 : 0));
  $('imagesMeta').textContent = known.length ? `${known.length} sprite${known.length > 1 ? 's' : ''}` : '';
  for (const [h, m] of known) {
    const li = el('li', used.has(h) ? 'used' : '');
    li.tabIndex = 0;
    li.title = `${m.w}×${m.h}. Click to ${sel.kind === 'particles' ? 'add to the particles' : sel.kind === 'layer' || sel.kind === 'icon' ? 'use on the selected sprite' : 'add to the face'}`;
    const im = el('img'); im.alt = ''; im.src = assetUrl(h);
    li.append(im, el('small', '', `${m.w}×${m.h}`));
    li.onclick = () => useSprite(h);
    li.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); useSprite(h); } };
    ul.append(li);
  }
}

// Click in the library: swap the selected sprite's image, add it to the selected particles, or add it as a new sprite.
function useSprite(h) {
  const m = spriteInfo(h);
  if (sel.kind === 'particles') {
    const p = target();
    if (m.w > (limits.particle_sprite || 48) || m.h > (limits.particle_sprite || 48)) return toast(`Particles must be at most ${limits.particle_sprite || 48}×${limits.particle_sprite || 48} px.`, true);
    if (p.sprites.includes(h)) p.sprites.splice(p.sprites.indexOf(h), 1);
    else if (p.sprites.length >= (limits.emitter_sprites || 3)) return toast(`At most ${limits.emitter_sprites || 3} sprites per particle effect.`, true);
    else p.sprites.push(h);
    if (!p.sprites.length) p.sprites.push(h);
    changed(true);
  } else if (sel.kind === 'layer' || sel.kind === 'icon') {
    const l = target();
    l.sprite = h;
    l.motion = l.motion.filter(x => x.type !== 'frames' || m.w % x.frames === 0);
    changed(true);
  } else addLayer(h);
}

function addLayer(h, frames = 1) {
  if (draft.layers.length >= limits.layers) return toast(`At most ${limits.layers} sprites on a face.`, true);
  const m = spriteInfo(h);
  const fw = Math.floor(m.w / frames);
  const l = { name: '', sprite: h, x: Math.round((SCREEN_W - fw) / 2), y: Math.round((SCREEN_H - m.h) / 2), copies: 1, stagger: { x: 0, y: 0, phase: 0 }, motion: [], scale: 100, scatter: null };
  if (frames > 1) l.motion.push({ type: 'frames', period: Math.round(1000 * frames / 6), phase: 0, frames, fps: 6 });
  draft.layers.push(l);
  select('layer', draft.layers.length - 1);
  changed(true);
}

$('addLayer').onclick = () => {
  const sprites = Object.keys(assetMeta).filter(h => assetMeta[h].kind === 'sprite');
  if (!sprites.length) { toast('Upload a sprite first (Images, below).', true); $('imagesCard').scrollIntoView({ behavior: 'smooth', block: 'nearest' }); return; }
  const h = sprites.find(x => spriteInfo(x).w <= 160) || sprites[0];
  addLayer(h);
};
$('addIcon').onclick = () => {
  if (draft.icon) return select('icon');
  const sprites = Object.keys(assetMeta).filter(h => assetMeta[h].kind === 'sprite' && spriteInfo(h).w <= (limits.icon || 64) && spriteInfo(h).h <= (limits.icon || 64));
  if (!sprites.length) { toast(`The icon has to be a sprite of at most ${limits.icon || 64}×${limits.icon || 64} px. Upload one (Images, below).`, true); return; }
  draft.icon = { sprite: sprites[0], x: 8, y: 8, copies: 1, stagger: { x: 0, y: 0, phase: 0 }, motion: [], scale: 100, scatter: null };
  select('icon');
  changed(true);
};
$('addParticles').onclick = () => {
  if (draft.particles.length >= limits.emitters) return toast(`At most ${limits.emitters} particle effects.`, true);
  const small = Object.keys(assetMeta).filter(h => assetMeta[h].kind === 'sprite' && spriteInfo(h).w <= (limits.particle_sprite || 48) && spriteInfo(h).h <= (limits.particle_sprite || 48));
  if (!small.length) { toast(`Particles use small sprites (at most ${limits.particle_sprite || 48}×${limits.particle_sprite || 48} px). Upload one first (Images, below).`, true); $('imagesCard').scrollIntoView({ behavior: 'smooth', block: 'nearest' }); return; }
  draft.particles.push({ kind: 'fall', sprites: [small[0]], count: 20, area: { x: 0, y: 0, w: SCREEN_W, h: SCREEN_H }, speed_min: 15, speed_max: 40, sway: 12, period: 4000, phase: 0, depth: 'front', seed: 1 + Math.floor(Math.random() * 99999) });
  select('particles', draft.particles.length - 1);
  changed(true);
};

// ---- uploads

for (const [labelId, inputId] of [['bgChoose', 'bgPick'], ['spChoose', 'spPick']]) {
  $(labelId).onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $(inputId).click(); } };
}
$('bgPick').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) uploadBackground(f); };
$('spPick').onchange = e => { const fs = [...e.target.files]; e.target.value = ''; if (fs.length) uploadSprites(fs); };

const card = $('imagesCard');
['dragenter', 'dragover'].forEach(t => card.addEventListener(t, e => { e.preventDefault(); card.classList.add('drag'); }));
['dragleave', 'drop'].forEach(t => card.addEventListener(t, e => { e.preventDefault(); card.classList.remove('drag'); }));
card.addEventListener('drop', e => {
  const fs = [...e.dataTransfer.files].filter(f => f.type.startsWith('image/'));
  if (!fs.length) return toast("That doesn't look like an image.", true);
  uploadSprites(fs);
});
['dragover', 'drop'].forEach(t => window.addEventListener(t, e => e.preventDefault()));

async function uploadBackground(file) {
  try {
    const bg = await FaceAssets.makeBackground(file);
    const j = await api('/api/assets/bg', { method: 'PUT', headers: { 'content-type': 'image/jpeg' }, body: bg.blob });
    register(j.asset);
    localImg[j.asset.hash] = bg.canvas;
    draft.background = j.asset.hash;
    changed(true);
    toast(bg.cropped ? 'Background set (cropped to fit 320×240)' : 'Background set');
  } catch (e) { toast(e.message, true); }
}

async function uploadSprites(files) {
  const frames = clamp(Math.floor($('spFrames').value) || 1, 1, 16);
  const added = [];
  for (const file of files) {
    try {
      const s = await FaceAssets.makeSprite(file, frames, limits, clamp(Math.floor($('spMax').value) || 64, 8, limits.sprite_frame || 160));
      const j = await api('/api/assets/sprite', { method: 'PUT', headers: { 'content-type': 'application/octet-stream' }, body: s.bin });
      await api(`/api/assets/${j.asset.hash}.png`, { method: 'PUT', headers: { 'content-type': 'image/png' }, body: s.png });
      register(j.asset);
      localImg[j.asset.hash] = s.canvas;
      added.push(j.asset.hash);
    } catch (e) { toast(`${file.name}: ${e.message}`, true); }
  }
  if (!added.length) return;
  if (sel.kind === 'particles') { for (const h of added) useSprite(h); }
  else for (const h of added) addLayer(h, frames);
  renderLibrary();
  toast(added.length > 1 ? `${added.length} sprites added` : 'Sprite added');
}

// ---------------------------------------------------------------- when

function renderWhen() {
  const w = draft.when;
  const body = $('whenBody');
  body.textContent = '';
  const sum = describeWhen(w);
  $('whenSummary').textContent = sum ? `Shown when: ${sum}. All of these must fit.` : 'No rules yet: this face is only shown when you pin it, or if it is the fallback.';

  // hours
  const hrs = checkField('Only at certain hours', () => !!w.hours, on => { w.hours = on ? { from: 9, to: 17 } : null; changed(false); renderWhen(); });
  body.append(hrs);
  if (w.hours) {
    const row = el('div', 'fields');
    const mk = (label, key, lo, hi) => {
      const s = el('select');
      for (let h = lo; h <= hi; h++) { const o = el('option', '', h === 24 ? '24:00 (midnight)' : String(h).padStart(2, '0') + ':00'); o.value = h; s.append(o); }
      s.value = w.hours[key];
      s.onchange = () => { w.hours[key] = +s.value; changed(false); renderWhen(); };
      return field(label, s);
    };
    row.append(mk('From', 'from', 0, 23), mk('Until', 'to', 1, 24));
    body.append(row);
    if (w.hours.from === w.hours.to) body.append(el('p', 'warnline', 'From and until can’t be the same hour.'));
    else if (w.hours.to < w.hours.from) body.append(el('p', 'hint', 'It runs past midnight.'));
  }

  const chips = (label, items, list, onToggle) => {
    const d = el('div', 'f');
    d.append(el('div', 'lbl', label));
    const c = el('div', 'chips');
    for (const [v, text] of items) {
      const b = el('button', 'chip', text);
      b.type = 'button';
      b.setAttribute('aria-pressed', list.includes(v) ? 'true' : 'false');
      b.onclick = () => { onToggle(v); changed(false); renderWhen(); };
      c.append(b);
    }
    d.append(c);
    return d;
  };
  const toggle = (arr, v) => { const i = arr.indexOf(v); if (i >= 0) arr.splice(i, 1); else arr.push(v); arr.sort((a, b) => a - b); };
  body.append(chips('Months (none picked = any month)', MONTHS_SHORT.map((m, i) => [i + 1, m]), w.months, v => toggle(w.months, v)));
  body.append(chips('Days of the week (none picked = any day)', DAY_ORDER.map(d => [d, DAYS_SHORT[d]]), w.weekdays, v => toggle(w.weekdays, v)));

  // special dates
  const dates = el('div', 'f');
  dates.append(el('div', 'lbl', 'Special dates (any of these)'));
  const ul = el('ul', 'dates');
  w.dates.forEach((d, i) => {
    const li = el('li');
    li.append(el('span', 'grow', `${d.day} ${FaceRules.MONTHS[d.month - 1]}${d.year ? ' ' + d.year : ', every year'}`));
    const rm = el('button', 'btn ghost small danger', 'Remove');
    rm.type = 'button';
    rm.onclick = () => { w.dates.splice(i, 1); changed(false); renderWhen(); };
    li.append(rm);
    ul.append(li);
  });
  dates.append(ul);
  if (w.dates.length < (limits.dates || 8)) {
    const form = el('div', 'add-date');
    const month = el('select');
    FaceRules.MONTHS.forEach((m, i) => { const o = el('option', '', m); o.value = i + 1; month.append(o); });
    const day = el('input'); day.type = 'number'; day.min = 1; day.max = 31; day.placeholder = 'Day';
    day.setAttribute('aria-label', 'Day of the month');
    const every = el('input'); every.type = 'checkbox'; every.checked = true;
    const year = el('input'); year.type = 'number'; year.min = 2000; year.max = 2100; year.value = clockParts().year; year.hidden = true;
    year.setAttribute('aria-label', 'Year');
    every.onchange = () => { year.hidden = every.checked; };
    const lab = el('label', 'check'); lab.append(every, el('span', '', 'Every year'));
    const add = el('button', 'btn ghost small', '+ Add date');
    add.type = 'button';
    add.onclick = () => {
      const d = Math.round(+day.value), m = +month.value;
      const y = every.checked ? null : Math.round(+year.value);
      if (!(d >= 1 && d <= 31) || isNaN(new Date(Date.UTC(y || 2024, m - 1, d)).getTime()) || new Date(Date.UTC(y || 2024, m - 1, d)).getUTCMonth() !== m - 1) return toast("That isn't a real date.", true);
      if (w.dates.some(x => x.month === m && x.day === d && x.year === y)) return;
      w.dates.push({ month: m, day: d, year: y });
      changed(false); renderWhen();
    };
    form.append(month, day, lab, year, add);
    dates.append(form);
  }
  body.append(dates);
}

// ---------------------------------------------------------------- the stage: an animated 320x240 preview you can drag around

const stage = $('stage');
const ctx = stage.getContext('2d');
let playing = true, t0 = performance.now(), tPaused = 0, wantDraw = true, lastFrame = 0;
let hits = [];           // what was drawn where this frame, back to front: {kind, i, x, y, w, h}
let areaHits = [];
let drag = null;

function requestDraw() { wantDraw = true; }

function sizeStage() {
  const r = stage.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 3);
  const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
  if (stage.width !== w || stage.height !== h) { stage.width = w; stage.height = h; requestDraw(); }
}
new ResizeObserver(sizeStage).observe(stage);

$('playBtn').onclick = () => {
  playing = !playing;
  const now = performance.now();
  if (playing) t0 = now - tPaused; else tPaused = now - t0;
  $('playBtn').textContent = playing ? '⏸ Pause' : '▶ Play';
  $('playBtn').setAttribute('aria-pressed', String(playing));
  requestDraw();
};

document.fonts.load('500 40px Montserrat').then(requestDraw, () => { /* falls back to the system font */ });

function frame(now) {
  requestAnimationFrame(frame);
  if (!draft || $('editView').hidden || now - lastFrame < 33) return;   // ~30 fps, like the clock's own tick is slower
  if (!playing && !wantDraw) return;
  lastFrame = now;
  wantDraw = false;
  draw(playing ? now - t0 : tPaused);
}
requestAnimationFrame(frame);

function sizesOf(hashes) { return hashes.map(h => { const m = spriteInfo(h); return { w: m.w, h: m.h }; }); }

function draw(t) {
  const sc = stage.width / SCREEN_W;
  ctx.setTransform(sc, 0, 0, sc, 0, 0);
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  ctx.globalAlpha = 1;
  ctx.fillStyle = '#1d1716';
  ctx.fillRect(0, 0, SCREEN_W, SCREEN_H);
  hits = []; areaHits = [];

  const bg = draft.background && drawable(draft.background);
  if (bg) ctx.drawImage(bg, 0, 0, SCREEN_W, SCREEN_H);

  draft.particles.forEach((p, i) => { if (p.depth === 'back') drawParticles(p, i, t); });
  draft.layers.forEach((l, i) => drawLayer(l, 'layer', i, t));
  draft.particles.forEach((p, i) => { if (p.depth !== 'back') drawParticles(p, i, t); });

  const now = clockParts();
  drawText(draft.time, FaceRules.formatTime(now), 'time');
  if (draft.date) drawText(draft.date, FaceRules.formatDate(draft.date.format, now), 'date');
  if (draft.icon) drawLayer(draft.icon, 'icon', undefined, t);
  if (sel.kind === 'np') drawNowPlaying();

  drawSelection(sc);
}

// A stand-in for the Spotify strip, drawn only while its row is selected (on the clock it appears when somebody plays).
function drawNowPlaying() {
  const np = draft.nowplaying || NP_DEFAULT;
  const x = np.x, y = np.y, w = np.w;
  ctx.save();
  ctx.globalAlpha = np.show ? 1 : 0.35;
  if (np.bg) {
    ctx.globalAlpha *= np.bg_pct / 100;
    ctx.fillStyle = np.bg;
    ctx.beginPath(); ctx.roundRect(x, y, w, NP_H, 12); ctx.fill();
    ctx.globalAlpha = np.show ? 1 : 0.35;
  }
  const half = w / 2, art = 38, pad = np.bg ? 6 : 0;
  for (let side = 0; side < 2; side++) {
    const left = side === 0;
    const hx = left ? x + pad : x + w - pad - art;
    ctx.fillStyle = '#d9667a';
    ctx.beginPath(); ctx.roundRect(hx, y + (NP_H - art) / 2, art, art, 7); ctx.fill();
    ctx.font = '500 14px Montserrat, system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.textAlign = left ? 'left' : 'right';
    const tx = left ? hx + art + 6 : hx - 6;
    ctx.save();
    ctx.beginPath(); ctx.rect(left ? x : x + half, y, half, NP_H); ctx.clip();
    ctx.fillStyle = np.title; ctx.fillText(left ? 'First song' : 'Second song', tx, y + NP_H / 2 - 9);
    ctx.fillStyle = np.artist; ctx.fillText('Artist', tx, y + NP_H / 2 + 9);
    ctx.restore();
  }
  ctx.restore();
  hits.push({ kind: 'np', x, y, w, h: NP_H });
}

function drawLayer(l, kind, i, t) {
  const img = drawable(l.sprite);
  const info = spriteInfo(l.sprite);
  for (let c = 0; c < Math.max(1, l.copies || 1); c++) {
    const p = FaceMotion.layerPlacement(l, info, c, t);
    if (img && p.opacity > 0) {
      ctx.globalAlpha = clamp(p.opacity / 100, 0, 1);
      ctx.drawImage(img, p.sx, p.sy, p.sw, p.sh, p.x, p.y, p.w, p.h);
      ctx.globalAlpha = 1;
    }
    hits.push({ kind, i, x: p.x, y: p.y, w: p.w, h: p.h });
  }
  if (l.scatter) areaHits.push({ kind, i, ...l.scatter.area });   // dragging the area moves where the copies spawn
}

function drawParticles(p, i, t) {
  const a = p.area;
  areaHits.push({ kind: 'particles', i, x: a.x, y: a.y, w: a.w, h: a.h });
  const imgs = p.sprites.map(drawable);
  const sizes = sizesOf(p.sprites);
  ctx.save();
  ctx.beginPath();
  ctx.rect(a.x, a.y, a.w, a.h);
  ctx.clip();
  for (let k = 0; k < p.count; k++) {
    const q = FaceMotion.particle(p, k, t, sizes);
    const img = imgs[q.sprite];
    if (!img || q.opacity <= 0) continue;
    ctx.globalAlpha = clamp(q.opacity / 100, 0, 1);
    ctx.drawImage(img, q.x, q.y, sizes[q.sprite].w, sizes[q.sprite].h);
  }
  ctx.restore();
}

function textRect(slot, text) {
  const size = Number.isFinite(slot.size) ? slot.size : 20;
  ctx.font = `500 ${size}px Montserrat, system-ui, sans-serif`;
  const w = ctx.measureText(text).width;
  const x = slot.align === 'left' ? slot.x : slot.align === 'right' ? slot.x - w : slot.x - w / 2;
  return { x, y: slot.y, w, h: size * TEXT_LINE, size };
}

function drawText(slot, text, kind) {
  const r = textRect(slot, text);
  ctx.textBaseline = 'alphabetic';
  ctx.textAlign = slot.align;
  const base = slot.y + TEXT_BASELINE * r.size;
  if (slot.shadow) {
    const off = Math.max(1, Math.floor(r.size / 24));
    ctx.fillStyle = slot.shadow;
    ctx.fillText(text, slot.x + off, base + off);
  }
  ctx.fillStyle = slot.color;
  ctx.fillText(text, slot.x, base);
  hits.push({ kind, ...r });
}

function selectedRects() {
  if (sel.kind === 'particles') return areaHits.filter(h => h.kind === 'particles' && h.i === sel.i);
  if (sel.kind === 'bg') return [];
  const own = hits.filter(h => h.kind === sel.kind && (sel.kind === 'time' || sel.kind === 'date' || sel.kind === 'icon' || sel.kind === 'np' || h.i === sel.i));
  const t = target();
  return t && t.scatter ? own.concat(areaHits.filter(h => h.kind === sel.kind && h.i === sel.i)) : own;
}

function drawSelection(sc) {
  const rects = selectedRects();
  ctx.save();
  ctx.globalAlpha = 1;
  for (const r of rects) {
    ctx.lineWidth = 3 / sc; ctx.strokeStyle = 'rgba(0,0,0,.55)'; ctx.setLineDash([]);
    ctx.strokeRect(r.x, r.y, r.w, r.h);
    ctx.lineWidth = 1.5 / sc; ctx.strokeStyle = '#fff'; ctx.setLineDash([5 / sc, 4 / sc]);
    ctx.strokeRect(r.x, r.y, r.w, r.h);
  }
  const areaRect = rects.find(r => areaHits.includes(r));
  if (areaRect) {
    const r = areaRect, s = 12 / sc;
    ctx.setLineDash([]);
    ctx.fillStyle = '#fff'; ctx.strokeStyle = 'rgba(0,0,0,.6)'; ctx.lineWidth = 1 / sc;
    ctx.fillRect(r.x + r.w - s / 2, r.y + r.h - s / 2, s, s);
    ctx.strokeRect(r.x + r.w - s / 2, r.y + r.h - s / 2, s, s);
  }
  ctx.restore();
}

// ---- dragging

function logical(e) {
  const r = stage.getBoundingClientRect();
  return { x: (e.clientX - r.left) * SCREEN_W / r.width, y: (e.clientY - r.top) * SCREEN_H / r.height };
}
const inside = (p, r, pad = 0) => p.x >= r.x - pad && p.x <= r.x + r.w + pad && p.y >= r.y - pad && p.y <= r.y + r.h + pad;

function pick(p) {
  for (let i = hits.length - 1; i >= 0; i--) if (inside(p, hits[i], hits[i].w < 14 || hits[i].h < 14 ? 6 : 0)) return hits[i];
  for (let i = areaHits.length - 1; i >= 0; i--) if (inside(p, areaHits[i])) return areaHits[i];
  return null;
}

stage.addEventListener('pointerdown', e => {
  if (!draft) return;
  stage.focus({ preventScroll: true });
  const p = logical(e);
  const r = stage.getBoundingClientRect();
  const px = SCREEN_W / r.width;           // logical pixels per screen pixel
  // the corner handle of the selected particle area
  const a = areaOf(target());
  if (a) {
    if (Math.hypot(p.x - (a.x + a.w), p.y - (a.y + a.h)) < 14 * px) {
      drag = { mode: 'resize', obj: a, id: e.pointerId, start: p, orig: { w: a.w, h: a.h } };
      stage.setPointerCapture(e.pointerId); stage.classList.add('dragging');
      return;
    }
  }
  const hit = pick(p);
  if (!hit) { return; }
  if (!(sel.kind === hit.kind && (hit.i === undefined || hit.i === sel.i))) select(hit.kind, hit.i);
  const obj = target();
  if (!obj) return;
  const pos = areaOf(obj) || obj;
  drag = { mode: 'move', obj: pos, kind: hit.kind, isArea: pos !== obj, id: e.pointerId, start: p, orig: { x: pos.x, y: pos.y } };
  stage.setPointerCapture(e.pointerId);
  stage.classList.add('dragging');
  e.preventDefault();
});

stage.addEventListener('pointermove', e => {
  if (!drag || drag.id !== e.pointerId) return;
  const p = logical(e);
  const dx = Math.round(p.x - drag.start.x), dy = Math.round(p.y - drag.start.y);
  if (drag.mode === 'resize') {
    drag.obj.w = clamp(drag.orig.w + dx, 8, SCREEN_W - drag.obj.x);
    drag.obj.h = clamp(drag.orig.h + dy, 8, SCREEN_H - drag.obj.y);
  } else {
    const o = drag.orig;
    let w = 8, h = 8;
    if (drag.isArea) { w = drag.obj.w; h = drag.obj.h; }
    else if (drag.kind === 'layer' || drag.kind === 'icon') { const l = target(); w = frameWidth(l); h = spriteInfo(l.sprite).h; }
    else if (drag.kind === 'np') { w = drag.obj.w; h = NP_H; }
    else { const hit = hits.find(x => x.kind === drag.kind); w = hit ? hit.w : 40; h = hit ? hit.h : 20; }
    const text = drag.kind === 'time' || drag.kind === 'date';     // x of a text is where it is anchored, not its left edge
    drag.obj.x = clamp(o.x + dx, 0, SCREEN_W - (text ? 0 : w));
    drag.obj.y = clamp(o.y + dy, 0, Math.max(0, SCREEN_H - h));
  }
  syncBound();
  changed(false);
});
const endDrag = e => { if (drag && drag.id === e.pointerId) { drag = null; stage.classList.remove('dragging'); } };
stage.addEventListener('pointerup', endDrag);
stage.addEventListener('pointercancel', endDrag);

stage.addEventListener('keydown', e => {
  const k = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
  if (!k || !target()) return;
  e.preventDefault();
  const n = e.shiftKey ? 10 : 1;
  nudge(k[0] * n, k[1] * n);
});

// ---------------------------------------------------------------- go

load();
document.addEventListener('visibilitychange', () => { if (!document.hidden && !draft && !$('listView').hidden) refresh().then(showList, () => {}); });
