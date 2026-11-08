import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
import mindfulness
from model import Invalid

class MindfulnessTests(unittest.TestCase):
    def test_valid_levels_and_expiry(self):
        now = 1800000000
        for value in range(1, 6):
            self.assertEqual(mindfulness.clean({'at': now, 'mood': value}, now), {'at': now, 'mood': value})
        self.assertEqual(mindfulness.RETENTION, 30 * 86400)

    def test_bad_responses_are_rejected(self):
        now = 1800000000
        for data in (None, {}, {'at': now, 'mood': 0}, {'at': now, 'mood': 6},
                     {'at': now, 'mood': True}, {'at': now + 301, 'mood': 1},
                     {'at': 0, 'mood': 1}, {'at': '1800000000', 'mood': 1}):
            with self.subTest(data=data), self.assertRaises(Invalid):
                mindfulness.clean(data, now)

    def test_notes_are_short_and_normalized(self):
        self.assertEqual(mindfulness.clean_note('  You  are\n loved  '), 'You are loved')
        self.assertEqual(mindfulness.clean_note(''), '')
        for value in (None, 123, 'x' * 121, 'é' * 61):
            with self.subTest(value=value), self.assertRaises(Invalid):
                mindfulness.clean_note(value)
