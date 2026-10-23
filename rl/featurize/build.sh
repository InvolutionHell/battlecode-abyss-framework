#!/usr/bin/env bash
# Build the offline featurizers (native, any C++17 compiler).
#   rl/featurize/build.sh [OUT_DIR]        # default: $RL_ROOT/bin, or ./rl_runs/bin without RL_ROOT
# Produces featurize_rl, featurize_f1, featurize_f2 (24 memory inputs) and featurize_f2h (44 inputs, -DF2_HEARD).
# -ffp-contract=off keeps the native build from fusing multiply-adds that the WASM build does not fuse.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-${RL_ROOT:-./rl_runs}/bin}
CXX=${CXX:-c++}
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O2 -ffp-contract=off)
"$CXX" "${FLAGS[@]}" "$HERE/featurize_rl.cpp" -o "$OUT/featurize_rl"
"$CXX" "${FLAGS[@]}" "$HERE/featurize_f1.cpp" -o "$OUT/featurize_f1"
"$CXX" "${FLAGS[@]}" "$HERE/featurize_f2.cpp" -o "$OUT/featurize_f2"
"$CXX" "${FLAGS[@]}" -DF2_HEARD "$HERE/featurize_f2.cpp" -o "$OUT/featurize_f2h"
echo "featurizers in $OUT"
