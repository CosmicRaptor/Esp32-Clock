const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../../public/management.js'),'utf8');
function page() {
 const elements=new Map(),calls=[];
 const el=id=>{if(!elements.has(id))elements.set(id,{value:'',textContent:'',disabled:false,files:[]});return elements.get(id);};
 let reply={settings:{interval:3600},release:{release:'a'.repeat(64),size:3000},approved:null,diagnostics:null};
 class File {}
 const ctx={window:{},document:{getElementById:el},File,fetch:async(url,opts)=>{calls.push({url,opts});return {ok:true,json:async()=>reply};}};
 vm.runInNewContext(source,ctx);
 return {el,calls,window:ctx.window};
}
test('Upload does not install; install sends the displayed immutable digest',async()=>{
 const p=page();await p.window.loadManagement();
 assert.equal(p.el('installFirmware').disabled,false);
 assert.equal(p.calls.length,1);
 await p.el('installFirmware').onclick();
 const call=p.calls.find(c=>c.opts.method==='POST');
 assert.equal(call.url,'/api/management/install');
 assert.equal(JSON.parse(call.opts.body).release,'a'.repeat(64));
});
test('Diagnostic interval, cancellation, and missing-file feedback',async()=>{
 const p=page();await p.window.loadManagement();
 p.el('diagnosticInterval').value='300';await p.el('saveDiagnostics').onclick();
 assert.equal(JSON.parse(p.calls.find(c=>c.opts.method==='PUT').opts.body).interval,300);
 await p.el('cancelFirmware').onclick();assert.ok(p.calls.some(c=>c.opts.method==='DELETE'));
 await p.el('uploadFirmware').onclick();assert.match(p.el('managementResult').textContent,/Choose an application/);
});
