const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

test('Faces page installer uses its API helpers and retains old faces when activating the collection', async () => {
  const button = {}, status = {}, saved = [{id:'old',name:'Paris 1'}], settings = [];
  const entries = Array.from({length:9},(_,i)=>({key:String(i),name:`Panda ${i}`}));
  const context = {
    document:{getElementById:id=>id==='installPandas'?button:status},
    PandaCollection:{sprites:{},entries,build:(e,refs)=>({name:e.name,background:refs[e.key],when:{hours:null}})},
    FaceAssets:{makeBackground:async()=>({blob:new Uint8Array([1])})},
    fetch:async()=>({ok:true,blob:async()=>new Uint8Array([1])}),
    api:async p=>p==='/api/faces'?{faces:[...saved],limits:{faces:12}}:{asset:{hash:'abc'}},
    sendJson:async(p,method,body)=>{
      if(p==='/api/face-check')return {ok:true};
      if(p==='/api/face-settings'){settings.push(body);return {};}
      const f={...body,id:`id${saved.length}`};saved.push(f);return {face:f};
    },
    refresh:async()=>{},showList:()=>{},
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../../public/panda-install.js'),'utf8'),context);
  await button.onclick();
  assert.equal(saved.length,10,status.textContent);
  assert.equal(settings.length,1,status.textContent);
  assert.equal(settings[0].default,'id8');
  assert.equal(settings[0].pinned,'');
  assert.equal(settings[0].order.at(-1),'old');
  assert.equal(button.hidden,true);
  assert.match(status.textContent,/All nine faces are saved/);
});
