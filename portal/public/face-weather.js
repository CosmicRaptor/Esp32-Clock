(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.FaceWeather=factory();})(typeof self!=='undefined'?self:this,function(){
  'use strict';
  function group(code){
    if([2,3,45,48].includes(code))return 'cloudy';
    if([51,53,55,56,57,61,63,65,66,67,80,81,82,95,96,97,99].includes(code))return 'rain';
    return 'default';
  }
  function current(reading,now){return reading&&now>=reading.observed_at&&now-reading.observed_at<=10800?group(reading.code):'default';}
  function scene(face,kind){
    const v=face.weather&&face.weather[kind];if(!v)return face;
    return {...face,background:v.background,layers:v.layers,particles:v.particles,
      time:v.text_color?{...face.time,color:v.text_color}:face.time,
      date:face.date&&v.text_color?{...face.date,color:v.text_color}:face.date};
  }
  return {group,current,scene};
});
