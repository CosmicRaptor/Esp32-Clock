const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const header = fs.readFileSync(require.resolve('../../../firmware/BhoomiClock/alarm_settings_js.h'), 'utf8');
const source = header.split('R"ALARMSJS(')[1].split(')ALARMSJS";')[0];

class Element {
  children = [];
  value = '';
  textContent = '';
  listeners = {};
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  add(child) { this.append(child); }
  addEventListener(event, handler) { this.listeners[event] = handler; }
  setAttribute() {}
  focus() {}
}

test('LAN alarm editor loads an existing alarm, queues edits, and deletes by ID', async () => {
  const nodes = {};
  const node = id => nodes[id] ??= new Element();
  const requests = [];
  const id = '123456789abc';
  const pending = {pending: 1, alarms: []};
  let state = {pending: 0, alarms: [{
    id, time: '08:30', days: 0, date: '2026-12-01', enabled: false, label: '<Wake>', sound: 'chime.mp3'
  }]};
  const context = {
    document: {getElementById: node, createElement: () => new Element(), addEventListener() {}},
    window: {},
    fetch: async (path, options = {}) => {
      requests.push({path, options});
      if (options.method === 'POST') state = {...state, pending: 1};
      if (options.method === 'DELETE') state = pending;
      return {ok: true, json: async () => path.startsWith('/files') ? {files: [{name: 'bell.wav'}]} : state};
    },
    Option: class { constructor(text, value) { this.text = text; this.value = value; } },
    URLSearchParams, confirm: () => true, setInterval() {},
  };
  const flush = () => new Promise(resolve => setImmediate(resolve));
  vm.runInNewContext(source, context);
  context.window.initAlarmSettings('Asia/Kolkata');
  await flush();

  const row = node('alarmList').children[0];
  assert.equal(row.children[0].children[0].textContent, '08:30 · <Wake>');
  assert.equal(row.children[0].children[1].textContent, '2026-12-01 · Off');
  await row.children[1].onclick();
  assert.equal(node('alarmDate').required, true);
  assert.equal(node('alarmSound').value, 'chime.mp3');
  assert.deepEqual(node('alarmSound').children.map(o => o.value), ['', 'chime.mp3', 'bell.wav']);

  const monday = node('alarmDays').children[1].children[0];
  monday.checked = true;
  monday.listeners.change();
  assert.equal(node('alarmDate').required, false);
  node('alarmEnabled').checked = true;
  node('alarmEditor').onsubmit({preventDefault() {}});
  await flush();
  const save = requests.find(entry => entry.options.method === 'POST');
  assert.equal(save.options.headers['X-Clock'], '1');
  assert.equal(save.options.body.get('id'), id);
  assert.equal(save.options.body.get('days'), '2');
  assert.equal(save.options.body.get('enabled'), '1');
  assert.match(node('alarmStatus').textContent, /1 change waiting to sync/);

  await node('alarmList').children[0].children[1].onclick();
  await node('alarmDelete').onclick();
  const remove = requests.find(entry => entry.options.method === 'DELETE');
  assert.equal(remove.path, '/alarms?id=' + id);
  assert.equal(remove.options.headers['X-Clock'], '1');
  assert.equal(node('alarmList').children[0].textContent, 'No alarms set.');
});
