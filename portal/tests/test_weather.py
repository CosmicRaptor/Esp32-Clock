import os
import sys
import unittest
from unittest.mock import AsyncMock
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
import weather
import clock_pb2 as pb
from model import Invalid, WEATHER_CITY_MAX
from test_wire import nanopb_size, nanopb_array

NOW = 1800000000
CITY = {'name': 'Mumbai, India', 'latitude': 19.076, 'longitude': 72.8777}
DATA = {'current': {'temperature_2m': 28.4, 'weather_code': 0, 'is_day': 1, 'time': NOW - 120}}


class Validation(unittest.TestCase):
    def test_location(self):
        self.assertEqual(weather.location(CITY), CITY)
        self.assertIsNone(weather.location(None))
        for d in ([], {}, dict(CITY, name=''), dict(CITY, latitude=float('nan')),
                  dict(CITY, longitude=181), dict(CITY, latitude=True), dict(CITY, name='x\ny')):
            with self.assertRaises(Invalid):
                weather.location(d)

    def test_current_and_night(self):
        r = weather.simplify(DATA, NOW)
        self.assertEqual((r['temperature_c'], r['emoji']), (28, '☀️'))
        self.assertEqual(weather.simplify({'current': dict(DATA['current'], is_day=0)}, NOW)['emoji'], '🌙')
        for code in weather.ICONS:
            self.assertTrue(weather.simplify({'current': dict(DATA['current'], weather_code=code)}, NOW)['emoji'])

    def test_rejects_bad_observation(self):
        for patch in ({'temperature_2m': None}, {'temperature_2m': float('nan')}, {'weather_code': 900},
                      {'time': NOW - 10801}, {'is_day': 2}, {'weather_code': True}):
            with self.assertRaises((ValueError, TypeError)):
                weather.simplify({'current': dict(DATA['current'], **patch)}, NOW)

    def test_proto_negative_temperature_and_city(self):
        m = pb.Weather(available=True, temperature_c=-7, code=71, observed_at=NOW, city='😀' * 80)
        self.assertEqual(pb.Weather.FromString(m.SerializeToString()), m)
        self.assertLessEqual(len(m.SerializeToString()), nanopb_size("Weather"))
        self.assertEqual(nanopb_array("Weather", "city"), 4 * WEATHER_CITY_MAX + 1)


class Cache(unittest.IsolatedAsyncioTestCase):
    async def test_miss_then_hit_and_expiry(self):
        fetch, store = AsyncMock(return_value=DATA), AsyncMock()
        r = await weather.current(CITY, None, NOW, fetch, store)
        rec = store.call_args.args[0]
        self.assertEqual(r['observed_at'], NOW - 120)
        fetch.reset_mock(); store.reset_mock()
        self.assertEqual(await weather.current(CITY, rec, NOW + 899, fetch, store), r)
        fetch.assert_not_called(); store.assert_not_called()
        await weather.current(CITY, rec, NOW + 900, fetch, store)
        fetch.assert_awaited_once(); store.assert_awaited_once()

    async def test_outage_retains_observation_and_throttles(self):
        r = weather.simplify(DATA, NOW)
        rec = {'location': CITY, 'attempted_at': NOW - 900, 'reading': r}
        fetch, store = AsyncMock(side_effect=OSError()), AsyncMock()
        self.assertEqual(await weather.current(CITY, rec, NOW, fetch, store), r)
        failed = store.call_args.args[0]
        fetch.reset_mock(); store.reset_mock()
        self.assertEqual(await weather.current(CITY, failed, NOW + 600, fetch, store), r)
        fetch.assert_not_called(); store.assert_not_called()

    async def test_never_returns_old_city_or_expired_data(self):
        rec = {'location': CITY, 'attempted_at': NOW, 'reading': weather.simplify(DATA, NOW)}
        fetch, store = AsyncMock(side_effect=OSError()), AsyncMock()
        self.assertIsNone(await weather.current(dict(CITY, latitude=1), rec, NOW, fetch, store))
        self.assertIsNone(await weather.current(CITY, rec, NOW + 10801, fetch, store))

    async def test_malformed_upstream_and_empty_cache(self):
        fetch, store = AsyncMock(return_value={}), AsyncMock()
        self.assertIsNone(await weather.current(CITY, None, NOW, fetch, store))
        failed = store.call_args.args[0]
        fetch.reset_mock()
        self.assertIsNone(await weather.current(CITY, failed, NOW + 1, fetch, store))
        fetch.assert_not_called()
