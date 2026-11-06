# Montserrat Medium

`montserrat-medium-latin.woff2` is Montserrat Medium (weight 500) cut down to Basic Latin plus a few punctuation
marks, so the Faces page can draw the clock's digits with the same font the clock uses (the firmware renders
`font_montserrat_ttf.c`, copied from LVGL's `scripts/built_in_font`, with Tiny TTF).

Copyright 2011 The Montserrat Project Authors (https://github.com/JulietaUla/Montserrat).
Licensed under the SIL Open Font License, Version 1.1: http://scripts.sil.org/OFL
It is served from here, not from a CDN, because the site sits behind Cloudflare Access.

Made with: `pyftsubset montserrat-medium.ttf --unicodes="U+0020-007E,U+00B7,U+2013,U+2014" --flavor=woff2 --layout-features=kern`
