import unittest
import alarm_audio
from model import Invalid


class AlarmAudioTests(unittest.TestCase):
    def test_safe_bounded_name_and_content_identity(self):
        first = alarm_audio.prepare('../My song.MP3', b'example audio')
        self.assertEqual(first['name'], 'My-song-' + first['sha256'][:12] + '.mp3')
        self.assertEqual(first['size'], 13)
        self.assertNotEqual(first['sha256'], alarm_audio.prepare('My song.mp3', b'other')['sha256'])
        self.assertLessEqual(len(alarm_audio.prepare('x' * 500 + '.opus', b'a')['name']), 64)

    def test_rejects_missing_unsupported_and_large_files(self):
        for name, data in [('song.mp3', b''), ('song.exe', b'a'), ('song', b'a'),
                           ('song.mp3', b'a' * (alarm_audio.MAX_BYTES + 1))]:
            with self.subTest(name=name, size=len(data)):
                with self.assertRaises(Invalid):
                    alarm_audio.prepare(name, data)
