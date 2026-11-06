'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const R = require('../../public/face-rules.js');

const vectors = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'vectors', 'face_rules.json'), 'utf8'));

for (const c of vectors.matches) {
  test(`matches: ${c.name}`, () => assert.equal(R.matches(c.when, c.now), c.match));
}
for (const c of vectors.picks) {
  test(`pick: ${c.name}`, () => assert.equal(R.pick(c.faces, c.default, c.pinned, c.now), c.expect));
}

test('localParts: the clock\'s own time zone decides the day', () => {
  // 18:30 UTC on Saturday the 7th is already midnight on Sunday the 8th in India
  const at = new Date('2026-11-07T18:30:00Z');
  assert.deepEqual(R.localParts(at, 'Asia/Kolkata'), { year: 2026, month: 11, day: 8, hour: 0, minute: 0, weekday: 0 });
  assert.deepEqual(R.localParts(at, 'UTC'), { year: 2026, month: 11, day: 7, hour: 18, minute: 30, weekday: 6 });
  assert.deepEqual(R.localParts(at, 'America/Los_Angeles'), { year: 2026, month: 11, day: 7, hour: 10, minute: 30, weekday: 6 });
});

test('localParts: midnight is hour 0, not 24', () => {
  assert.equal(R.localParts(new Date('2026-01-01T00:00:00Z'), 'UTC').hour, 0);
});

test('time and date text', () => {
  const now = { year: 2026, month: 11, day: 7, hour: 9, minute: 5, weekday: 6 };
  assert.equal(R.formatTime(now), '09:05');
  assert.equal(R.formatDate('long', now), 'Saturday, 7 November');
  assert.equal(R.formatDate('short', now), 'Sat, 7 Nov');
  assert.equal(R.formatDate('day_month', now), '7 November');
  assert.equal(R.formatDate('weekday', now), 'Saturday');
  assert.equal(R.formatDate('numeric', now), '07/11/2026');
});
