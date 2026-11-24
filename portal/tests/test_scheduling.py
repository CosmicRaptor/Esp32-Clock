from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORTAL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORTAL / "src"))
import model
import wire


class ScheduledNotesTests(unittest.TestCase):
    def test_due_text_changes_device_payload_at_delivery_without_a_state_write(self):
        state = model.normalize_state({"version": 42, "message": {"text": "Earlier", "at": 4}})
        state["scheduled_notes"] = [
            {"id": "first", "kind": "text", "at": 10, "text": "Hello"},
            {"id": "second", "kind": "text", "at": 20, "text": "Hello"},
        ]

        def at(now):
            return wire.device_state({**state, "message": model.effective_message(state, now)})

        self.assertEqual((at(9).note.text, at(10).note.text), ("Earlier", "Hello"))
        self.assertEqual(at(20).version, 42)
        self.assertNotEqual(at(19).SerializeToString(), at(20).SerializeToString())
        self.assertEqual(at(20).note.sent_at, 20)
        state.update(message=None, message_at=15)
        self.assertFalse(at(19).HasField("note"))
        self.assertEqual(at(20).note.text, "Hello")
        state.update(message={"text": "Manual", "at": 20}, message_at=20)
        self.assertEqual(at(20).note.text, "Manual")

    def test_video_due_time_and_immediate_override(self):
        old, new, future = ({"sha256": label} for label in ("prior", "scheduled", "future"))
        notes = [{"kind": "video", "at": 10, "video": new}, {"kind": "video", "at": 20, "video": future}]
        self.assertEqual(model.effective_video(notes, old, 9), old)
        self.assertEqual(model.effective_video(notes, old, 10), new)
        immediate = {**old, "at": 11}
        self.assertEqual(model.effective_video(notes, immediate, 19), immediate)
        self.assertEqual(model.effective_video(notes, immediate, 20), future)

    def test_queue_keeps_due_inbox_items_and_limits_pending(self):
        notes = [{"kind": "text", "at": 1}, {"kind": "text", "at": 2},
                 {"kind": "video", "at": 3}, {"kind": "video", "at": 5}]
        saved = model.add_scheduled_note(notes, {"kind": "text", "at": 5}, 4)
        self.assertEqual([(n["kind"], n["at"]) for n in saved],
                         [("text", 2), ("video", 3), ("video", 5), ("text", 5)])
        with self.assertRaises(model.Invalid):
            model.add_scheduled_note(saved, {"kind": "text", "at": 5}, 4)
        full = [{"kind": "text", "at": 10 + i} for i in range(model.MAX_SCHEDULED_NOTES)]
        with self.assertRaises(model.Invalid):
            model.add_scheduled_note(full, {"kind": "video", "at": 60}, 4)
        for invalid in (None, True, "6", 5, 10**15):
            with self.assertRaises(model.Invalid):
                model.clean_note_time(invalid, 5)
        self.assertEqual(model.clean_note_time(6, 5), 6)


class DailyRestartTests(unittest.TestCase):
    def test_wire_distinguishes_off_from_midnight(self):
        state = model.normalize_state({"version": 1})
        self.assertFalse(wire.device_state(state).HasField("daily_restart"))
        for time, minute in (("00:00", 0), ("23:59", 1439)):
            state["daily_restart"] = model.clean_restart(time)
            message = wire.device_state(state)
            self.assertTrue(message.HasField("daily_restart"))
            self.assertEqual(message.daily_restart.minute_of_day, minute)
        for invalid in ("24:00", "10:60", "1:00", False, 0):
            with self.assertRaises(model.Invalid):
                model.clean_restart(invalid)

    def test_firmware_window_and_persisted_day(self):
        source = r"""
#include "daily_restart.h"
#include <cassert>
int main() {
  struct tm t{};
  t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 25;
  const auto day = dailyrestart::dateKey(t);
  assert(day == 20261125);
  assert(dailyrestart::due(0, t, 20261124));
  assert(!dailyrestart::due(0, t, day));
  assert(!dailyrestart::due(30, t, 0));
  t.tm_min = 59;
  assert(dailyrestart::due(0, t, 0));
  assert(!dailyrestart::due(0, t, 20261126));
  t.tm_hour = 1; t.tm_min = 0;
  assert(!dailyrestart::due(0, t, 0));
  assert(!dailyrestart::due(1440, t, 0));
  t.tm_mday = 26; t.tm_hour = 0;
  assert(dailyrestart::due(0, t, day));
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "restart"
            include = PORTAL.parent / "firmware/BhoomiClock"
            subprocess.run(["c++", "-std=c++17", "-I", str(include), "-x", "c++", "-", "-o", str(binary)],
                           input=source, text=True, check=True)
            subprocess.run([str(binary)], check=True)
