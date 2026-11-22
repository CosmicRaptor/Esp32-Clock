"""Validate and name audio files staged for the clock's alarm folder."""
import hashlib
import re
from model import Invalid

MAX_BYTES = 8 * 1024 * 1024
EXTENSIONS = {'mp3', 'wav', 'm4a', 'aac', 'flac', 'ogg', 'opus'}


def prepare(name, data):
    if not isinstance(name, str) or '.' not in name:
        raise Invalid('Choose an audio file with a supported extension.')
    stem, ext = name.rsplit('.', 1)
    ext = ext.lower()
    if ext not in EXTENSIONS:
        raise Invalid('Use an MP3, WAV, M4A, AAC, FLAC, OGG or OPUS file.')
    if not data or len(data) > MAX_BYTES:
        raise Invalid('Choose a nonempty audio file no larger than 8 MB.')
    stem = re.sub(r'[^A-Za-z0-9_-]+', '-', stem).strip('-_')[:36] or 'song'
    digest = hashlib.sha256(data).hexdigest()
    return {'sha256': digest, 'size': len(data), 'name': f'{stem}-{digest[:12]}.{ext}'}
