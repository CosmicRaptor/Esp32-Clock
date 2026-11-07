#pragma once
// Shared categories: portal/public/face-weather.js and portal/src/weather.py.
namespace faceweather {
enum Group { BASE = 0, CLOUDY = 1, RAIN = 2 };
inline Group group(unsigned code) {
  switch (code) {
    case 2: case 3: case 45: case 48: return CLOUDY;
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: case 95: case 96: case 97: case 99: return RAIN;
    default: return BASE;
  }
}
}
