import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
import volume
from model import Invalid

class VolumeTests(unittest.TestCase):
    def test_accepts_separate_levels_and_mute(self):
        self.assertEqual(volume.clean({'alarm': 0, 'video': 21}), {'alarm': 0, 'video': 21})
        self.assertEqual(volume.DEFAULT, {'alarm': 18, 'video': 18})
    def test_rejects_invalid_levels(self):
        for item in (None, {}, {'alarm': -1, 'video': 2}, {'alarm': 22, 'video': 2},
                     {'alarm': True, 'video': 2}, {'alarm': 1, 'video': '2'}):
            with self.subTest(item=item), self.assertRaises(Invalid):
                volume.clean(item)
