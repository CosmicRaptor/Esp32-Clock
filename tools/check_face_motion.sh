#!/usr/bin/env bash
# Checks that the firmware's face motion maths (face_motion.h) gives the same numbers as the portal's JS
# (portal/public/face-motion.js). Needs a C++ compiler, Node and the Nanopb library (see portal/README.md).
set -euo pipefail
cd "$(dirname "$0")"
NANOPB="${NANOPB:-$HOME/Documents/Arduino/libraries/Nanopb}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
c++ -std=c++17 -O1 -Wall -I.. -I"$NANOPB" check_face_motion.cpp -o "$tmp/check"
"$tmp/check" > "$tmp/c.txt"
node check_face_motion.js > "$tmp/js.txt"
python3 - "$tmp/c.txt" "$tmp/js.txt" <<'PY'
import sys
a, b = (open(p).read().split("\n") for p in sys.argv[1:3])
bad = 0
if len(a) != len(b): print("different number of lines", len(a), len(b)); bad = 1
for x, y in zip(a, b):
    xs, ys = x.split(), y.split()
    ok = len(xs) == len(ys) and all(p == q or (abs(float(p) - float(q)) < 0.0015) for p, q in zip(xs, ys))
    # a pixel that lands on a rounding boundary may differ by one between libm and V8: allow it for x/y of particles
    if not ok and xs[:1] == ["part"] and len(xs) == len(ys) and xs[:4] == ys[:4] and xs[6] == ys[6] and all(abs(int(p) - int(q)) <= 1 for p, q in zip(xs[4:6], ys[4:6])):
        ok = True
    if not ok:
        bad += 1
        print("C :", x); print("JS:", y)
print(f"{len(a) - 1} lines compared, {bad} different")
sys.exit(1 if bad else 0)
PY
