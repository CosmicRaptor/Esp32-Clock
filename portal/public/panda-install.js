// One-time collection setup inside the existing Faces editor.
(function () {
'use strict';
const installBtn=document.getElementById('installPandas');
const statusEl=document.getElementById('pandaStatus');
const lim={sprite_frame:160,sprite_pixels:51200,sprite_h:160,sprite_w:1280};
const prepared={};
const json=sendJson;
async function source(name) {
  const r = await fetch(`/panda-collection/${name}.png`);
  if (!r.ok) throw new Error(`Could not load ${name}.`);
  return r.blob();
}
// Trim transparent padding before shrinking: the visible accent gets the intended pixel size.
async function trimmed(blob) {
  const img = await createImageBitmap(blob), c = document.createElement('canvas');
  c.width=img.width;c.height=img.height;
  const x=c.getContext('2d');x.drawImage(img,0,0);
  const a=x.getImageData(0,0,c.width,c.height).data;
  let l=c.width,r=0,t=c.height,b=0;
  for(let y=0;y<c.height;y++) for(let xx=0;xx<c.width;xx++) if(a[(y*c.width+xx)*4+3]>8){l=Math.min(l,xx);r=Math.max(r,xx);t=Math.min(t,y);b=Math.max(b,y);}
  if(l>r) throw new Error('An accent image is empty.');
  const out=document.createElement('canvas');out.width=r-l+1;out.height=b-t+1;
  out.getContext('2d').drawImage(c,l,t,out.width,out.height,0,0,out.width,out.height);
  img.close();
  return new Promise(resolve=>out.toBlob(resolve,'image/png'));
}
// Content-addressed uploads and matching collection names/backgrounds allow interrupted installs to resume.
async function install(){
  installBtn.disabled=true;
  try{
    const overview=await api('/api/faces');
    const newRefs={};
    for(const [key,a] of Object.entries(prepared)){
      statusEl.textContent=`Saving artwork ${key}…`;
      const sprite=!!a.bin;
      const r=await api(`/api/assets/${sprite?'sprite':'bg'}`,{method:'PUT',headers:{'content-type':sprite?'application/octet-stream':'image/jpeg'},body:sprite?a.bin:a.blob});
      newRefs[key]=r.asset.hash;
      if(sprite)await api(`/api/assets/${r.asset.hash}.png`,{method:'PUT',headers:{'content-type':'image/png'},body:a.png});
    }
    const drafts=PandaCollection.entries.map(e=>PandaCollection.build(e,newRefs));
    // Keep each existing face ID, base artwork, schedule and Spotify settings when adding weather scenes.
    const existing=drafts.map(f=>{const matches=overview.faces.filter(old=>old.name===f.name);if(matches.length>1)throw new Error(`More than one face is named ${f.name}; rename one before adding variations.`);return matches[0];});
    const upgrading=existing.every(Boolean);
    for(let i=0;i<drafts.length;i++) if(existing[i] && drafts[i].weather) drafts[i]={...existing[i],weather:drafts[i].weather};
    for(const f of drafts){const check=await json('/api/face-check','POST',f);if(!check.ok)throw new Error(`${f.name}: ${check.errors.join(' ')}`);}
    const needed=existing.filter(f=>!f).length;
    if(overview.faces.length+needed>overview.limits.faces)throw new Error(`This collection needs ${needed} free face slots. Existing faces have been kept.`);
    const ids=[];
    for(let i=0;i<drafts.length;i++){
      statusEl.textContent=`Adding ${drafts[i].name}…`;
      if(existing[i]) {
        if(drafts[i].weather) await json(`/api/faces/${existing[i].id}`,'PUT',drafts[i]);
        ids.push(existing[i].id);
      } else ids.push((await json('/api/faces','POST',drafts[i])).face.id);
    }
    const latest=await api('/api/faces');
    if(!upgrading) await json('/api/face-settings','PUT',{order:[...ids,...latest.faces.map(f=>f.id).filter(id=>!ids.includes(id))],default:ids[7],pinned:''});
    statusEl.textContent=upgrading?'Weather variations added to all nine faces. Your schedules and pin are unchanged.':'All nine faces are saved. Daily schedule active; Paris sunset with cat is the fallback.';
    installBtn.textContent='Collection installed';
    await refresh(); showList(); installBtn.hidden=true;
  }catch(e){statusEl.textContent=e.message;installBtn.disabled=false;}
}

installBtn.onclick=async()=>{
  installBtn.disabled=true;
  try {
    for(const [name,max] of Object.entries({...PandaCollection.sprites,...PandaCollection.weatherSprites})) {
      statusEl.textContent=`Preparing ${name}…`;
      prepared[name]=await FaceAssets.makeSprite(await trimmed(await source(name)),1,lim,max);
    }
    for(const e of PandaCollection.entries) {
      statusEl.textContent=`Preparing ${e.name}…`;
      prepared[e.key]=await FaceAssets.makeBackground(await source(e.key));
    }
    for(const e of PandaCollection.entries) for(const kind of ['cloudy','rain']) {
      statusEl.textContent=`Preparing ${e.name}: ${kind}…`;
      prepared[e.key+'-'+kind]=await FaceAssets.makeBackground(await source('weather/'+e.key+'-'+kind));
    }
    await install();
  } catch(e) {statusEl.textContent=e.message;installBtn.disabled=false;}
};
})();
