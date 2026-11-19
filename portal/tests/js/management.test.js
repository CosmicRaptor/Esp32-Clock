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
 const ctx={window:{},document:{getElementById:el},File,crypto:require('node:crypto').webcrypto,Uint8Array,fetch:async(url,opts)=>{calls.push({url,opts});return {ok:true,json:async()=>url.endsWith('/firmware') ? {release:'a'.repeat(64),size:3000} : reply};}};
 vm.runInNewContext(source,ctx);
 return {el,calls,window:ctx.window,File};
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
test('Signed upload rejects mismatched metadata before sending firmware',async()=>{
 const p=page(),image=new p.File();
 Object.assign(image,{size:256,arrayBuffer:async()=>new Uint8Array(256).buffer});
 p.el('firmwareFile').files=[image];
 p.el('firmwareSignature').files=[{size:200,text:async()=>JSON.stringify({release:'wrong',size:256})}];
 await p.el('uploadFirmware').onclick();
 assert.match(p.el('managementResult').textContent,/does not match/);
 assert.equal(p.calls.length,0);
});
test('Signed upload sends binary and detached metadata without installation',async()=>{
 const p=page(),image=new p.File(),bytes=new Uint8Array(256);
 const digest=require('node:crypto').createHash('sha256').update(bytes).digest('hex');
 Object.assign(image,{size:256,arrayBuffer:async()=>bytes.buffer});
 p.el('firmwareFile').files=[image];
 p.el('firmwareSignature').files=[{size:300,text:async()=>JSON.stringify({release:digest,size:256,version:'0.11.2',signature:'30'.repeat(70)})}];
 await p.el('uploadFirmware').onclick();
 assert.equal(p.calls[0].url,'/api/management/firmware');
 assert.equal(p.calls[1].url,'/api/management/firmware/'+'a'.repeat(64)+'/signature');
 assert.equal(JSON.parse(p.calls[1].opts.body).version,'0.11.2');
 assert.ok(p.calls.every(call=>call.opts.method!=='POST'));
});
test('Diagnostic interval, cancellation, and missing-file feedback',async()=>{
 const p=page();await p.window.loadManagement();
 p.el('diagnosticInterval').value='300';await p.el('saveDiagnostics').onclick();
 assert.equal(JSON.parse(p.calls.find(c=>c.opts.method==='PUT').opts.body).interval,300);
 await p.el('cancelFirmware').onclick();assert.ok(p.calls.some(c=>c.opts.method==='DELETE'));
 await p.el('uploadFirmware').onclick();assert.match(p.el('managementResult').textContent,/Choose an application/);
});
