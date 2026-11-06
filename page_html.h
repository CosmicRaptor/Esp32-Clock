#pragma once
#include <Arduino.h>

// The one page the clock serves. /info tells it which mode it's in:
//  - "setup":    on the Bhoomi-Clock hotspot -> Wi-Fi picker + time zone, "Connect"
//  - "settings": on the home network         -> time zone only, "Save"
// Networks and zones are fetched as JSON and rendered with textContent (no HTML escaping needed).
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bhoomi's clock</title>
<style>
:root{--bg:#fbf7f2;--card:#fff;--ink:#2b2420;--mute:#8a7f78;--accent:#d9667a;--line:#ece4dc;--ok:#3a8f5c;--err:#c0392b}
*{box-sizing:border-box}
body{margin:0;font:16px/1.4 -apple-system,system-ui,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--ink)}
main{max-width:420px;margin:0 auto;padding:28px 16px 40px}
h1{font-size:24px;margin:0 0 4px}
p.sub{color:var(--mute);margin:0 0 20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:16px}
.row{display:flex;justify-content:space-between;align-items:center}
.lbl{font-size:13px;color:var(--mute);text-transform:uppercase;letter-spacing:.04em}
button.link{background:none;border:0;color:var(--accent);font:inherit;font-size:14px;padding:4px}
ul{list-style:none;margin:8px 0 0;padding:0;max-height:260px;overflow:auto}
li{display:flex;justify-content:space-between;padding:11px 6px;border-top:1px solid var(--line);cursor:pointer}
li:first-child{border-top:0}
li.sel{background:#fbeef0;border-radius:10px}
li .meta{color:var(--mute);font-size:13px;white-space:nowrap;padding-left:8px}
input,select{width:100%;font:inherit;padding:12px;border:1px solid var(--line);border-radius:10px;margin-top:6px;background:#fff;color:var(--ink)}
select{appearance:none;-webkit-appearance:none}
.pw{position:relative}
.pw button{position:absolute;right:6px;top:12px}
.go{width:100%;margin-top:4px;padding:14px;border:0;border-radius:12px;background:var(--accent);color:#fff;font:inherit;font-weight:600}
.go:disabled{opacity:.5}
#msg{margin-top:14px;min-height:1.4em;text-align:center}
.ok{color:var(--ok)}.err{color:var(--err)}
.empty{color:var(--mute);padding:10px 6px}
.note{color:var(--mute);font-size:13px;margin-top:8px}
[hidden]{display:none!important}
</style></head><body><main>
<h1 id="title">Hello! &#x1F570;</h1>
<p class="sub" id="sub">Let's get your clock online so it always knows the time.</p>

<form id="f">
<div class="card" id="wifiList">
  <div class="row"><span class="lbl">Wi-Fi networks</span><button type="button" class="link" id="rescan">Refresh</button></div>
  <ul id="list"><li class="empty">Looking for networks…</li></ul>
</div>

<div class="card" id="wifiCreds">
  <label class="lbl" for="ssid">Network name</label>
  <input id="ssid" autocomplete="off" autocapitalize="none" maxlength="32">
  <label class="lbl" for="pass" style="display:block;margin-top:12px">Password</label>
  <div class="pw"><input id="pass" type="password" maxlength="63" autocomplete="off">
  <button type="button" class="link" id="show">Show</button></div>
</div>

<div class="card">
  <label class="lbl" for="tz">Time zone</label>
  <select id="tz"><option>Loading…</option></select>
  <div class="note" id="tzNote"></div>
</div>

<button class="go" id="go">Connect</button>
<div id="msg"></div>
</form>
</main>
<script>
const $=id=>document.getElementById(id);
let mode='setup',polling=null;

// ---- time zones ----
const ALIAS={'Asia/Calcutta':'Asia/Kolkata','Asia/Saigon':'Asia/Ho_Chi_Minh','Europe/Kiev':'Europe/Kyiv',
  'Asia/Katmandu':'Asia/Kathmandu','Asia/Rangoon':'Asia/Yangon','America/Buenos_Aires':'America/Argentina/Buenos_Aires',
  'Etc/UTC':'UTC','Etc/GMT':'UTC','GMT':'UTC','Asia/Tel_Aviv':'Asia/Jerusalem','Australia/ACT':'Australia/Sydney'};
function offMin(id){
  try{
    const p=new Intl.DateTimeFormat('en-US',{timeZone:id,timeZoneName:'longOffset'}).formatToParts(new Date());
    const v=p.find(x=>x.type==='timeZoneName').value; // "GMT+05:30" / "GMT"
    const m=v.match(/([+-])(\d+):?(\d*)/); if(!m)return 0;
    return (m[1]==='-'?-1:1)*(parseInt(m[2])*60+parseInt(m[3]||'0'));
  }catch(e){return null}
}
function fmtOff(m){if(m===null)return '';const s=m<0?'-':'+';m=Math.abs(m);
  return 'UTC'+s+String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');}
function phoneTz(){try{const z=Intl.DateTimeFormat().resolvedOptions().timeZone;return ALIAS[z]||z;}catch(e){return ''}}

async function loadZones(saved){
  const zones=await (await fetch('/zones')).json();
  zones.forEach(z=>z.off=offMin(z.id));
  zones.sort((a,b)=>(a.off??1e9)-(b.off??1e9)||a.city.localeCompare(b.city));
  const sel=$('tz');sel.innerHTML='';
  zones.forEach(z=>{const o=document.createElement('option');o.value=z.id;
    o.textContent=(z.off===null?'':fmtOff(z.off)+'  ')+z.city;sel.appendChild(o);});
  const mine=phoneTz();
  let pick=saved||'';
  if(!pick&&zones.some(z=>z.id===mine))pick=mine;
  if(!pick){const myOff=-new Date().getTimezoneOffset();const m=zones.find(z=>z.off===myOff);if(m)pick=m.id;}
  if(pick)sel.value=pick;
  const note=$('tzNote');
  if(saved)note.textContent='';
  else if(pick&&pick===mine)note.textContent='Picked from your phone. Change it if the clock lives elsewhere.';
  else note.textContent='Pick the city closest to where the clock lives.';
}

// ---- wifi list (setup mode) ----
function bars(r){return r>-60?'▂▄▆█':r>-70?'▂▄▆':r>-80?'▂▄':'▂';}
function render(nets){
  const ul=$('list');ul.innerHTML='';
  if(!nets.length){const li=document.createElement('li');li.className='empty';li.textContent='Looking for networks…';ul.appendChild(li);return;}
  nets.forEach(n=>{
    const li=document.createElement('li');
    const a=document.createElement('span');a.textContent=n.ssid;
    const b=document.createElement('span');b.className='meta';b.textContent=(n.open?'':'\u{1F512} ')+bars(n.rssi);
    li.append(a,b);
    li.onclick=()=>{document.querySelectorAll('li').forEach(x=>x.classList.remove('sel'));li.classList.add('sel');
      $('ssid').value=n.ssid;$('pass').value='';if(n.open){$('pass').placeholder='(open network)';}else{$('pass').placeholder='';$('pass').focus();}};
    ul.appendChild(li);
  });
}
async function loadNets(refresh){
  try{
    const j=await (await fetch('/scan'+(refresh?'?refresh=1':''))).json();
    render(j.networks);
    if(j.scanning||!j.networks.length)setTimeout(()=>loadNets(false),1500);
  }catch(e){setTimeout(()=>loadNets(false),2000);}
}
$('rescan').onclick=()=>{$('list').innerHTML='<li class="empty">Scanning…</li>';loadNets(true);};
$('show').onclick=()=>{const p=$('pass');const s=p.type==='password';p.type=s?'text':'password';$('show').textContent=s?'Hide':'Show';};

function say(t,c){const m=$('msg');m.textContent=t;m.className=c||'';}

$('f').onsubmit=async e=>{
  e.preventDefault();
  if(mode==='settings'){
    $('go').disabled=true;say('Saving…');
    try{
      const r=await fetch('/tz',{method:'POST',body:new URLSearchParams({tz:$('tz').value})});
      if(!r.ok)throw 0;
      const i=await (await fetch('/info')).json();
      say('Saved! ✨'+(i.clock?' The clock now reads '+i.clock+'.':''),'ok');$('tzNote').textContent='';
    }catch(e){say('Could not save. Please try again.','err');}
    $('go').disabled=false;return;
  }
  if(!$('ssid').value){say('Pick a network first.','err');return;}
  $('go').disabled=true;say('Connecting…');
  const body=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,tz:$('tz').value});
  try{await fetch('/connect',{method:'POST',body});}catch(e){}
  poll();
};
function poll(){
  clearTimeout(polling);
  polling=setTimeout(async()=>{
    try{
      const j=await (await fetch('/status')).json();
      if(j.state==='connecting'){say('Connecting to '+j.ssid+'…');poll();}
      else if(j.state==='connected'){say('Connected! ✨ The clock is online. You can close this page.','ok');}
      else if(j.state==='failed'){say(j.reason||'Could not connect.','err');$('go').disabled=false;}
      else{poll();}
    }catch(e){say('Connecting… (your phone may briefly reconnect to the clock)');poll();}
  },1000);
}

(async()=>{
  let info={mode:'setup',tz:''};
  try{info=await (await fetch('/info')).json();}catch(e){}
  mode=info.mode;
  if(mode==='settings'){
    $('title').textContent='Clock settings';
    $('sub').textContent=info.clock?'The clock currently reads '+info.clock+'.':'Adjust how your clock shows the time.';
    $('wifiList').hidden=true;$('wifiCreds').hidden=true;$('go').textContent='Save';
  }else{
    loadNets(false);
  }
  loadZones(info.tz).catch(()=>{$('tz').innerHTML='<option value="UTC">UTC</option>';});
})();
</script></body></html>
)HTML";
