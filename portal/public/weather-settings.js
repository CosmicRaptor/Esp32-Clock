
// Shared city picker behavior for the admin and LAN settings pages.
(() => {
  const local = false;
  const get = id => document.getElementById(id);
  const report = text => { get('weatherError').textContent = text; };
  let results = [];
  async function request(url, options) {
    const r = await fetch(url, options);
    const j = await r.json();
    if (!r.ok) throw new Error(j.error || 'Weather settings are unavailable.');
    return j;
  }
  async function refresh() {
    try {
      const j = await request(local ? '/weather' : '/api/weather/location');
      get('weatherCurrent').textContent = local
        ? (j.pending ? 'Waiting for server: ' + (j.pendingCity || 'weather off') + (j.code && j.code !== 204 ? ' (will retry)' : '') : j.city || 'Weather is off')
        : j.location ? j.location.name : 'Weather is off';
    } catch (e) { report(e.message); }
  }
  get('weatherSearch').onsubmit = async e => {
    e.preventDefault(); report('');
    get('weatherFind').disabled = true;
    get('weatherSave').disabled = true;
    results = []; get('weatherCities').replaceChildren();
    try {
      const query = encodeURIComponent(get('weatherQuery').value.trim());
      const url = local ? 'https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=' + query : '/api/weather/cities?q=' + query;
      const j = await request(url, {signal: AbortSignal.timeout(10000)});
      results = (j.results || []).map(c => ({name: [c.name, c.admin1, c.country].filter(Boolean).join(', ').slice(0, 80), latitude: c.latitude, longitude: c.longitude}));
      results.forEach((c, i) => { const o = document.createElement('option'); o.value = i; o.textContent = c.name; get('weatherCities').append(o); });
      get('weatherSave').disabled = !results.length;
      if (!results.length) report('No cities found. Try another spelling.');
    } catch (e) { report('City search failed. Please try again.'); }
    finally { get('weatherFind').disabled = false; }
  };
  async function save(loc) {
    get('weatherSave').disabled = true; get('weatherOff').disabled = true; report('');
    try {
      await request(local ? '/weather' : '/api/weather/location', local
        ? {method: 'POST', headers: {'X-Clock': '1'}, body: new URLSearchParams(loc || {name:'',latitude:0,longitude:0})}
        : {method: 'PUT', headers: {'content-type':'application/json'}, body: JSON.stringify({location:loc})});
      await refresh();
      if (!local) report('Saved. The clock will pick it up on its next weather check.');
    } catch (e) { report(e.message); }
    finally { get('weatherSave').disabled = !results.length; get('weatherOff').disabled = false; }
  }
  get('weatherSave').onclick = () => { const c = results[Number(get('weatherCities').value)]; if (c) save(c); };
  get('weatherOff').onclick = () => save(null);
  if (local) { refresh(); setInterval(refresh, 10000); }
  else { window.loadWeatherSettings = refresh; }
})();
