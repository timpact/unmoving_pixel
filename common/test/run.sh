#!/bin/sh
# Builds and runs the unit tests of common/ (and dash/app) on the host.
#   sh common/test/run.sh
set -e
cd "$(dirname "$0")/../.."
out="${TMPDIR:-/tmp}/unmoving-pixel-tests"
mkdir -p "$out"
flags="-Wall -Wextra -std=c11 -fsanitize=address,undefined -I common/include"
cc $flags common/test/test_proto.c common/src/up_proto.c -o "$out/test_proto"
cc $flags common/test/test_lights.c common/src/up_lights.c -lm -o "$out/test_lights"
cc $flags common/test/test_preset.c -o "$out/test_preset"
cc $flags -I dash/app dash/app/test/test_measurement.c dash/app/measurement.c -o "$out/test_measurement"
cc $flags -I dash/app -I dash/ui dash/app/test/test_dash_state.c dash/app/dash_state.c dash/app/measurement.c -o "$out/test_dash_state"
for t in test_proto test_lights test_preset test_measurement test_dash_state; do
    "$out/$t"
done
