"""Open-Meteo validation and a demand-driven, persisted 15-minute cache."""
import asyncio
import math
from urllib.parse import urlencode
from model import Invalid, WEATHER_CITY_MAX

TTL = 900
MAX_AGE = 3 * 3600
ICONS = {0: '☀️', 1: '🌤️', 2: '⛅', 3: '☁️', 45: '🌫️', 48: '🌫️',
         51: '🌧️', 53: '🌧️', 55: '🌧️', 56: '🌧️', 57: '🌧️',
         61: '🌧️', 63: '🌧️', 65: '🌧️', 66: '🌧️', 67: '🌧️',
         71: '❄️', 73: '❄️', 75: '❄️', 77: '❄️', 80: '🌧️', 81: '🌧️', 82: '🌧️',
         85: '❄️', 86: '❄️', 95: '⛈️', 96: '⛈️', 99: '⛈️'}


def location(data):
    if data is None:
        return None
    if not isinstance(data, dict):
        raise Invalid('Choose a city from the search results.')
    name = data.get('name')
    if not isinstance(name, str) or not name.strip() or len(name) > WEATHER_CITY_MAX or any(ord(c) < 32 for c in name):
        raise Invalid('Choose a city from the search results.')
    coords = []
    for key, limit in (('latitude', 90), ('longitude', 180)):
        value = data.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or abs(value) > limit:
            raise Invalid('Invalid weather coordinates.')
        coords.append(round(value, 5))
    return dict(name=name.strip(), latitude=coords[0], longitude=coords[1])


def forecast_url(loc):
    return 'https://api.open-meteo.com/v1/forecast?' + urlencode({
        'latitude': loc['latitude'], 'longitude': loc['longitude'],
        'current': 'temperature_2m,weather_code,is_day', 'timeformat': 'unixtime',
        'temperature_unit': 'celsius', 'timezone': 'UTC'})


def simplify(data, now):
    c = data['current']
    temp, code, stamp, day = (c[k] for k in ('temperature_2m', 'weather_code', 'time', 'is_day'))
    if (isinstance(temp, bool) or not isinstance(temp, (int, float)) or not math.isfinite(temp)
            or not -100 <= temp <= 70 or type(code) is not int or code not in ICONS
            or type(stamp) is not int or not now - MAX_AGE <= stamp <= now + 300
            or type(day) is not int or day not in (0, 1)):
        raise ValueError('Invalid current weather')
    return {'temperature_c': round(temp), 'code': code, 'is_day': bool(day), 'observed_at': stamp,
            'emoji': '🌙' if code == 0 and not day else ICONS[code]}


def usable(record, loc, now):
    return (isinstance(record, dict) and record.get('location') == loc
            and isinstance(record.get('reading'), dict)
            and 0 <= now - record['reading'].get('observed_at', 0) <= MAX_AGE)


async def current(loc, record, now, fetch_json, store):
    """Never mix cities; preserve observation time on failure, retry no faster than 15 min."""
    good = usable(record, loc, now)
    if record and record.get('location') == loc and 0 <= now - record.get('attempted_at', 0) < TTL:
        return record['reading'] if good else None
    try:
        reading = simplify(await asyncio.wait_for(fetch_json(forecast_url(loc)), timeout=6), now)
    except Exception:
        reading = record['reading'] if good else None
    await store({'location': loc, 'attempted_at': now, 'reading': reading})
    return reading
