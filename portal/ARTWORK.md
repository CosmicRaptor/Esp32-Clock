# Panda weather artwork

The nine existing faces each contain a default scene plus cloudy/foggy and stormy rain overrides. Their names, daily schedules, fallback and Spotify layouts stay unchanged. The source PNGs live in `public/panda-collection/`; edited weather backgrounds live in its `weather/` directory.

| Scene | Cloudy/foggy | Stormy rain |
| --- | --- | --- |
| Paris blossoms/beret | Muted blossom sky | Dark wet Paris sky |
| Snowy sleeping panda | Overcast winter | Winter rain; default keeps decorative snow |
| Café/mug | Cloudy window | Rain and lightning inside window bounds |
| Paris night drive | Misty skyline | Storm over the city |
| Autumn bench | Foggy autumn park | Wet stormy autumn park |
| Cloud stargazing | Misty clouds | Dark storm clouds |
| Bedtime | Cloudy window | Rain and lightning inside window bounds |
| Paris sunset with cat | Hazy Paris | Storm over Paris |
| Sleepy cherry blossoms | Muted blossom sky | Rainy cherry blossoms |

Weather PNGs were generated as edits of the approved illustrations using ImageGen. They preserve the pandas, composition and foreground while changing the atmosphere. Rain backgrounds contain no permanent lightning bolt. Transparent rain and lightning assets are trimmed during browser conversion to 1×14 and 11×42 pixels respectively. Runtime backgrounds are 320×240 baseline JPEG; sprites use the existing RGB565A8 encoder.

`panda-collection.js` defines the overrides, colors, bounds and motion. Rain covers drizzle, rain, showers and thunderstorm codes. Lightning blinks every 1,000 ms, fades within 200 ms, and receives a new deterministic random position each cycle through `scatter.period=1000`. Preview and firmware share the same uint32 reseeding formula. Random draws can occasionally repeat positions. Indoor effects remain within windows. Cloudy scenes remove decorative particles except café steam; winter default retains snow because local snow is not expected.

The existing Faces page offers default, cloudy/foggy, rain and automatic previews. The installer upgrades by unique face name, preserving IDs, base edits, rules, Spotify settings, order and pin. Duplicate names are rejected. Script URLs are versioned so cached motion code cannot make lightning appear fixed.

The local 27-scene review gallery is outside `public` and is not deployed. All converted scenes were checked by the real model validator, with maximum rounded animation budget 8%. Python, JS and C++ tests cover classification, protobuf, retained assets, safe upgrading and motion parity. `test_transfer.py` compiles the real buffered HTTP transfer helper against host stubs to exercise idle yielding, stalled-transfer deadline and write failures.
