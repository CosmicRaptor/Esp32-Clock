"""Validation and retention policy for clock mindfulness responses."""
from model import Invalid

RETENTION = 30 * 86400


def clean(data, now):
    if not isinstance(data, dict):
        raise Invalid('Expected a mood response.')
    at, mood = data.get('at'), data.get('mood')
    if type(at) is not int or type(mood) is not int or not 1 <= mood <= 5:
        raise Invalid('Invalid mood response.')
    if at > now + 300 or at < 1700000000:
        raise Invalid('Invalid response time.')
    return {'at': at, 'mood': mood}


def clean_note(value):
    if not isinstance(value, str):
        raise Invalid('Enter a short note.')
    value = ' '.join(value.split())
    if len(value.encode('utf-8')) > 120 or any(ord(c) < 32 for c in value):
        raise Invalid('The note must be at most 120 UTF-8 bytes.')
    return value
