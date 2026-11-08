"""Validate browser-prepared BCV1 clips before staging them in KV."""
import hashlib
import struct
from model import Invalid, jpeg_info

MAX_BYTES = 12 * 1024 * 1024
MAX_FRAME = 100000


def validate(data):
    if len(data) < 16 or len(data) > MAX_BYTES or data[:4] != b'BCV1':
        raise Invalid('Invalid prepared video.')
    frames, audio_size = struct.unpack_from('<II', data, 4)
    if not 1 <= frames <= 100 or not 44 <= audio_size <= 44 + 20 * 22050 * 2:
        raise Invalid('Invalid prepared video length.')
    offset = 12 + frames * 4
    if offset > len(data):
        raise Invalid('Incomplete prepared video.')
    sizes = struct.unpack_from('<' + 'I' * frames, data, 12)
    for size in sizes:
        if not 1 <= size <= MAX_FRAME or offset + size > len(data):
            raise Invalid('Invalid prepared video frame.')
        width, height, progressive = jpeg_info(data[offset:offset + size])
        if (width, height, progressive) != (320, 240, False):
            raise Invalid('A video frame is not a 320×240 baseline JPEG.')
        offset += size
    if offset + audio_size != len(data):
        raise Invalid('Incomplete prepared video audio.')
    wav = data[offset:]
    if (wav[:4] != b'RIFF' or wav[8:16] != b'WAVEfmt ' or wav[36:40] != b'data' or
        struct.unpack_from('<IHHIIHH', wav, 16) != (16, 1, 1, 22050, 44100, 2, 16) or
        struct.unpack_from('<I', wav, 4)[0] != audio_size - 8 or
        struct.unpack_from('<I', wav, 40)[0] != audio_size - 44):
        raise Invalid('Invalid prepared video audio.')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'frames': frames, 'size': len(data)}
