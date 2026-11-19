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
  el('uploadFirmware').onclick=()=>run(async()=>{
    const f=el('firmwareFile').files[0], sidecar=el('firmwareSignature').files[0];
    if(!f || !sidecar) throw Error('Choose an application .bin and its matching signature .json.');
    if(f.size>6291456 || sidecar.size>2048) throw Error('Firmware or signature file is too large.');
    const signed=JSON.parse(await sidecar.text());
    const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',await f.arrayBuffer())),b=>b.toString(16).padStart(2,'0')).join('');
    if(signed.release!==digest || signed.size!==f.size) throw Error('Signature metadata does not match this firmware.');
    const uploaded=await api('/firmware','PUT',f);
    for(let attempt=0;attempt<7;attempt++) {
      try {await api('/firmware/'+uploaded.release+'/signature','PUT',{version:signed.version,signature:signed.signature});return;}
      catch(e) {if(attempt===6) throw e; await new Promise(resolve=>setTimeout(resolve,15000));}
    }
  });
  el('installFirmware').onclick=()=>run(()=>api('/install','POST',{release:candidate.release}));
  el('cancelFirmware').onclick=()=>run(()=>api('/install','DELETE'));
  window.loadManagement=()=>refresh().catch(e=>{el('managementResult').textContent=e.message;});
})();
