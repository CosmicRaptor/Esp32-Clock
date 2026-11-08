import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
import mood_video
from model import Invalid

def clip():
    jpeg = b'\xff\xd8\xff\xc0\x00\x11\x08\x00\xf0\x01\x40' + b'\x00' * 10
    audio = (b'RIFF' + struct.pack('<I', 38) + b'WAVEfmt ' +
             struct.pack('<IHHIIHH', 16, 1, 1, 22050, 44100, 2, 16) +
             b'data' + struct.pack('<I', 2) + b'\x00\x00')
    return b'BCV1' + struct.pack('<III', 1, len(audio), len(jpeg)) + jpeg + audio

class MoodVideoTests(unittest.TestCase):
    def test_valid_clip(self):
        data = clip()
        self.assertEqual(mood_video.validate(data)['frames'], 1)
        self.assertEqual(mood_video.validate(data)['size'], len(data))
    def test_invalid_clips_rejected(self):
        data = clip()
        for bad in (b'', data[:-1], b'NOPE' + data[4:], data[:4] + struct.pack('<I', 101) + data[8:]):
            with self.subTest(data=bad[:12]), self.assertRaises(Invalid):
                mood_video.validate(bad)
