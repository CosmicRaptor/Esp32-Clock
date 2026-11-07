"""Firmware release and diagnostic validation; no credentials or arbitrary logs accepted."""
import hashlib
import re
from model import Invalid

INTERVALS = (0, 300, 900, 1800, 3600, 21600)
MAX_FIRMWARE = 6 * 1024 * 1024

def settings(data):
    if not isinstance(data, dict):
        raise Invalid('Expected settings object.')
    interval = data.get('interval', 3600)
    if type(interval) is not int or interval not in INTERVALS:
        raise Invalid('Choose a supported diagnostic interval.')
    return {'interval': interval}

def firmware(data):
    # ESP image header: magic and ESP32-S3 chip id (9), little endian.
    if not 256 <= len(data) <= MAX_FIRMWARE or data[0] != 0xe9 or data[12:14] != b'\x09\x00' or data[32:36] != b'\x32\x54\xcd\xab':
        raise Invalid('Upload an ESP32-S3 application .bin (not a merged flash image).')
    return {'release': hashlib.sha256(data).hexdigest(), 'size': len(data)}

def report(data):
    if not isinstance(data, dict):
        raise Invalid('Expected a diagnostic object.')
    out = {}
    for key in ('uptime', 'heap', 'min_heap', 'largest_block', 'psram', 'reset', 'rssi', 'poll_code', 'ota_code', 'ota_state'):
        value = data.get(key, 0)
        if type(value) is not int or not -2147483648 <= value <= 0xffffffffffff:
            raise Invalid('Invalid diagnostic number: ' + key)
        out[key] = value
    for key in ('firmware', 'running', 'failed', 'installed'):
        value = data.get(key, '')
        if not isinstance(value, str) or not re.fullmatch(r'[a-zA-Z0-9._-]{0,64}', value):
            raise Invalid('Invalid diagnostic identifier.')
        out[key] = value
    return out

def report_due(old, new, now):
    """Normal snapshots are limited to five minutes; OTA transitions may report after one minute."""
    if not old:
        return True
    elapsed = now - old.get('at', 0)
    transition = any(old.get(k, '' if k != 'ota_state' else 0) != new.get(k, '' if k != 'ota_state' else 0)
                     for k in ('firmware', 'running', 'installed', 'failed', 'ota_state'))
    return elapsed >= 300 or (elapsed >= 60 and transition)
