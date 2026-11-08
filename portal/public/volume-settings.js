// Shared controls for the remote admin and the clock's LAN settings page.
(() => {
  'use strict';
  const get = id => document.getElementById(id);
  const local = document.documentElement.dataset.volumeMode === 'local';
  const alarm = get('volumeAlarm'), video = get('volumeVideo'), status = get('volumeStatus');
  let dirty = false, saving = false, started = false;
  const update = () => { get('volumeAlarmValue').textContent = `${alarm.value} / 21`; get('volumeVideoValue').textContent = `${video.value} / 21`; };
  alarm.oninput = video.oninput = () => { dirty = true; update(); };
  async function read() {
    const r = await fetch(local ? '/volume' : '/api/volume', {credentials:'same-origin'});
    const j = await r.json(); if (!r.ok) throw Error(j.error || 'Could not load volume.');
    const v = local ? j : j.volume;
    if (dirty || saving) return;
    alarm.value = v.alarm; video.value = v.video; update();
    status.textContent = local && v.pending ? 'Saved on the clock; waiting to sync to the portal.' : '';
  }
  get('volumeSave').onclick = async () => {
    const button = get('volumeSave'); button.disabled = true; saving = true; status.textContent = 'Saving…';
    try {
      const values = {alarm:Number(alarm.value), video:Number(video.value)};
      const r = await fetch(local ? '/volume' : '/api/volume', local
        ? {method:'POST',headers:{'X-Clock':'1'},body:new URLSearchParams(values)}
        : {method:'PUT',credentials:'same-origin',headers:{'content-type':'application/json'},body:JSON.stringify(values)});
      const j = await r.json(); if (!r.ok) throw Error(j.error || 'Could not save volume.');
      dirty = false;
      status.textContent = local ? 'Saved on the clock; waiting to sync to the portal.' : 'Saved. The clock will update when it connects.';
    } catch (e) { status.textContent = e.message; }
    finally { button.disabled = false; saving = false; }
  };
  window.loadVolumeSettings = () => { started = true; return read().catch(e => { status.textContent = e.message; }); };
  setInterval(() => { if (started && !dirty && !saving) read().catch(() => {}); }, 10000);
  if (local) window.loadVolumeSettings();
})();
