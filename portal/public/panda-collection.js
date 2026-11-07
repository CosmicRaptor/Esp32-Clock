// The approved nine-face collection. Uses only motions already supported by the clock.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PandaCollection = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  const sprites = { petal: 18, snowflake: 10, steam: 32, 'shooting-star': 44, leaf: 15, twinkle: 14 };
  const entries = [
    ['01', 'Paris blossoms/beret', 12, 15, 'petal', 195, 28, 40, '#28151c'],
    ['02', 'Snowy sleeping panda', 5, 7, 'snowflake', 194, 25, 40, '#fff4ee'],
    ['03', 'Café/mug', 9, 12, 'steam', 115, 91, 37, '#fff0d8'],
    ['04', 'Paris night drive', 20, 22, 'shooting-star', 207, 29, 40, '#fff4ee'],
    ['05', 'Autumn bench', 15, 18, 'leaf', 209, 29, 39, '#29160e'],
    ['06', 'Cloud stargazing', 22, 24, 'shooting-star', 211, 28, 40, '#fff4ee'],
    ['07', 'Bedtime', 0, 5, 'twinkle', 207, 34, 40, '#fff4ee'],
    ['08', 'Paris sunset with cat', null, null, 'twinkle', 197, 26, 39, '#fff4ee'],
    ['09', 'Sleepy cherry blossoms', 7, 9, 'petal', 219, 30, 38, '#28151c'],
  ].map(([key, name, from, to, accent, x, y, size, color]) => ({key, name, from, to, accent, x, y, size, color}));
  // Pill, title, artist: blossom pink, winter violet, café cream, and each scene's own evening colours.
  const musicThemes = [
    ['#ffe0ed','#4c203a','#9c496e'],['#312b62','#fff4ff','#d7c5ff'],
    ['#f9dfb1','#66351f','#9f603c'],['#262342','#fff0db','#c8b4ef'],
    ['#ffdfad','#62331f','#a65a34'],['#443261','#fff3ef','#e6c6ff'],
    ['#3b3158','#fff0d2','#d6b9ed'],['#56365c','#fff1e5','#f4bad8'],
    ['#ffe3ef','#52263e','#a84f79'],
  ];
  function build(e, refs) {
    const lower = e.key === '03' || e.key === '07';
    const timeY = lower ? e.y : 62;
    const theme = musicThemes[entries.indexOf(e)];
    const f = {name: e.name, background: refs[e.key],
      time: {x:e.x,y:timeY,size:e.size,color:e.color,shadow:'',align:'center'},
      date: {x:e.x,y:timeY + Math.ceil(e.size * 1.219) + 3,size:14,color:e.color,shadow:'',align:'center',format:'short'},
      icon:null,layers:[],particles:[],nowplaying:{show:true,x:8,y:lower?187:7,w:304,bg:theme[0],bg_pct:90,title:theme[1],artist:theme[2]},
      when:{hours:e.from === null ? null : {from:e.from,to:e.to},months:[],weekdays:[],dates:[]}};
    const particle = (kind, count, area, min, max, sway, period) => ({kind,count,area,sprites:[refs[e.accent]],speed_min:min,speed_max:max,sway,period,phase:0,depth:'front',seed:parseInt(e.key,10)*117});
    if (e.accent === 'petal' || e.accent === 'leaf') f.particles.push(particle('fall',7,{x:8,y:0,w:304,h:236},9,17,7,7000));
    if (e.accent === 'snowflake') f.particles.push(particle('fall',16,{x:5,y:0,w:310,h:235},8,18,4,7000));
    if (e.accent === 'twinkle') f.particles.push(particle('twinkle',7,{x:15,y:8,w:280,h:e.key === '07' ? 100 : 130},0,0,0,4500));
    if (e.accent === 'steam') f.particles.push(particle('rise',3,{x:222,y:157,w:28,h:32},5,8,2,5000));
    if (e.accent === 'shooting-star') f.layers.push({name:'Shooting star',sprite:refs[e.accent],x:140,y:5,copies:1,scale:100,stagger:{x:0,y:0,phase:0},scatter:null,motion:[
      {type:'path',period:6500,dx:-105,dy:60,mode:'loop',phase:0},
      {type:'fade',period:6500,phase:0,keys:[{at:0,v:0},{at:80,v:100},{at:300,v:0},{at:999,v:0}]}]});
    return f;
  }
  return {sprites,entries,build};
});
