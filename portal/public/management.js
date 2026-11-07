(() => {
  const el = id => document.getElementById(id);
  let candidate;
  async function api(path, method='GET', body) {
    const r = await fetch('/api/management'+path, {method, body:body instanceof File ? body : body === undefined ? undefined : JSON.stringify(body), headers:body instanceof File ? {'content-type':'application/octet-stream'} : {'content-type':'application/json'}});
    const j = await r.json(); if (!r.ok) throw Error(j.error || 'Request failed'); return j;
  }
  async function refresh() {
    const j = await api(''); candidate = j.release;
    el('diagnosticInterval').value = j.settings.interval;
    el('clockHealthSummary').textContent=j.diagnostics ? `Firmware ${j.diagnostics.firmware} · Wi-Fi ${j.diagnostics.rssi} dBm · Received ${new Date(j.diagnostics.at*1000).toLocaleString()}` : 'No report received yet.';
    el('clockDiagnostics').textContent = j.diagnostics ? JSON.stringify({...j.diagnostics, received:new Date(j.diagnostics.at*1000).toLocaleString()}, null, 2) : 'No report received yet.';
    const d=j.diagnostics || {};
    const state=j.approved && d.failed===j.approved.release ? 'Trial failed; previous firmware restored' : j.approved && d.installed===j.approved.release && !d.ota_state ? 'Installed and healthy' : d.ota_state ? 'Trial running startup checks' : j.approved ? 'Install requested' : 'Awaiting install';
    el('firmwareCandidate').textContent = candidate ? `${candidate.size} bytes · ${candidate.release}\n${state}` : 'No firmware uploaded.';
    el('installFirmware').disabled = !candidate;
  }
  async function run(fn) { try {await fn(); el('managementResult').textContent='Saved. Allow a minute for the clock to check in.'; await refresh();} catch(e) {el('managementResult').textContent=e.message;} }
  el('saveDiagnostics').onclick=()=>run(()=>api('/settings','PUT',{interval:Number(el('diagnosticInterval').value)}));
  el('uploadFirmware').onclick=()=>run(()=>{const f=el('firmwareFile').files[0]; if(!f) throw Error('Choose an application .bin first.'); if(f.size>6291456) throw Error('Firmware exceeds 6 MB.'); return api('/firmware','PUT',f);});
  el('installFirmware').onclick=()=>run(()=>api('/install','POST',{release:candidate.release}));
  el('cancelFirmware').onclick=()=>run(()=>api('/install','DELETE'));
  window.loadManagement=()=>refresh().catch(e=>{el('managementResult').textContent=e.message;});
})();
