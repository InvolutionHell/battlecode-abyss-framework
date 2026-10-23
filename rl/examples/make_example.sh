#!/usr/bin/env bash
# End-to-end example of the learning pipeline on data with no third-party origin: self-play games of the released bot
# with both networks switched off, records, two tiny nets, int16 export, a bot that carries the new nets, and the
# train / serve consistency test of that bot's own code against the reference forward.
#
#   rl/examples/make_example.sh [N]       N games per (map, side) (default 1: one map, two games)
#
# Environment (defaults in brackets):
#   RL_ROOT [./rl_runs/example]   everything goes here: ws/ (an unswbc workspace with copies of the bots and the
#                                 maps), selfplay/, bin/, data/, run_rl/, run_f1/, vectors/
#   MAPS ["arena"]                maps to play (names without .map), taken from MAPS_DIR, bots/maps or the toolkit
#   NCPUS [4]                     parallel games and torch threads
#   SANDBOX [0]                   1: play in the judge's WASM sandbox (slower, judge-exact CPU metering)
#   CHANNELS [16] HIDDEN [32]     size of the tiny nets (the released ones are 64 / 128)
#   PLAY_EXAMPLE [1]              0: skip the final game of the bot carrying the new nets
#   PYTHON [uv run ...]           the Python to use; by default uv provides numpy, pycapnp and torch
# Needs: unswbc on PATH (or UNSWBC), a C++20 compiler (unswbc builds the bot natively with -std=c++20; the featurizers
# need only C++17), uv (or PYTHON with numpy, pycapnp and torch installed).
#
# Steps:
#   1. bots: ws/abyss_nonet = bots/abyss built with -DBC_RL_PRIOR=0 -DBC_R4NET=0, written as #undef / #define lines at
#      the end of its ablation.hpp (unswbc passes no compiler flags, and ablation.hpp sets BC_R4NET itself, so a
#      command-line define would not reach it), and both weight files deleted, so the build proves that no network and
#      no imitation-trained weight takes part in the games.
#   2. self-play: abyss_nonet against a twin copy of itself (rl/selfplay/selfplay.py), fixed seeds.
#   3. records: featurizers (rl/featurize/build.sh), 9-class records (build_rl.py) and F1 records of the non-queen
#      dragons (build_f1.py --no-queen).
#   4. training: pretrain.py (9-class policy / value), then train_f1.py warm-started from it.
#   5. export: export_bot.py -> rl_weights.hpp (int16 rows); train_f1.py writes f1_weights.hpp itself.
#   6. ws/abyss_example = bots/abyss with the two new nets (make_arm.py), compiled and played once.
#   7. consistency: make_vectors.py on the self-play replays, check.cpp compiled against ws/abyss_example's own
#      io.hpp / rl_features.hpp / r4net.hpp / rl_net.hpp: PASS means the bot computes exactly what training saw.
set -euo pipefail
N=${1:-1}
HERE=$(cd "$(dirname "$0")" && pwd)
RL=$(cd "$HERE/.." && pwd)
REPO=$(cd "$RL/.." && pwd)
ROOT=${RL_ROOT:-./rl_runs/example}
mkdir -p "$ROOT"
ROOT=$(cd "$ROOT" && pwd)
NCPUS=${NCPUS:-4}
MAPS=${MAPS:-arena}
C=${CHANNELS:-16}
H=${HIDDEN:-32}
export NCPUS PYTHONDONTWRITEBYTECODE=1
if [ -n "${PYTHON:-}" ]; then
  read -r -a PY <<< "$PYTHON"
else
  PY=(uv run -q --with numpy --with pycapnp --with torch python)
fi
WS=$ROOT/ws
step() { printf '\n== %s\n' "$*"; }

step "1. bots: abyss_nonet (no networks, no weight files) in $WS"
mkdir -p "$WS/maps"
for m in $MAPS; do  # the maps: MAPS_DIR, else bots/maps, else the toolkit's own (`unswbc maps` copies them)
  [ -f "$WS/maps/$m.map" ] && continue
  src=${MAPS_DIR:-$REPO/bots/maps}/$m.map
  if [ -f "$src" ]; then cp "$src" "$WS/maps/"; else "${UNSWBC:-unswbc}" maps "$WS/maps" 2>&1 | grep -v "is out\|pip install" || true; fi
  [ -f "$WS/maps/$m.map" ] || { echo "no map $m (set MAPS_DIR)" >&2; exit 1; }
done
"${PY[@]}" "$RL/integration/make_arm.py" abyss_nonet --no-nets --bots "$REPO/bots" --out-bots "$WS"
for f in rl_weights.hpp r4_weights.hpp; do
  [ ! -e "$WS/abyss_nonet/$f" ] || { echo "abyss_nonet still has $f" >&2; exit 1; }
done

step "2. self-play: $N game(s) per side on: $MAPS"
SB=()
[ "${SANDBOX:-0}" = 1 ] && SB=(--sandbox)
# shellcheck disable=SC2086
"${PY[@]}" "$RL/selfplay/selfplay.py" --bot abyss_nonet --opps abyss_nonet --bots "$WS" --maps $MAPS --repeat "$N" \
  -j "$NCPUS" --seed-base 1 --out "$ROOT/selfplay" ${SB[@]+"${SB[@]}"}

step "3. records"
"$RL/featurize/build.sh" "$ROOT/bin"
mkdir -p "$ROOT/data"
cp "$ROOT/selfplay/index.jsonl" "$ROOT/data/index.jsonl"
"${PY[@]}" "$RL/data/build_rl.py" --data "$ROOT/data" --featurize "$ROOT/bin/featurize_rl" --shard 0 --nshards 1 \
  --sample 1 --max-round 500 --out "$ROOT/data/rl_0.bin"
"${PY[@]}" "$RL/data/build_f1.py" --data "$ROOT/data" --featurize "$ROOT/bin/featurize_f1" --shard 0 --nshards 1 \
  --no-queen --move1-rate 0.25 --out "$ROOT/data/f1_0.bin"

step "4. training (tiny nets: C $C, H $H)"
"${PY[@]}" "$RL/train/pretrain.py" "$ROOT/data/rl_0.bin" --out "$ROOT/run_rl" --channels "$C" --hidden "$H" \
  --epochs 3 --batch 512 --min-samples 1
"${PY[@]}" "$RL/train/train_f1.py" "$ROOT/data/f1_0.bin" --out "$ROOT/run_f1" --channels "$C" --hidden "$H" \
  --epochs 3 --batch 512 --min-samples 1 --init "$ROOT/run_rl/model.pt"

step "5. export"
"${PY[@]}" "$RL/train/export_bot.py" "$ROOT/run_rl/weights_rl.hpp" "$ROOT/run_rl/rl_weights.hpp"
head -c 200 "$ROOT/run_f1/f1_weights.hpp"; echo

step "6. ws/abyss_example: the released bot with the two new nets"
"${PY[@]}" "$RL/integration/make_arm.py" abyss_example --bots "$REPO/bots" --out-bots "$WS" \
  --rl-weights "$ROOT/run_rl/rl_weights.hpp" --r4-weights "$ROOT/run_f1/f1_weights.hpp"
if [ "${PLAY_EXAMPLE:-1}" = 1 ]; then
  first=$(echo "$MAPS" | awk '{print $1}')
  (cd "$WS" && "${UNSWBC:-unswbc}" run "maps/$first.map" abyss_example abyss_nonet --no-replay ${SB[@]+"${SB[@]}"}) | grep -E 'wins|draw|points per turn'
fi

step "7. consistency of ws/abyss_example's own code with the reference forward"
# shellcheck disable=SC2046
"${PY[@]}" "$RL/eval/consistency/make_vectors.py" $(ls "$ROOT"/selfplay/*/replays/*.replay* | head -2) \
  --featurize "$ROOT/bin/featurize_f2" --out "$ROOT/vectors" --dragons 3 \
  --weights r4="$WS/abyss_example/r4_weights.hpp" --weights rl="$WS/abyss_example/rl_weights.hpp"
CXX=${CXX:-c++}
FLAGS=(-std=c++17 -O2 -ffp-contract=off)
"$CXX" "${FLAGS[@]}" -DF1_NET_HEADER='"r4net.hpp"' -I "$WS/abyss_example" -I "$RL/runtime" \
  "$RL/eval/consistency/check.cpp" -o "$ROOT/check_r4"
"$CXX" "${FLAGS[@]}" -DCHECK_RLNET -I "$WS/abyss_example" "$RL/eval/consistency/check.cpp" -o "$ROOT/check_rl"
"$ROOT/check_r4" "$ROOT/vectors/turns.txt" "$ROOT/vectors/expect_r4.txt"
"$ROOT/check_rl" "$ROOT/vectors/turns.txt" "$ROOT/vectors/expect_rl.txt"
step "done: $ROOT"
