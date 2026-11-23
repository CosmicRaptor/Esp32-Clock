#pragma once
#include <Arduino.h>

static const char ALARM_SETTINGS_JS[] PROGMEM = R"ALARMSJS(
window.initAlarmSettings = timezone => {
  const byId = id => document.getElementById(id);
  const list = byId('alarmList'), status = byId('alarmStatus'), form = byId('alarmEditor');
  const add = byId('alarmAdd'), save = byId('alarmSave'), remove = byId('alarmDelete');
  const cancel = byId('alarmCancel'), error = byId('alarmError');
  const time = byId('alarmTime'), date = byId('alarmDate'), label = byId('alarmLabel');
  const sound = byId('alarmSound'), enabled = byId('alarmEnabled');
  const names = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const chips = names.map(name => {
    const item = document.createElement('label');
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.addEventListener('change', syncDate);
    item.append(box, name);
    byId('alarmDays').append(item);
    return box;
  });
  let editing = null, saving = false, reading = false, generation = 0, lastList = '';

  function days() {
    return chips.reduce((mask, box, i) => mask | (box.checked ? 1 << i : 0), 0);
  }
  function syncDate() {
    date.required = days() === 0;
    byId('alarmDateRow').hidden = !date.required;
  }

  async function request(path, options) {
    const response = await fetch(path, options);
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Could not reach the clock.');
    return data;
  }

  function render(data) {
    const pending = data.pending;
    status.textContent = data.cloudFull ? 'The cloud alarm list is full. Changes are saved on this clock.'
      : data.rejected ? 'The cloud rejected an alarm change. Edit or delete that alarm to retry.'
      : pending ? pending + (pending === 1 ? ' change waiting' : ' changes waiting') + ' to sync with the cloud.'
      : '';
    const snapshot = JSON.stringify(data.alarms);
    if (snapshot === lastList) return;
    lastList = snapshot;
    list.replaceChildren();
    if (!data.alarms.length) {
      const empty = document.createElement('li');
      empty.className = 'empty';
      empty.textContent = 'No alarms set.';
      list.append(empty);
    }
    for (const alarm of data.alarms) {
      const row = document.createElement('li');
      const description = document.createElement('div');
      const heading = document.createElement('strong');
      heading.textContent = alarm.time + (alarm.label ? ' · ' + alarm.label : '');
      const details = document.createElement('span');
      details.className = 'note';
      const schedule = alarm.days === 127 ? 'Every day' : alarm.days
        ? names.filter((name, i) => alarm.days & (1 << i)).join(', ')
        : alarm.date;
      details.textContent = schedule + (alarm.enabled ? '' : ' · Off');
      description.append(heading, details);
      const edit = document.createElement('button');
      edit.type = 'button';
      edit.className = 'link';
      edit.textContent = 'Edit';
      edit.setAttribute('aria-label', 'Edit alarm at ' + heading.textContent);
      edit.onclick = () => open(alarm);
      row.append(description, edit);
      list.append(row);
    }
  }

  async function refresh() {
    if (saving || reading || document.hidden) return;
    const started = generation;
    reading = true;
    try {
      const data = await request('/alarms');
      if (started === generation) render(data);
    } catch (e) {
      if (started === generation) status.textContent = 'Could not load alarms from the clock.';
    } finally {
      reading = false;
    }
  }

  async function open(alarm = {}) {
    if (saving) return;
    editing = alarm;
    time.value = alarm.time || '';
    date.value = alarm.date || '';
    label.value = alarm.label || '';
    enabled.checked = alarm.enabled ?? true;
    chips.forEach((box, i) => { box.checked = !!(alarm.days & (1 << i)); });
    syncDate();
    const selected = alarm.sound || '';
    sound.replaceChildren(new Option('Built-in beep', ''));
    if (selected) sound.add(new Option(selected, selected));
    sound.value = selected;
    remove.hidden = !alarm.id;
    error.textContent = '';
    add.hidden = true;
    form.hidden = false;
    time.focus();
    try {
      const available = await request('/files?folder=sounds');
      if (editing !== alarm || saving) return;
      for (const {name} of available.files) {
        if (name !== selected) sound.add(new Option(name, name));
      }
    } catch (e) { /* Built-in beep and any saved sound remain available. */ }
  }

  function close() {
    editing = null;
    form.hidden = true;
    add.hidden = false;
  }

  async function change(deleting) {
    if (!editing || saving) return;
    const id = editing.id || '';
    if (deleting && !confirm('Delete this alarm?')) return;
    if (!deleting && [...label.value.trim()].length > 40) {
      error.textContent = 'Use up to 40 characters for the alarm label.';
      return;
    }
    saving = true;
    ++generation;
    save.disabled = remove.disabled = cancel.disabled = true;
    error.textContent = '';
    try {
      const path = deleting ? '/alarms?' + new URLSearchParams({id}) : '/alarms';
      const body = deleting ? undefined : new URLSearchParams({
        id, time: time.value, days: days(), date: date.value, label: label.value,
        sound: sound.value, enabled: enabled.checked ? '1' : '0'
      });
      render(await request(path, {method: deleting ? 'DELETE' : 'POST', headers: {'X-Clock': '1'}, body}));
      close();
    } catch (e) {
      error.textContent = e.message;
    } finally {
      saving = false;
      save.disabled = remove.disabled = cancel.disabled = false;
      refresh();
    }
  }

  byId('alarmZone').textContent = timezone || '';
  byId('alarmCard').hidden = false;
  add.onclick = () => open();
  cancel.onclick = close;
  remove.onclick = () => change(true);
  form.onsubmit = e => { e.preventDefault(); change(false); };
  refresh();
  setInterval(refresh, 15000);
  document.addEventListener('visibilitychange', refresh);
};
)ALARMSJS";
