#pragma once
#include <Arduino.h>

// The one page the clock serves. /info tells it which mode it's in:
//  - "setup":    on the Bhoomi-Clock hotspot -> Wi-Fi picker + time zone, "Connect"
//  - "settings": on the home network         -> time zone ("Save") + photos & sounds on the SD card
//                                               + cloud connection status and its keys (write-only)
// Networks, zones and files are fetched as JSON and rendered with textContent (no HTML escaping).
// Photos are decoded and resized in the browser (fit inside 320x240, JPEG) before upload.
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bhoomi's clock</title>
<style>
:root{--bg:#fbf7f2;--card:#fff;--ink:#2b2420;--mute:#8a7f78;--accent:#d9667a;--line:#ece4dc;--soft:#fbeef0;--ok:#3a8f5c;--err:#c0392b}
*{box-sizing:border-box}
body{margin:0;font:16px/1.4 -apple-system,system-ui,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--ink)}
main{max-width:520px;margin:0 auto;padding:28px 16px 40px}
h1{font-size:24px;margin:0 0 4px}
p.sub{color:var(--mute);margin:0 0 20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:16px}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px}
.lbl{font-size:13px;color:var(--mute);text-transform:uppercase;letter-spacing:.04em}
.meta{color:var(--mute);font-size:13px;white-space:nowrap}
button.link{background:none;border:0;color:var(--accent);font:inherit;font-size:14px;padding:4px;cursor:pointer}
ul{list-style:none;margin:8px 0 0;padding:0}
#list{max-height:260px;overflow:auto}
li{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:11px 6px;border-top:1px solid var(--line)}
li:first-child{border-top:0}
#list li{cursor:pointer}
li.sel{background:var(--soft);border-radius:10px}
li .name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;min-width:0;flex:1}
input,select{width:100%;font:inherit;padding:12px;border:1px solid var(--line);border-radius:10px;margin-top:6px;background:#fff;color:var(--ink)}
select{appearance:none;-webkit-appearance:none}
.pw{position:relative}
.pw button{position:absolute;right:6px;top:12px}
.go{width:100%;margin-top:4px;padding:14px;border:0;border-radius:12px;background:var(--accent);color:#fff;font:inherit;font-weight:600;cursor:pointer}
.go:disabled{opacity:.5}
#msg{margin:14px 0 16px;min-height:1.4em;text-align:center}
.ok{color:var(--ok)}.err{color:var(--err)}
.empty{color:var(--mute);padding:10px 6px}
.note{color:var(--mute);font-size:13px;margin-top:8px}
.drop{display:block;margin-top:12px;border:2px dashed var(--line);border-radius:14px;padding:22px 12px;text-align:center;cursor:pointer;transition:.15s}
.drop b{display:block;font-size:17px}
.drop span{color:var(--mute);font-size:14px}
.drop.over{border-color:var(--accent);background:var(--soft)}
.q{margin-top:10px;font-size:14px}
.q .row{padding:4px 0}
.bar{height:6px;border-radius:3px;background:var(--line);overflow:hidden;margin-top:4px}
.bar i{display:block;height:100%;width:0;background:var(--accent);transition:width .2s}
.sec{margin-top:18px;display:flex;justify-content:space-between;align-items:baseline}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:8px;margin-top:8px}
.ph{position:relative;aspect-ratio:4/3;border-radius:10px;overflow:hidden;background:var(--line)}
.ph img{width:100%;height:100%;object-fit:cover;display:block}
.x{position:absolute;top:4px;right:4px;width:26px;height:26px;border-radius:13px;border:0;background:rgba(0,0,0,.55);color:#fff;font-size:15px;line-height:26px;padding:0;cursor:pointer}
.snd .x{position:static;background:none;color:var(--mute);font-size:18px}
.play{border:0;background:var(--soft);color:var(--accent);width:32px;height:32px;border-radius:16px;font-size:13px;cursor:pointer;flex:none}
.dot{display:inline-block;width:9px;height:9px;border-radius:5px;background:var(--mute);margin-right:6px}
.dot.ok{background:var(--ok)}.dot.err{background:var(--err)}
details{margin-top:12px;border-top:1px solid var(--line);padding-top:10px}
summary{cursor:pointer;color:var(--accent);font-size:15px}
.k{margin-top:12px}.k .lbl{display:flex;justify-content:space-between}
.k .lbl i{font-style:normal;text-transform:none;letter-spacing:0}
#keysMsg{margin-top:10px;min-height:1.2em;text-align:center}
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

<div class="card" id="weatherCard" hidden>
  <h2>Weather</h2>
  <p id="weatherCurrent" role="status">Loading city…</p>
  <form id="weatherSearch"><label for="weatherQuery">City</label>
    <input id="weatherQuery" minlength="2" maxlength="80" placeholder="Search for a city" required>
    <button class="btn" id="weatherFind">Search</button>
  </form>
  <label for="weatherCities">Matching cities</label>
  <select id="weatherCities" aria-label="Matching cities"></select>
  <button class="btn primary" id="weatherSave" disabled>Use this city</button>
  <button class="btn ghost" id="weatherOff">Turn weather off</button>
  <p class="hint">Weather beside the date · refreshed every 15 minutes.<br>Weather data by <a href="https://open-meteo.com/" target="_blank" rel="noopener">Open-Meteo</a>.</p>
  <p id="weatherError" role="alert"></p>
</div>

<div class="card" id="files" hidden>
  <div class="row"><span class="lbl">Photos &amp; sounds</span><span class="meta" id="space"></span></div>
  <div id="noCard" class="note" hidden></div>
  <div id="filesBody">
    <label class="drop" id="drop">
      <input type="file" id="pick" multiple accept="image/*,audio/*,.mp3,.wav,.m4a,.aac,.flac,.ogg,.opus" hidden>
      <b>Drop photos or sounds here</b>
      <span>or tap to choose. Photos are resized to fit the clock's screen.</span>
    </label>
    <div class="q" id="queue"></div>
    <div class="sec"><span class="lbl">Photos</span><span class="meta" id="pCount"></span></div>
    <div class="grid" id="photos"></div>
    <div class="sec"><span class="lbl">Sounds</span><span class="meta" id="sCount"></span></div>
    <ul id="sounds"></ul>
  </div>
</div>

<div class="card" id="moodVideoCard" hidden>
  <div class="lbl">A little moment</div><h2 style="margin:4px 0 6px;font-size:20px">A video for each mood</h2>
  <p class="note">Choose a mood and a video with sound. The clock plays it after that response.</p>
  <label class="lbl" for="moodVideoMood">Mood</label>
  <select id="moodVideoMood"><option value="1">Very low</option><option value="2">Low</option><option value="3">Okay</option><option value="4">Good</option><option value="5">Great</option></select>
  <label class="lbl" for="moodVideoFile">Video with audio</label><input id="moodVideoFile" type="file" accept="video/*">
  <button class="go" id="moodVideoUpload">Prepare and upload</button>
  <p class="note">Choose a video up to 2 minutes. The first 20 seconds are prepared in your browser and saved to the clock’s SD card.</p>
  <p id="moodVideoStatus" role="status"></p>
</div>
<div class="card" id="cloud" hidden>
  <div class="row"><span class="lbl">Cloud connection</span><span class="meta" id="cloudAgo"></span></div>
  <div style="margin-top:8px"><span class="dot" id="cloudDot"></span><span id="cloudState">Checking…</span></div>
  <div class="note" id="cloudHelp"></div>
  <details id="keysBox">
    <summary>Update keys</summary>
    <form id="keysForm" autocomplete="off">
    <p class="note">The clock uses these to reach its cloud page. When one expires, paste the new one here. Fill in only what changed; saved keys are never shown again.</p>
    <div class="k"><label class="lbl" for="kId">Access Client ID <i id="hId"></i></label>
      <input id="kId" autocapitalize="none" spellcheck="false" maxlength="200" placeholder="….access"></div>
    <div class="k"><label class="lbl" for="kSecret">Access Client Secret <i id="hSecret"></i></label>
      <div class="pw"><input id="kSecret" type="password" maxlength="200" spellcheck="false">
      <button type="button" class="link" data-show="kSecret">Show</button></div></div>
    <div class="k"><label class="lbl" for="kToken">Device token <i id="hToken"></i></label>
      <div class="pw"><input id="kToken" type="password" maxlength="200" spellcheck="false">
      <button type="button" class="link" data-show="kToken">Show</button></div></div>
    <button class="go" id="keysGo" style="margin-top:14px">Save keys</button>
    <div id="keysMsg"></div>
    <div style="text-align:center"><button type="button" class="link" id="keysReset">Go back to the built-in keys</button></div>
    </form>
  </details>
</div>
</main>
<script src="/mood-video.js"></script>
<script>
const $=id=>document.getElementById(id);
const el=(tag,cls,text)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined)e.textContent=text;return e;};
let mode='setup',polling=null;

// ---------------- time zones ----------------
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
  zones.forEach(z=>{const o=el('option','',(z.off===null?'':fmtOff(z.off)+'  ')+z.city);o.value=z.id;sel.appendChild(o);});
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

// ---------------- Wi-Fi (setup mode) ----------------
function bars(r){return r>-60?'▂▄▆█':r>-70?'▂▄▆':r>-80?'▂▄':'▂';}
function render(nets){
  const ul=$('list');ul.innerHTML='';
  if(!nets.length){ul.appendChild(el('li','empty','Looking for networks…'));return;}
  nets.forEach(n=>{
    const li=el('li');
    li.append(el('span','name',n.ssid),el('span','meta',(n.open?'':'\u{1F512} ')+bars(n.rssi)));
    li.onclick=()=>{document.querySelectorAll('#list li').forEach(x=>x.classList.remove('sel'));li.classList.add('sel');
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

// ---------------- files (settings mode) ----------------
const AUDIO=/\.(mp3|wav|m4a|aac|flac|ogg|opus)$/i, IMAGE=/\.(jpe?g|png|heic|heif|webp|gif|bmp|avif)$/i;
const W=320,H=240;
function human(b){const u=['B','KB','MB','GB','TB'];let i=0;while(b>=1024&&i<u.length-1){b/=1024;i++;}return (i?b.toFixed(b<10?1:0):b)+' '+u[i];}
function clean(n){n=n.replace(/[^A-Za-z0-9._\-() ]/g,'_').replace(/^\.+/,'').trim();return n.slice(0,64)||'file';}
function fileUrl(folder,name){return '/file?'+new URLSearchParams({folder,name});}

let audio=null,playing='';
async function listFolder(folder){
  const r=await fetch('/files?folder='+folder);const j=await r.json();
  if(!j.ok)throw new Error(j.error||'Could not read the card');
  j.files.sort((a,b)=>a.name.localeCompare(b.name,undefined,{numeric:true}));
  return j;
}
async function refreshFiles(){
  try{
    const p=await listFolder('photos'), s=await listFolder('sounds');
    $('noCard').hidden=true;$('filesBody').hidden=false;
    $('space').textContent=human(p.total-p.used)+' free of '+human(p.total);
    const g=$('photos');g.innerHTML='';
    $('pCount').textContent=p.files.length?p.files.length+' photo'+(p.files.length>1?'s':''):'';
    if(!p.files.length)g.appendChild(el('div','empty','No photos yet.'));
    p.files.forEach(f=>{
      const d=el('div','ph');const img=el('img');img.loading='lazy';img.alt=f.name;img.src=fileUrl('photos',f.name);
      const x=el('button','x','×');x.title='Delete '+f.name;x.onclick=()=>del('photos',f.name);
      d.append(img,x);g.appendChild(d);
    });
    const ul=$('sounds');ul.innerHTML='';
    $('sCount').textContent=s.files.length?s.files.length+' sound'+(s.files.length>1?'s':''):'';
    if(!s.files.length)ul.appendChild(el('li','empty','No sounds yet.'));
    s.files.forEach(f=>{
      const li=el('li','snd');
      const pb=el('button','play',playing===f.name?'❚❚':'▶');pb.title='Play on this device';
      pb.onclick=()=>togglePlay(f.name);
      const x=el('button','x','×');x.title='Delete '+f.name;x.onclick=()=>del('sounds',f.name);
      li.append(pb,el('span','name',f.name),el('span','meta',human(f.size)),x);ul.appendChild(li);
    });
  }catch(e){
    $('filesBody').hidden=true;$('noCard').hidden=false;$('noCard').textContent=e.message;$('space').textContent='';
  }
}
function togglePlay(name){
  if(audio){audio.pause();audio=null;}
  if(playing===name){playing='';refreshFiles();return;}
  playing=name;audio=new Audio(fileUrl('sounds',name));audio.onended=()=>{playing='';refreshFiles();};
  audio.play().catch(()=>{playing='';});refreshFiles();
}
async function del(folder,name){
  if(!confirm('Delete '+name+' from the clock?'))return;
  try{
    const r=await fetch('/delete',{method:'POST',body:new URLSearchParams({folder,name})});
    const j=await r.json();if(!j.ok)throw new Error(j.error);
  }catch(e){alert('Could not delete: '+(e.message||'error'));}
  refreshFiles();
}

// decode (respecting EXIF rotation) and shrink to fit inside W x H, as a JPEG blob
async function decode(file){
  try{return await createImageBitmap(file,{imageOrientation:'from-image'});}catch(e){}
  return new Promise((res,rej)=>{const i=new Image();i.onload=()=>res(i);
    i.onerror=()=>rej(new Error("This browser can't open that image (HEIC? try Safari, or export as JPEG)."));
    i.src=URL.createObjectURL(file);});
}
async function resize(file){
  const src=await decode(file);
  const w=src.width,h=src.height,s=Math.min(1,W/w,H/h);
  const tw=Math.max(1,Math.round(w*s)),th=Math.max(1,Math.round(h*s));
  let cur=src,cw=w,ch=h;
  while(cw/2>=tw&&ch/2>=th){ // halve in steps: sharper than one big jump
    cw=Math.round(cw/2);ch=Math.round(ch/2);
    const c=el('canvas');c.width=cw;c.height=ch;const x=c.getContext('2d');x.imageSmoothingQuality='high';
    x.drawImage(cur,0,0,cw,ch);cur=c;
  }
  const c=el('canvas');c.width=tw;c.height=th;const x=c.getContext('2d');x.imageSmoothingQuality='high';
  x.fillStyle='#000';x.fillRect(0,0,tw,th);x.drawImage(cur,0,0,tw,th);
  return new Promise((res,rej)=>c.toBlob(b=>b?res(b):rej(new Error('Could not convert image')),'image/jpeg',0.85));
}
function send(blob,folder,name,onp){
  return new Promise((res,rej)=>{
    const fd=new FormData();fd.append('file',blob,name);
    const x=new XMLHttpRequest();
    x.open('POST','/upload?'+new URLSearchParams({folder,name,size:blob.size}));
    x.upload.onprogress=e=>{if(e.lengthComputable)onp(e.loaded/e.total);};
    x.onload=()=>{let j={};try{j=JSON.parse(x.responseText);}catch(e){}
      if(x.status===200&&j.ok)res(j);else rej(new Error(j.error||('Upload failed ('+x.status+')')));};
    x.onerror=()=>rej(new Error('Lost connection to the clock'));
    x.send(fd);
  });
}

$('moodVideoUpload').onclick=async()=>{
  const btn=$('moodVideoUpload'), status=$('moodVideoStatus'); btn.disabled=true;
  try {
    const clip=await MoodVideo.prepare($('moodVideoFile').files[0], t=>{status.textContent=t;});
    status.textContent='Uploading prepared video…';
    await send(clip,'mood-staging','mood-'+$('moodVideoMood').value+'.bcv',()=>{});
    status.textContent='Saved on the clock. This mood will play the new video.';
  }catch(e){status.textContent=e.message;}
  finally{btn.disabled=false;}
};

const jobs=[];let busy=false;
function enqueue(files){
  for(const f of files){
    const isA=(f.type||'').startsWith('audio/')||AUDIO.test(f.name);
    const isI=!isA&&((f.type||'').startsWith('image/')||IMAGE.test(f.name));
    const row=el('div');const top=el('div','row');const nm=el('span','name',f.name);const st=el('span','meta','waiting');
    const bar=el('div','bar');const fill=el('i');bar.appendChild(fill);top.append(nm,st);row.append(top,bar);
    $('queue').appendChild(row);
    if(!isA&&!isI){st.textContent='not a photo or sound';st.className='meta err';bar.remove();continue;}
    jobs.push({f,isA,st,fill,row});
  }
  run();
}
async function run(){
  if(busy)return;busy=true;
  while(jobs.length){
    const j=jobs.shift();
    try{
      let blob=j.f,name=clean(j.f.name),folder='sounds';
      if(!j.isA){
        j.st.textContent='resizing…';
        blob=await resize(j.f);folder='photos';name=clean(j.f.name.replace(/\.[^.]*$/,''))+'.jpg';
      }
      j.st.textContent='0%';
      const r=await send(blob,folder,name,p=>{j.fill.style.width=(p*100).toFixed(0)+'%';j.st.textContent=(p*100).toFixed(0)+'% of '+human(blob.size);});
      j.fill.style.width='100%';j.st.textContent='saved';j.st.className='meta ok';
      setTimeout(()=>j.row.remove(),2500);
      refreshFiles();
    }catch(e){j.st.textContent=e.message;j.st.className='meta err';j.fill.parentNode&&j.fill.parentNode.remove();}
  }
  busy=false;
}
const drop=$('drop');
['dragenter','dragover'].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.add('over');}));
['dragleave','drop'].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.remove('over');}));
drop.addEventListener('drop',e=>enqueue(e.dataTransfer.files));
$('pick').onchange=e=>{enqueue(e.target.files);e.target.value='';};
// a file dropped anywhere else shouldn't navigate away from the page
['dragover','drop'].forEach(t=>window.addEventListener(t,e=>e.preventDefault()));

// ---------------- cloud connection (settings mode) ----------------
const CLOUD={
  ok:['ok','Connected.',''],
  waiting:['','Connecting…',''],
  access:['err','Cloudflare Access is turning the clock away.','The Access service token has probably expired or been revoked. Enter a new Client ID and Client Secret under "Update keys".'],
  token:['err','The cloud page doesn\'t accept the clock\'s device token.','Enter the current device token under "Update keys".'],
  nokey:['err','No device token yet.','Enter it under "Update keys".'],
  offline:['err','Can\'t reach the cloud page right now.','Usually a Wi-Fi or internet problem. The clock keeps trying on its own.'],
  https:['err','The cloud page address is wrong (it must start with https://).','This one needs a firmware update.'],
  error:['err','The cloud page answered with an error.','The clock keeps trying on its own.'],
  off:['','Not set up on this clock.',''],
};
function ago(s){if(s===null||s===undefined)return '';if(s<60)return s+' s ago';if(s<3600)return Math.floor(s/60)+' min ago';
  if(s<172800)return Math.floor(s/3600)+' h ago';return Math.floor(s/86400)+' days ago';}
function keyHint(k){if(!k.set)return 'not set';return (k.custom?'saved here':'built-in')+(k.hint?' · '+k.hint:'');}
let cloudTimer=null;
async function loadCloud(soon){
  clearTimeout(cloudTimer);
  try{
    const j=await (await fetch('/portal')).json();
    const c=CLOUD[j.state]||CLOUD.error;
    $('cloudDot').className='dot '+c[0];
    $('cloudState').textContent=c[1]+(j.state!=='ok'&&j.code?' ('+j.code+')':'');
    $('cloudHelp').textContent=c[2]+(j.state!=='ok'&&j.okAgo!==null?' Last connected '+ago(j.okAgo)+'.':'');
    $('cloudAgo').textContent=j.ago!==null?'checked '+ago(j.ago):'';
    $('hId').textContent=keyHint(j.keys.accessId);$('hSecret').textContent=keyHint(j.keys.accessSecret);$('hToken').textContent=keyHint(j.keys.token);
    $('keysReset').hidden=!(j.keys.token.custom||j.keys.accessId.custom||j.keys.accessSecret.custom);
    if(j.state==='access'||j.state==='token'||j.state==='nokey')$('keysBox').open=true;
  }catch(e){$('cloudState').textContent='Couldn\'t ask the clock.';$('cloudDot').className='dot';}
  cloudTimer=setTimeout(()=>loadCloud(false),soon?2000:10000);
}
function keysSay(t,c){const m=$('keysMsg');m.textContent=t;m.className=c||'';}
async function saveKeys(body){
  $('keysGo').disabled=true;keysSay('Saving…');
  try{
    const r=await fetch('/portal',{method:'POST',headers:{'X-Clock':'1'},body});
    const j=await r.json();if(!r.ok||j.ok===false)throw new Error(j.error||'Could not save.');
    ['kId','kSecret','kToken'].forEach(i=>$(i).value='');
    keysSay('Saved! The clock is reconnecting with them…','ok');
    loadCloud(true);
  }catch(e){keysSay(e.message||'Could not save.','err');}
  $('keysGo').disabled=false;
}
$('keysForm').onsubmit=e=>{e.preventDefault();
  const b=new URLSearchParams();
  [['kId','accessId'],['kSecret','accessSecret'],['kToken','token']].forEach(([i,n])=>{const v=$(i).value.trim();if(v)b.append(n,v);});
  if(!b.toString()){keysSay('Fill in the key that changed.','err');return;}
  if(b.has('accessId')!==b.has('accessSecret')&&!confirm('Usually the Client ID and Client Secret change together. Save just the one?'))return;
  saveKeys(b);
};
$('keysReset').onclick=()=>{if(confirm('Forget the keys saved here and use the ones the clock came with?'))saveKeys(new URLSearchParams({reset:'1'}));};
document.querySelectorAll('[data-show]').forEach(b=>b.onclick=()=>{const p=$(b.dataset.show);const s=p.type==='password';p.type=s?'text':'password';b.textContent=s?'Hide':'Show';});

// ---------------- start ----------------
(async()=>{
  let info={mode:'setup',tz:''};
  try{info=await (await fetch('/info')).json();}catch(e){}
  mode=info.mode;
  if(mode==='settings'){
    $('title').textContent='Clock settings';
    $('sub').textContent=info.clock?'The clock currently reads '+info.clock+'.':'Adjust how your clock shows the time.';
    $('wifiList').hidden=true;$('wifiCreds').hidden=true;$('go').textContent='Save time zone';
    $('weatherCard').hidden=false;
    $('files').hidden=false;$('moodVideoCard').hidden=false;refreshFiles();
    $('cloud').hidden=false;loadCloud(false);
  }else{
    loadNets(false);
  }
  loadZones(info.tz).catch(()=>{$('tz').innerHTML='<option value="UTC">UTC</option>';});
})();
</script>
<script>
// Shared city picker behavior for the admin and LAN settings pages.
(() => {
  const local = true;
  const get = id => document.getElementById(id);
  const report = text => { get('weatherError').textContent = text; };
  let results = [];
  async function request(url, options) {
    const r = await fetch(url, options);
    const j = await r.json();
    if (!r.ok) throw new Error(j.error || 'Weather settings are unavailable.');
    return j;
  }
  async function refresh() {
    try {
      const j = await request(local ? '/weather' : '/api/weather/location');
      get('weatherCurrent').textContent = local
        ? (j.pending ? 'Waiting for server: ' + (j.pendingCity || 'weather off') + (j.code && j.code !== 204 ? ' (will retry)' : '') : j.city || 'Weather is off')
        : j.location ? j.location.name : 'Weather is off';
    } catch (e) { report(e.message); }
  }
  get('weatherSearch').onsubmit = async e => {
    e.preventDefault(); report('');
    get('weatherFind').disabled = true;
    get('weatherSave').disabled = true;
    results = []; get('weatherCities').replaceChildren();
    try {
      const query = encodeURIComponent(get('weatherQuery').value.trim());
      const url = local ? 'https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=' + query : '/api/weather/cities?q=' + query;
      const j = await request(url, {signal: AbortSignal.timeout(10000)});
      results = (j.results || []).map(c => ({name: [c.name, c.admin1, c.country].filter(Boolean).join(', ').slice(0, 80), latitude: c.latitude, longitude: c.longitude}));
      results.forEach((c, i) => { const o = document.createElement('option'); o.value = i; o.textContent = c.name; get('weatherCities').append(o); });
      get('weatherSave').disabled = !results.length;
      if (!results.length) report('No cities found. Try another spelling.');
    } catch (e) { report('City search failed. Please try again.'); }
    finally { get('weatherFind').disabled = false; }
  };
  async function save(loc) {
    get('weatherSave').disabled = true; get('weatherOff').disabled = true; report('');
    try {
      await request(local ? '/weather' : '/api/weather/location', local
        ? {method: 'POST', headers: {'X-Clock': '1'}, body: new URLSearchParams(loc || {name:'',latitude:0,longitude:0})}
        : {method: 'PUT', headers: {'content-type':'application/json'}, body: JSON.stringify({location:loc})});
      await refresh();
      if (!local) report('Saved. The clock will pick it up on its next weather check.');
    } catch (e) { report(e.message); }
    finally { get('weatherSave').disabled = !results.length; get('weatherOff').disabled = false; }
  }
  get('weatherSave').onclick = () => { const c = results[Number(get('weatherCities').value)]; if (c) save(c); };
  get('weatherOff').onclick = () => save(null);
  if (local) { refresh(); setInterval(refresh, 10000); }
  else { window.loadWeatherSettings = refresh; }
})();
</script></body></html>
)HTML";
