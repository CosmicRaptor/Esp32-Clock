#!/usr/bin/env bash
# Regenerate code from clock.proto. Needs `protoc` and `uv` (brew install protobuf uv).
# The generated files are committed, so this only needs running after editing the .proto/.options.
set -euo pipefail
cd "$(dirname "$0")"

NANOPB_VERSION=0.4.9.1   # must match the Nanopb Arduino library the firmware builds against

# Python, for the Worker. protoc's major version must not be newer than the protobuf package
# that pywrangler vendors (see portal/pylock.toml).
protoc -I . --python_out=../portal/src --pyi_out=../portal/src clock.proto

# C, for the firmware. Arduino only compiles the sketch folder (and src/), so the output goes
# straight into the sketch root: BhoomiClock/clock.pb.{c,h}.
uvx --from "nanopb==${NANOPB_VERSION}" nanopb_generator -I . -D .. clock.proto

echo "generated: portal/src/clock_pb2.py(i), clock.pb.{c,h} (sketch root)"
