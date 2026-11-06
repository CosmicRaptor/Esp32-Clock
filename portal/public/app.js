// Bhoomi clock admin page. Plain JS, no build step. All text goes in via textContent.
'use strict';

const $ = id => document.getElementById(id);
const el = (tag, cls, text) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};

const SCREEN_W = 320, SCREEN_H = 240;
const MSG_MAX = 120;
const DAY_NAMES = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const DAY_ORDER = [1, 2, 3, 4, 5, 6, 0]; // shown Monday first; values match C's tm_wday

let state = null;   // the server's state document
let device = null;  // what the clock last reported, or null
let skew = 0;       // server clock minus ours, in seconds

// ---------------------------------------------------------------- api

async function api(path, opts = {}) {
  const r = await fetch(path, { credentials: 'same-origin', ...opts });
  let j = {};
  try { j = await r.json(); } catch (e) { /* empty body */ }
  if (r.status === 401 && path !== '/api/login') { showLogin(); throw new Error('Please log in again.'); }
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
  toast.timer = setTimeout(() => { t.className = 'toast' + (bad ? ' bad' : ''); }, bad ? 4000 : 2400);
}

// Disable a button while an action runs; errors become a toast.
async function busy(btn, fn) {
  btn.disabled = true;
  try { return await fn(); }
  catch (e) { toast(e.message, true); }
  finally { btn.disabled = false; }
}

// ---------------------------------------------------------------- time helpers

function nowSec() { return Date.now() / 1000 + skew; }

function ago(sec) {
  const d = Math.max(0, nowSec() - sec);
  if (d < 60) return 'just now';
  if (d < 3600) return `${Math.floor(d / 60)} min ago`;
  if (d < 86400) { const h = Math.floor(d / 3600); return `${h} hour${h > 1 ? 's' : ''} ago`; }
  const n = Math.floor(d / 86400);
  return `${n} day${n > 1 ? 's' : ''} ago`;
}

function validTz(tz) {
  try { new Intl.DateTimeFormat('en', { timeZone: tz }); return true; } catch (e) { return false; }
}
function clockTz() {
  return device && device.tz && validTz(device.tz) ? device.tz : Intl.DateTimeFormat().resolvedOptions().timeZone;
}
// {date: 'YYYY-MM-DD', time: 'HH:MM'} as the clock sees it right now
function clockNow() {
  const parts = Object.fromEntries(new Intl.DateTimeFormat('en-CA', {
    timeZone: clockTz(), year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
  }).formatToParts(new Date(nowSec() * 1000)).map(p => [p.type, p.value]));
  return { date: `${parts.year}-${parts.month}-${parts.day}`, time: `${parts.hour}:${parts.minute}` };
}
function addDays(iso, n) {
  const d = new Date(iso + 'T00:00:00Z');
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
}
// the next date (clock's time zone) on which HH:MM still lies ahead
function nextDateFor(time) {
  const now = clockNow();
  return time > now.time ? now.date : addDays(now.date, 1);
}
function prettyDate(iso) {
  const now = clockNow();
  if (iso === now.date) return 'today';
  if (iso === addDays(now.date, 1)) return 'tomorrow';
  return new Date(iso + 'T12:00:00Z').toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'short', timeZone: 'UTC' });
}

// ---------------------------------------------------------------- boot / login

function showLogin() {
  $('app').hidden = true;
  $('login').hidden = false;
  $('password').focus();
}

$('loginForm').onsubmit = async e => {
  e.preventDefault();
  $('loginErr').textContent = '';
  await busy($('loginBtn'), async () => {
    try {
      await sendJson('/api/login', 'POST', { password: $('password').value });
    } catch (err) { $('loginErr').textContent = err.message; return; }
    $('password').value = '';
    await load();
  });
};

$('logout').onclick = async () => {
  try { await api('/api/logout', { method: 'POST' }); } catch (e) { /* ignore */ }
  showLogin();
};

async function load() {
  let j;
  try { j = await api('/api/state'); }
  catch (e) { if (!$('login').hidden) return; toast(e.message, true); return; }
  skew = j.now - Date.now() / 1000;
  device = j.device;
  setState(j.state);
  $('login').hidden = true;
  $('app').hidden = false;
  loadSpotify();
}

function setState(s) {
  state = s;
  renderStatus();
  renderPreview();
  renderPhoto();
  renderMessage();
  renderAlarms();
}

// ---------------------------------------------------------------- status + preview

function renderStatus() {
  const st = $('status');
  let text;
  if (!device || !device.seen) {
    text = "The clock hasn't checked in yet";
    st.classList.remove('on');
  } else {
    const fresh = nowSec() - device.seen < 15 * 60; // it reports at most every 10 min
    st.classList.toggle('on', fresh);
    text = fresh ? 'Clock online' : `Clock last seen ${ago(device.seen)}`;
    if (fresh && typeof device.rssi === 'number' && device.rssi < -85) text += ' · weak Wi-Fi';
  }
  st.querySelector('span').textContent = text;
  $('alarmTz').textContent = device && device.tz ? `Times in ${device.tz.split('/').pop().replace(/_/g, ' ')} time` : '';
}

function renderPreview() {
  const img = $('pvImg');
  if (state.image) { img.src = `/api/image/${state.image.id}`; img.hidden = false; }
  else { img.hidden = true; img.removeAttribute('src'); }
  const text = $('msgText').value.trim() || (state.message ? state.message.text : '');
  $('pvMsg').textContent = text;
  $('pvMsg').hidden = !text;
  tickPreview();
}
function tickPreview() {
  if (!state) return;
  $('pvTime').textContent = clockNow().time;
}
setInterval(() => { tickPreview(); if (state) renderStatus(); }, 15000);

// ---------------------------------------------------------------- photo

function renderPhoto() {
  const has = !!state.image;
  const img = $('photoImg');
  if (has) { img.src = `/api/image/${state.image.id}`; }
  img.hidden = !has;
  $('photoEmpty').hidden = has;
  $('removePhoto').hidden = !has;
  $('photoMeta').textContent = has ? `Sent ${ago(state.image.at)}` : '';
}

$('pick').onchange = e => { const f = e.target.files[0]; e.target.value = ''; if (f) openCropper(f); };
$('choose').onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('pick').click(); } };

// drop a photo anywhere on the card
const photoCard = $('photoCard');
['dragenter', 'dragover'].forEach(t => photoCard.addEventListener(t, e => { e.preventDefault(); photoCard.classList.add('drag'); }));
['dragleave', 'drop'].forEach(t => photoCard.addEventListener(t, e => { e.preventDefault(); photoCard.classList.remove('drag'); }));
photoCard.addEventListener('drop', e => {
  const f = [...e.dataTransfer.files].find(f => f.type.startsWith('image/') || /\.(heic|heif)$/i.test(f.name));
  if (f) openCropper(f); else toast("That doesn't look like a photo.", true);
});
['dragover', 'drop'].forEach(t => window.addEventListener(t, e => e.preventDefault()));

$('removePhoto').onclick = () => busy($('removePhoto'), async () => {
  if (!confirm('Remove the photo from the clock?')) return;
  const j = await api('/api/image', { method: 'DELETE' });
  setState(j.state);
  toast('Photo removed');
});

// ---- cropper: pan/zoom a photo inside a 4:3 frame, like a profile-picture picker ----

const crop = {
  src: null, iw: 0, ih: 0,  // source (canvas or bitmap) and its size
  fw: 0, fh: 0,             // frame size in CSS px
  min: 1, s: 1,             // scale: min = "cover", s = current
  ox: 0, oy: 0,             // image's top-left corner in frame px
  ptrs: new Map(),
};
const MAX_ZOOM = 5;
const canvas = $('cropCanvas');
const ctx = canvas.getContext('2d');

async function decodeImage(file) {
  let img;
  try { img = await createImageBitmap(file, { imageOrientation: 'from-image' }); }
  catch (e) {
    img = await new Promise((res, rej) => {
      const i = new Image();
      i.onload = () => res(i);
      i.onerror = () => rej(new Error("This browser can't open that photo. Try exporting it as a JPEG."));
      i.src = URL.createObjectURL(file);
    });
  }
  // keep the working copy modest: smooth dragging, and still 5x the screen at full zoom
  let src = img, w = img.width, h = img.height;
  while (Math.max(w, h) > 2400) {
    w = Math.round(w / 2); h = Math.round(h / 2);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const x = c.getContext('2d');
    x.imageSmoothingQuality = 'high';
    x.drawImage(src, 0, 0, w, h);
    src = c;
  }
  return { src, w, h };
}

async function openCropper(file) {
  let d;
  try { d = await decodeImage(file); } catch (e) { toast(e.message, true); return; }
  Object.assign(crop, { src: d.src, iw: d.w, ih: d.h });
  $('photoView').hidden = true;
  $('cropView').hidden = false;
  sizeCanvas();
  crop.s = crop.min;
  crop.ox = (crop.fw - crop.iw * crop.s) / 2;
  crop.oy = (crop.fh - crop.ih * crop.s) / 2;
  drawCrop();
  photoCard.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function closeCropper() {
  $('cropView').hidden = true;
  $('photoView').hidden = false;
  crop.src = null;
}

function sizeCanvas() {
  const r = canvas.getBoundingClientRect();
  const oldFw = crop.fw, zoom = crop.s / crop.min;
  const dpr = window.devicePixelRatio || 1;
  crop.fw = r.width; crop.fh = r.height;
  canvas.width = Math.round(r.width * dpr);
  canvas.height = Math.round(r.height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  crop.min = Math.max(crop.fw / crop.iw, crop.fh / crop.ih);
  if (oldFw) { // keep the same framing after a resize
    const k = crop.fw / oldFw;
    crop.ox *= k; crop.oy *= k; crop.s = crop.min * zoom;
  }
}

function clampCrop() {
  crop.s = Math.min(Math.max(crop.s, crop.min), crop.min * MAX_ZOOM);
  crop.ox = Math.min(0, Math.max(crop.fw - crop.iw * crop.s, crop.ox));
  crop.oy = Math.min(0, Math.max(crop.fh - crop.ih * crop.s, crop.oy));
}

function drawCrop() {
  if (!crop.src) return;
  clampCrop();
  ctx.imageSmoothingQuality = 'high';
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, crop.fw, crop.fh);
  ctx.drawImage(crop.src, crop.ox, crop.oy, crop.iw * crop.s, crop.ih * crop.s);
  $('zoom').value = Math.round(100 * Math.log(crop.s / crop.min) / Math.log(MAX_ZOOM));
}

// zoom so the image point under (fx, fy) stays put
function zoomAt(s, fx, fy) {
  s = Math.min(Math.max(s, crop.min), crop.min * MAX_ZOOM);
  crop.ox = fx - (fx - crop.ox) * s / crop.s;
  crop.oy = fy - (fy - crop.oy) * s / crop.s;
  crop.s = s;
  drawCrop();
}

function framePoint(e) {
  const r = canvas.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
}
function pinch() {
  const [a, b] = [...crop.ptrs.values()];
  return { d: Math.hypot(a.x - b.x, a.y - b.y), x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
}

canvas.addEventListener('pointerdown', e => {
  canvas.setPointerCapture(e.pointerId);
  crop.ptrs.set(e.pointerId, framePoint(e));
  $('cropFrame').classList.add('grabbing');
});
canvas.addEventListener('pointermove', e => {
  if (!crop.ptrs.has(e.pointerId)) return;
  const p = framePoint(e);
  if (crop.ptrs.size === 1) {
    const q = crop.ptrs.get(e.pointerId);
    crop.ox += p.x - q.x; crop.oy += p.y - q.y;
    crop.ptrs.set(e.pointerId, p);
    drawCrop();
  } else if (crop.ptrs.size === 2) {
    const before = pinch();
    crop.ptrs.set(e.pointerId, p);
    const after = pinch();
    crop.ox += after.x - before.x; crop.oy += after.y - before.y;
    zoomAt(crop.s * after.d / Math.max(before.d, 1), after.x, after.y);
  }
});
const endPointer = e => {
  crop.ptrs.delete(e.pointerId);
  if (!crop.ptrs.size) $('cropFrame').classList.remove('grabbing');
};
canvas.addEventListener('pointerup', endPointer);
canvas.addEventListener('pointercancel', endPointer);
canvas.addEventListener('wheel', e => {
  e.preventDefault();
  const p = framePoint(e);
  zoomAt(crop.s * Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0025)), p.x, p.y); // ctrlKey = trackpad pinch
}, { passive: false });
$('zoom').oninput = e => zoomAt(crop.min * Math.pow(MAX_ZOOM, e.target.value / 100), crop.fw / 2, crop.fh / 2);
window.addEventListener('resize', () => { if (crop.src) { sizeCanvas(); drawCrop(); } });

// the framed area, resampled to exactly 320x240 (halving first keeps it sharp), as a baseline JPEG
async function exportCrop() {
  const sx = -crop.ox / crop.s, sy = -crop.oy / crop.s;
  const sw = crop.fw / crop.s, sh = crop.fh / crop.s;
  let w = Math.round(sw), h = Math.round(sh);
  let cur = document.createElement('canvas');
  cur.width = w; cur.height = h;
  let x = cur.getContext('2d');
  x.imageSmoothingQuality = 'high';
  x.drawImage(crop.src, sx, sy, sw, sh, 0, 0, w, h);
  while (w / 2 >= SCREEN_W && h / 2 >= SCREEN_H) {
    w = Math.round(w / 2); h = Math.round(h / 2);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    x = c.getContext('2d');
    x.imageSmoothingQuality = 'high';
    x.drawImage(cur, 0, 0, w, h);
    cur = c;
  }
  const out = document.createElement('canvas');
  out.width = SCREEN_W; out.height = SCREEN_H;
  x = out.getContext('2d');
  x.imageSmoothingQuality = 'high';
  x.fillStyle = '#000';
  x.fillRect(0, 0, SCREEN_W, SCREEN_H);
  x.drawImage(cur, 0, 0, SCREEN_W, SCREEN_H);
  for (const q of [0.88, 0.78, 0.65]) {
    const blob = await new Promise(res => out.toBlob(res, 'image/jpeg', q));
    if (!blob) throw new Error("This browser couldn't make a JPEG.");
    if (blob.size < 180000 || q === 0.65) return blob;
  }
}

$('cropCancel').onclick = closeCropper;
$('cropSend').onclick = () => busy($('cropSend'), async () => {
  const blob = await exportCrop();
  const j = await api('/api/image', { method: 'PUT', headers: { 'content-type': 'image/jpeg' }, body: blob });
  closeCropper();
  setState(j.state);
  toast('Photo sent to the clock ✨');
});

// ---------------------------------------------------------------- message

function renderMessage() {
  const m = state.message;
  $('msgMeta').textContent = m ? `Sent ${ago(m.at)}` : '';
  $('msgClear').hidden = !m;
  if (document.activeElement !== $('msgText')) $('msgText').value = m ? m.text : '';
  msgInput();
}

function msgInput() {
  const t = $('msgText').value;
  $('msgCount').textContent = `${t.length}/${MSG_MAX}`;
  $('msgWarn').textContent = /\p{Extended_Pictographic}/u.test(t) ? "Emoji may not show up on the clock yet." : '';
  const sent = state && state.message ? state.message.text : '';
  $('msgSend').disabled = t.trim() === sent;
  const preview = t.trim() || sent;
  $('pvMsg').textContent = preview;
  $('pvMsg').hidden = !preview;
}
$('msgText').oninput = msgInput;

$('msgForm').onsubmit = e => {
  e.preventDefault();
  busy($('msgSend'), async () => {
    const text = $('msgText').value;
    const j = await sendJson('/api/message', 'PUT', { text });
    $('msgText').blur();
    setState(j.state);
    toast(text.trim() ? 'Message sent ✨' : 'Message cleared');
  });
};
$('msgClear').onclick = () => busy($('msgClear'), async () => {
  const j = await sendJson('/api/message', 'PUT', { text: '' });
  $('msgText').value = '';
  setState(j.state);
  toast('Message cleared');
});

// ---------------------------------------------------------------- alarms

function repeatText(a) {
  const d = a.days;
  if (!d.length) {
    const now = clockNow();
    const past = a.date < now.date || (a.date === now.date && a.time <= now.time);
    return past ? `Was ${prettyDate(a.date)} (done)` : `Once, ${prettyDate(a.date)}`;
  }
  const key = d.join(',');
  if (key === '0,1,2,3,4,5,6') return 'Every day';
  if (key === '1,2,3,4,5') return 'Weekdays';
  if (key === '0,6') return 'Weekends';
  return DAY_ORDER.filter(x => d.includes(x)).map(x => DAY_NAMES[x].slice(0, 3)).join(', ');
}

function renderAlarms() {
  const ul = $('alarms');
  ul.textContent = '';
  if (!state.alarms.length) ul.appendChild(el('li', 'empty-row', 'No alarms yet.'));
  for (const a of state.alarms) {
    const li = el('li', a.enabled ? '' : 'off');
    const main = el('div', 'a-main');
    main.append(el('div', 'a-time', a.time), el('div', 'a-sub', [a.label, repeatText(a)].filter(Boolean).join(' · ')));
    const sw = el('label', 'switch');
    const box = el('input');
    box.type = 'checkbox'; box.checked = a.enabled; box.setAttribute('role', 'switch');
    box.setAttribute('aria-label', `Alarm at ${a.time}`);
    sw.append(box, el('span'));
    sw.onclick = e => e.stopPropagation();
    box.onchange = async () => {
      box.disabled = true;
      try {
        const j = await sendJson(`/api/alarms/${a.id}`, 'PUT', { ...a, enabled: box.checked });
        setState(j.state);
      } catch (e) { box.checked = !box.checked; box.disabled = false; toast(e.message, true); }
    };
    li.append(main, sw);
    li.onclick = () => openEditor(a);
    ul.appendChild(li);
  }
  $('addAlarm').hidden = !$('editor').hidden;
}

// ---- editor ----
let editing = null; // alarm being edited, or {} for a new one
const dayChips = DAY_ORDER.map(d => {
  const b = el('button', 'chip', DAY_NAMES[d][0]);
  b.type = 'button';
  b.dataset.day = d;
  b.setAttribute('aria-label', DAY_NAMES[d]);
  b.setAttribute('aria-pressed', 'false');
  b.onclick = () => { b.setAttribute('aria-pressed', b.getAttribute('aria-pressed') === 'true' ? 'false' : 'true'); syncDate(); };
  $('aDays').appendChild(b);
  return b;
});
const pickedDays = () => dayChips.filter(b => b.getAttribute('aria-pressed') === 'true').map(b => +b.dataset.day).sort();
function setDays(days) { dayChips.forEach(b => b.setAttribute('aria-pressed', days.includes(+b.dataset.day) ? 'true' : 'false')); syncDate(); }
function syncDate() {
  const once = !pickedDays().length;
  $('aDateRow').hidden = !once;
  if (once && !$('aDate').value) $('aDate').value = nextDateFor($('aTime').value || '00:00');
}
document.querySelectorAll('[data-preset]').forEach(b => {
  b.onclick = () => {
    const p = b.dataset.preset;
    if (!p) $('aDate').value = '';
    setDays(p ? p.split(',').map(Number) : []);
  };
});
$('aTime').onchange = () => {
  // a one-off alarm whose date was auto-picked follows the time across midnight
  if (!pickedDays().length && editing && !editing.id) $('aDate').value = nextDateFor($('aTime').value || '00:00');
};

function fillSounds(current) {
  const sel = $('aSound');
  sel.textContent = '';
  const def = el('option', '', 'Gentle beep (built in)');
  def.value = '';
  sel.appendChild(def);
  const sounds = (device && device.sounds) || [];
  for (const s of sounds) { const o = el('option', '', s.replace(/\.[^.]+$/, '')); o.value = s; sel.appendChild(o); }
  if (current && !sounds.includes(current)) {
    const o = el('option', '', `${current} (not on the card)`);
    o.value = current;
    sel.appendChild(o);
  }
  sel.value = current || '';
  $('aSoundNote').textContent = sounds.length ? '' : 'Sounds on the clock\'s memory card will show up here once the clock reports them.';
}

function openEditor(a) {
  editing = a || {};
  const now = clockNow();
  // new alarms start at the next round hour
  const next = `${String((+now.time.slice(0, 2) + 1) % 24).padStart(2, '0')}:00`;
  $('aTime').value = a ? a.time : next;
  $('aDate').value = a ? a.date : '';
  $('aLabel').value = a ? a.label : '';
  setDays(a ? a.days : [1, 2, 3, 4, 5]);
  fillSounds(a ? a.sound : '');
  $('aDelete').hidden = !a;
  $('aSave').textContent = a ? 'Save' : 'Add alarm';
  $('editor').hidden = false;
  $('addAlarm').hidden = true;
  $('editor').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}
function closeEditor() {
  editing = null;
  $('editor').hidden = true;
  $('addAlarm').hidden = false;
}

$('addAlarm').onclick = () => openEditor(null);
$('aCancel').onclick = closeEditor;
$('editor').onsubmit = e => {
  e.preventDefault();
  busy($('aSave'), async () => {
    const days = pickedDays();
    const body = {
      time: $('aTime').value,
      days,
      date: days.length ? '' : $('aDate').value,
      label: $('aLabel').value,
      sound: $('aSound').value,
      enabled: editing.id ? editing.enabled : true,
    };
    if (!body.time) throw new Error('Pick a time.');
    if (!days.length && !body.date) throw new Error('Pick a date, or some days to repeat on.');
    if (!days.length && !editing.id) body.enabled = true;
    const j = editing.id
      ? await sendJson(`/api/alarms/${editing.id}`, 'PUT', body)
      : await sendJson('/api/alarms', 'POST', body);
    const isNew = !editing.id;
    closeEditor();
    setState(j.state);
    toast(isNew ? `Alarm set for ${body.time} ⏰` : 'Alarm saved');
  });
};
$('aDelete').onclick = () => busy($('aDelete'), async () => {
  if (!editing || !editing.id || !confirm(`Delete the ${editing.time} alarm?`)) return;
  const j = await api(`/api/alarms/${editing.id}`, { method: 'DELETE' });
  closeEditor();
  setState(j.state);
  toast('Alarm deleted');
});

// ---------------------------------------------------------------- music (Spotify)

// Per person: who is connected, and what they're playing right now (the server asks Spotify).
async function loadSpotify() {
  let j;
  try { j = await api('/api/spotify'); } catch (e) { return; }
  $('spSetup').hidden = j.configured;
  const ul = $('spSides');
  ul.textContent = '';
  j.sides.forEach((s, i) => {
    const li = el('li');
    const main = el('div', 's-main');
    const name = el('div', 's-name', s.name);
    name.append(el('small', '', i === 0 ? 'left' : 'right'));
    let sub, live = false;
    if (!s.connected) sub = 'Not connected';
    else if (!s.playing) sub = 'Connected · nothing playing';
    else {
      live = s.playing.status === 'playing';
      sub = `${live ? '♪' : '⏸'} ${s.playing.title}${s.playing.artist ? ' · ' + s.playing.artist : ''}`;
    }
    const subEl = el('div', 's-sub' + (live ? ' live' : ''), sub);
    main.append(name, subEl);
    li.append(main);
    if (j.configured) {
      // a plain link: the server answers with a redirect to Spotify's consent page
      const a = el('a', 'btn ghost small', s.connected ? 'Reconnect' : 'Connect');
      a.href = `/api/spotify/login?side=${s.side}`;
      li.append(a);
    }
    if (s.connected) {
      const b = el('button', 'btn ghost small danger', 'Disconnect');
      b.onclick = () => busy(b, async () => {
        if (!confirm(`Disconnect ${s.name}'s Spotify from the clock?`)) return;
        await api(`/api/spotify/${s.side}`, { method: 'DELETE' });
        toast('Disconnected');
        loadSpotify();
      });
      li.append(b);
    }
    ul.append(li);
  });
  const connected = j.sides.filter(s => s.connected).length;
  $('spMeta').textContent = connected ? `${connected} of 2 connected` : '';
}

// Spotify's consent page sends the browser back here with ?spotify=<result>
(function spotifyResult() {
  const r = new URLSearchParams(location.search).get('spotify');
  if (!r) return;
  history.replaceState(null, '', location.pathname);
  const msg = {
    ok: ['Spotify connected 🎶'],
    denied: ['Spotify said no (access wasn\'t granted).', true],
    bad_state: ['That Spotify link expired. Please tap Connect again.', true],
    failed: ["Couldn't finish connecting to Spotify. Try again?", true],
  }[r];
  if (msg) toast(...msg);
})();

// ---------------------------------------------------------------- go

load();
// pick up the clock's check-ins (and changes from another tab) when coming back to the page
document.addEventListener('visibilitychange', () => { if (!document.hidden && !$('app').hidden && $('editor').hidden && !crop.src) load(); });
