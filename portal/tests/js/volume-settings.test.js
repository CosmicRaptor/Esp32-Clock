const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../../public/volume-settings.js'), 'utf8');
function setup(local) {
  const nodes = Object.fromEntries(['volumeAlarm','volumeVideo','volumeAlarmValue','volumeVideoValue','volumeSave','volumeStatus'].map(id => [id, {value:'',textContent:'',disabled:false}]));
  const requests=[];
  const context={document:{documentElement:{dataset:{volumeMode:local?'local':'remote'}},getElementById:id=>nodes[id]},window:{},URLSearchParams,setInterval:()=>0,
    fetch:async (url, options={})=>{requests.push({url,options});return {ok:true,json:async()=>local?{alarm:6,video:10,pending:true}:{volume:{alarm:6,video:10}}}}};
  vm.runInNewContext(source,context);return {nodes,requests,context};
}
test('remote controls load distinct volumes and submit both', async () => {
  const {nodes,requests,context}=setup(false);
  await context.window.loadVolumeSettings();
  assert.equal(nodes.volumeAlarm.value,6); assert.equal(nodes.volumeVideo.value,10);
  nodes.volumeAlarm.value='4';nodes.volumeVideo.value='19';await nodes.volumeSave.onclick();
  assert.equal(requests[1].url,'/api/volume');assert.deepEqual(JSON.parse(requests[1].options.body),{alarm:4,video:19});
});
test('local save uses X-Clock and reports pending sync', async () => {
  const {nodes,requests}=setup(true);
  nodes.volumeAlarm.value='0';nodes.volumeVideo.value='15';await nodes.volumeSave.onclick();
  const save=requests.find(x=>x.options.method==='POST');
  assert.equal(save.url,'/volume'); assert.equal(save.options.headers['X-Clock'],'1');
  assert.match(nodes.volumeStatus.textContent,/waiting to sync/);
});
test('refresh does not overwrite a slider being edited', async () => {
  const {nodes,context}=setup(false);
  await context.window.loadVolumeSettings();
  nodes.volumeAlarm.value='2';nodes.volumeAlarm.oninput();
  await context.window.loadVolumeSettings();
  assert.equal(nodes.volumeAlarm.value,'2');
});
