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

test('weather upgrade preserves IDs, edited base settings, order and pin',async()=>{
  const button={},status={},writes=[],settings=[];
  const entries=Array.from({length:9},(_,i)=>({key:String(i),name:`Panda ${i}`}));
  const saved=entries.map((e,i)=>({id:`existing${i}`,name:e.name,background:'custom',when:{hours:{from:i,to:i+1}},nowplaying:{bg:'#abcdef'}}));
  const context={
    document:{getElementById:id=>id==='installPandas'?button:status},
    PandaCollection:{sprites:{},weatherSprites:{},entries,build:e=>({name:e.name,background:'preset',when:{hours:null},weather:{cloudy:{background:'cloud'},rain:{background:'storm'}}})},
    FaceAssets:{makeBackground:async()=>({blob:new Uint8Array([1])})},
    fetch:async()=>({ok:true,blob:async()=>new Uint8Array([1])}),
    api:async p=>p==='/api/faces'?{faces:[...saved],limits:{faces:12}}:{asset:{hash:'abc'}},
    sendJson:async(p,method,body)=>{
      if(p==='/api/face-check')return {ok:true};
      if(p==='/api/face-settings'){settings.push(body);return {};}
      writes.push({p,method,body});return {face:body};
    },refresh:async()=>{},showList:()=>{},
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../../public/panda-install.js'),'utf8'),context);
  await button.onclick();
  assert.equal(writes.length,9,status.textContent);assert.equal(settings.length,0);
  for(let i=0;i<9;i++){
    assert.equal(writes[i].p,`/api/faces/existing${i}`);assert.equal(writes[i].method,'PUT');
    assert.equal(writes[i].body.background,'custom');
    assert.deepEqual(writes[i].body.when,saved[i].when);
    assert.deepEqual(writes[i].body.nowplaying,saved[i].nowplaying);
    assert.ok(writes[i].body.weather.rain);
  }
});
