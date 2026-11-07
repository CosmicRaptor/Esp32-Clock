const test=require('node:test'),assert=require('node:assert/strict');
const Weather=require('../../public/face-weather.js');
const Panda=require('../../public/panda-collection.js');
const Motion=require('../../public/face-motion.js');

test('weather freshness falls back safely for missing, future and expired observations',()=>{
  assert.equal(Weather.current(null,10000),'default');
  assert.equal(Weather.current({code:97,observed_at:1000},10000),'rain');
  assert.equal(Weather.current({code:45,observed_at:1000},11800),'cloudy');
  assert.equal(Weather.current({code:45,observed_at:1000},11801),'default');
  assert.equal(Weather.current({code:65,observed_at:10001},10000),'default');
});
test('all nine weather sets replace default particles and have genuinely dynamic lightning',()=>{
  for(const e of Panda.entries){
    const refs=Object.fromEntries([...Object.keys(Panda.sprites),...Object.keys(Panda.weatherSprites),e.key,e.key+'-cloudy',e.key+'-rain'].map(k=>[k,k]));
    const face=Panda.build(e,refs),rain=Weather.scene(face,'rain');
    assert.equal(rain.background,e.key+'-rain');
    assert.equal(Weather.scene(face,'default'),face);
    assert.equal(rain.time.color,'#fff4ee');
    assert.ok(!rain.particles.some(p=>p.sprites.includes('snowflake')));
    const bolt=rain.layers[0],phase=bolt.motion[0].phase;
    assert.equal(bolt.motion[0].period,1000);assert.equal(bolt.scatter.period,1000);
    const first=Motion.copyOrigin(bolt,0,11,42,0),same=Motion.copyOrigin(bolt,0,11,42,999),next=Motion.copyOrigin(bolt,0,11,42,1000);
    assert.deepEqual(first,same);assert.notDeepEqual(first,next);
    const visible=Motion.layerPlacement(bolt,{w:22,h:42},0,bolt.motion[0].period-phase);
    const hidden=Motion.layerPlacement(bolt,{w:22,h:42},0,bolt.motion[0].period+500-phase);
    assert.ok(visible.opacity>0,e.name);assert.equal(hidden.opacity,0,e.name);
    assert.ok(visible.x>=0&&visible.y>=0&&visible.x+visible.w<=320&&visible.y+visible.h<=240);
    if(e.key==='03'||e.key==='07')assert.ok(rain.particles.at(-1).area.w<130);
  }
});
