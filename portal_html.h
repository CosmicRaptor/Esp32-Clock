#pragma once
#include <Arduino.h>

// Single-page setup UI served by the captive portal.
// Networks are fetched as JSON and rendered with textContent, so SSIDs never need HTML escaping.
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock setup</title>
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
input{width:100%;font:inherit;padding:12px;border:1px solid var(--line);border-radius:10px;margin-top:6px;background:#fff;color:var(--ink)}
.pw{position:relative}
.pw button{position:absolute;right:6px;top:12px}
.go{width:100%;margin-top:16px;padding:14px;border:0;border-radius:12px;background:var(--accent);color:#fff;font:inherit;font-weight:600}
.go:disabled{opacity:.5}
#msg{margin-top:14px;min-height:1.4em;text-align:center}
.ok{color:var(--ok)}.err{color:var(--err)}
.empty{color:var(--mute);padding:10px 6px}
</style></head><body><main>
<h1>Hello! &#x1F570;</h1>
<p class="sub">Let's get your clock online so it always knows the time.</p>

<div class="card">
  <div class="row"><span class="lbl">Wi-Fi networks</span><button class="link" id="rescan">Refresh</button></div>
  <ul id="list"><li class="empty">Looking for networks…</li></ul>
</div>

<form class="card" id="f">
  <label class="lbl" for="ssid">Network name</label>
  <input id="ssid" name="ssid" autocomplete="off" autocapitalize="none" required maxlength="32">
  <label class="lbl" for="pass" style="display:block;margin-top:12px">Password</label>
  <div class="pw"><input id="pass" name="pass" type="password" maxlength="63" autocomplete="off">
  <button type="button" class="link" id="show">Show</button></div>
  <button class="go" id="go">Connect</button>
  <div id="msg"></div>
</form>
</main>
<script>
const $=id=>document.getElementById(id);
let polling=null;
function bars(r){return r>-60?'▂▄▆█':r>-70?'▂▄▆':r>-80?'▂▄':'▂';}
function render(nets){
  const ul=$('list');ul.innerHTML='';
  if(!nets.length){const li=document.createElement('li');li.className='empty';li.textContent='No networks found yet. Tap Refresh.';ul.appendChild(li);return;}
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
async function load(refresh){
  try{
    const r=await fetch('/scan'+(refresh?'?refresh=1':''));const j=await r.json();
    render(j.networks);
    if(j.scanning)setTimeout(()=>load(false),1500);
  }catch(e){setTimeout(()=>load(false),2000);}
}
$('rescan').onclick=()=>{$('list').innerHTML='<li class="empty">Scanning…</li>';load(true);};
$('show').onclick=()=>{const p=$('pass');const s=p.type==='password';p.type=s?'text':'password';$('show').textContent=s?'Hide':'Show';};
function say(t,c){const m=$('msg');m.textContent=t;m.className=c||'';}
$('f').onsubmit=async e=>{
  e.preventDefault();
  $('go').disabled=true;say('Connecting…');
  const body=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value});
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
load(false);
</script></body></html>
)HTML";
