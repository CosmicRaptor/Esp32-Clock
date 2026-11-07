const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../../public/weather-settings.js'), 'utf8');

function picker(local = false) {
  const elements = new Map();
  function element(id) {
    if (!elements.has(id)) elements.set(id, {value: '', disabled: false, textContent: '', children: [],
      replaceChildren() { this.children = []; }, append(o) { this.children.push(o); }});
    return elements.get(id);
  }
  const calls = [];
  let reply = {};
  const context = {document: {getElementById: element, createElement: () => ({})}, window: {},
    URLSearchParams, AbortSignal, setInterval() {},
    fetch: async (url, opts) => { calls.push({url, opts}); return {ok: true, json: async () => reply}; }};
  vm.runInNewContext(local ? source.replace('const local = false', 'const local = true') : source, context);
  return {element, calls, setReply: j => { reply = j; }, window: context.window};
}

test('Admin: search distinguishes cities and saves coordinates, then supports turning off', async () => {
  const p = picker();
  p.element('weatherQuery').value = 'Springfield';
  p.setReply({results: [
    {name: 'Springfield', admin1: 'Illinois', country: 'United States', latitude: 39.78, longitude: -89.65},
    {name: 'Springfield', admin1: 'Massachusetts', country: 'United States', latitude: 42.10, longitude: -72.59}
  ]});
  await p.element('weatherSearch').onsubmit({preventDefault() {}});
  assert.equal(p.element('weatherCities').children.length, 2);
  assert.match(p.element('weatherCities').children[1].textContent, /Massachusetts/);
  assert.equal(p.element('weatherSave').disabled, false);
  p.element('weatherCities').value = '1';
  // onclick dispatches an async save; let all its promise continuations finish.
  p.element('weatherSave').onclick();
  await new Promise(resolve => setImmediate(resolve));
  const saved = p.calls.find(c => c.opts?.method === 'PUT');
  assert.equal(saved.url, '/api/weather/location');
  assert.equal(JSON.parse(saved.opts.body).location.latitude, 42.10);
  p.element('weatherOff').onclick();
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(JSON.parse(p.calls.filter(c => c.opts?.method === 'PUT').at(-1).opts.body), {location: null});
});

test('No city matches cannot save an old selection; labels use plain text', async () => {
  const p = picker();
  p.element('weatherQuery').value = '<img src=x>';
  p.setReply({results: [{name:'<img src=x>', latitude:1, longitude:2}]});
  await p.element('weatherSearch').onsubmit({preventDefault() {}});
  assert.equal(p.element('weatherCities').children[0].textContent, '<img src=x>');
  p.setReply({results: []});
  await p.element('weatherSearch').onsubmit({preventDefault() {}});
  assert.equal(p.element('weatherCities').children.length, 0);
  assert.equal(p.element('weatherSave').disabled, true);
  assert.match(p.element('weatherError').textContent, /No cities/);
});

test('LAN picker queues changes with the anti-CSRF header and displays pending state', async () => {
  const p = picker(true);
  p.setReply({results:[{name:'Mumbai', latitude:19.076, longitude:72.8777}]});
  p.element('weatherQuery').value = 'Mumbai';
  await p.element('weatherSearch').onsubmit({preventDefault() {}});
  assert.ok(p.calls.some(c => c.url.startsWith('https://geocoding-api.open-meteo.com/')));
  p.setReply({pending:true, pendingCity:'Mumbai', code:0});
  p.element('weatherCities').value = '0';
  p.element('weatherSave').onclick();
  await new Promise(resolve => setImmediate(resolve));
  const saved = p.calls.find(c => c.opts?.method === 'POST');
  assert.equal(saved.url, '/weather');
  assert.equal(saved.opts.headers['X-Clock'], '1');
  assert.equal(saved.opts.body.get('latitude'), '19.076');
  assert.match(p.element('weatherCurrent').textContent, /Waiting for server: Mumbai/);
});
