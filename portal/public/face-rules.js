// Which face the clock shows, and how it writes the time and date. Mirrors model.py's face_matches / pick_face
// (tests/vectors/face_rules.json is run against both) and the clock's own logic. Plain <script> or CommonJS.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.FaceRules = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];

  const emptyWhen = () => ({ hours: null, months: [], weekdays: [], dates: [] });
  const whenIsEmpty = w => !(w && ((w.hours) || (w.months && w.months.length) || (w.weekdays && w.weekdays.length) || (w.dates && w.dates.length)));

  // `now`: the clock's local {year, month, day, hour, weekday} (weekday 0 = Sunday). All the constraints that are set
  // must hold; a `when` with none never matches.
  function matches(when, now) {
    if (whenIsEmpty(when)) return false;
    const h = when.hours;
    if (h) {
      const inside = h.from < h.to ? now.hour >= h.from && now.hour < h.to : now.hour >= h.from || now.hour < h.to;
      if (!inside) return false;
    }
    if (when.months && when.months.length && !when.months.includes(now.month)) return false;
    if (when.weekdays && when.weekdays.length && !when.weekdays.includes(now.weekday)) return false;
    if (when.dates && when.dates.length &&
        !when.dates.some(d => d.month === now.month && d.day === now.day && (!d.year || d.year === now.year))) return false;
    return true;
  }

  // The id of the face to show, or null for the clock's built-in one. faces: [{id, when}] in priority order.
  function pick(faces, def, pinned, now) {
    const has = id => !!id && faces.some(f => f.id === id);
    if (has(pinned)) return pinned;
    for (const f of faces) if (matches(f.when, now)) return f.id;
    return has(def) ? def : null;
  }

  // The `now` for matches(), in time zone `tz` (an IANA name), from a Date.
  function localParts(date, tz) {
    const p = Object.fromEntries(new Intl.DateTimeFormat('en-US', {
      timeZone: tz, year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric',
      weekday: 'short', hourCycle: 'h23',
    }).formatToParts(date).map(x => [x.type, x.value]));
    return {
      year: +p.year, month: +p.month, day: +p.day, hour: +p.hour % 24, minute: +p.minute,
      weekday: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'].indexOf(p.weekday),
    };
  }

  const pad2 = n => String(n).padStart(2, '0');
  const formatTime = now => `${pad2(now.hour)}:${pad2(now.minute)}`;
  // The date slot's text, in English like the clock's own date line.
  function formatDate(format, now) {
    const wd = DAYS[now.weekday], mo = MONTHS[now.month - 1];
    switch (format) {
      case 'short': return `${wd.slice(0, 3)}, ${now.day} ${mo.slice(0, 3)}`;
      case 'day_month': return `${now.day} ${mo}`;
      case 'weekday': return wd;
      case 'numeric': return `${pad2(now.day)}/${pad2(now.month)}/${now.year}`;
      default: return `${wd}, ${now.day} ${mo}`;
    }
  }

  return { DAYS, MONTHS, emptyWhen, whenIsEmpty, matches, pick, localParts, formatTime, formatDate };
});
