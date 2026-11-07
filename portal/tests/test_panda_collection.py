"""Validate the shipped collection with the same rules the device receives."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
import model


class PandaCollectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[1]
        script = '''
          const p = require('./public/panda-collection.js');
          const keys = [...Object.keys(p.sprites), ...p.entries.map(e => e.key)];
          const refs = Object.fromEntries(keys.map((k,i) => [k,(i+1).toString(16).padStart(16,'0')]));
          const assets = {};
          for (const [name,hash] of Object.entries(refs)) {
            const sprite = name in p.sprites, size = p.sprites[name];
            // A square is a conservative bound on each trimmed/resized sprite.
            assets[hash] = sprite ? {kind:'sprite',w:size,h:size,bytes:12+3*size*size}
              : {kind:'bg',w:320,h:240,bytes:30000};
          }
          console.log(JSON.stringify({drafts:p.entries.map(e=>p.build(e,refs)),assets}));
        '''
        cls.collection = json.loads(subprocess.check_output(['node', '-e', script], cwd=root))

    def test_all_faces_fit_screen_and_budget_even_with_square_sprite_bounds(self):
        self.assertEqual(len(self.collection['drafts']), 9)
        for i, draft in enumerate(self.collection['drafts']):
            with self.subTest(face=draft['name']):
                _, report = model.check_face(draft, self.collection['assets'], f'{i+1:012x}')
                self.assertEqual(report['errors'], [])
                self.assertLess(report['budget']['ratio'], .15)

    def test_approved_daily_schedule_has_no_overlap_and_sunset_is_fallback(self):
        drafts = self.collection['drafts']
        expected = [6]*5 + [1]*2 + [8]*2 + [2]*3 + [0]*3 + [4]*3 + [7]*2 + [3]*2 + [5]*2
        for hour, index in enumerate(expected):
            matches = [i for i,f in enumerate(drafts) if f['when']['hours'] and
                       f['when']['hours']['from'] <= hour < f['when']['hours']['to']]
            self.assertEqual(matches, [] if index == 7 else [index], f'hour {hour}')
        self.assertIsNone(drafts[7]['when']['hours'])
        self.assertEqual(drafts[7]['name'], 'Paris sunset with cat')

    def test_every_shipped_image_exists(self):
        assets = Path(__file__).resolve().parents[1] / 'public' / 'panda-collection'
        for name in ['01','02','03','04','05','06','07','08','09',
                     'petal','snowflake','steam','shooting-star','leaf','twinkle']:
            self.assertTrue((assets / f'{name}.png').is_file(), name)

    def test_spotify_is_themed_and_does_not_overlap_time_or_date(self):
        for f in self.collection['drafts']:
            np = f['nowplaying']
            self.assertTrue(np['show'])
            self.assertNotEqual(np['bg'], '#000000')
            for name in ['time', 'date']:
                slot = f[name]
                self.assertTrue(slot['y'] >= np['y'] + 46 or
                                slot['y'] + slot['size'] * 1.219 <= np['y'],
                                f"{f['name']}: {name} overlaps Spotify")
