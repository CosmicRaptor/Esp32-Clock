#!/usr/bin/env bash
# Runs the portal's shared `when` test vectors (portal/tests/vectors/face_rules.json) through the firmware's rules
# (face_rules.h). Needs a C++ compiler, Python 3 and the Nanopb library (NANOPB=... to say where).
set -euo pipefail
cd "$(dirname "$0")"
NANOPB="${NANOPB:-$HOME/Documents/Arduino/libraries/Nanopb}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
c++ -std=c++17 -O1 -Wall -I../firmware/BhoomiClock -I"$NANOPB" check_face_rules.cpp -o "$tmp/check"
python3 - "$tmp" <<'PY'
import json, subprocess, sys
tmp = sys.argv[1]
cases = json.load(open("../portal/tests/vectors/face_rules.json"))["matches"]
lines = []
for c in cases:
    n, w = c["now"], c["when"]
    h = w["hours"] or {"from": 0, "to": 0}
    months = sum(1 << (m - 1) for m in w["months"])
    days = sum(1 << d for d in w["weekdays"])
    dates = "".join(f" {d['month']} {d['day']} {d['year'] or 0}" for d in w["dates"])
    lines.append(f"{n['year']} {n['month']} {n['day']} {n['hour']} {n['weekday']} {h['from']} {h['to']} {months} {days} {len(w['dates'])}{dates}")
out = subprocess.run([tmp + "/check"], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.split()
bad = [c["name"] for c, o in zip(cases, out) if (o == "1") != c["match"]]
print(f"{len(cases)} cases, {len(bad)} wrong", *bad, sep="\n  " if bad else "")
sys.exit(1 if bad or len(out) != len(cases) else 0)
PY
