#!/bin/bash
# One evaluation pair on a worker node, launched (detached) by infra/lab/lab.py:
#   CAND vs OPP on MAPS x both sides x REPEAT, plus the first PVN maps of bots/maps/PVDIR x both sides, all --sandbox.
#
#   pair.sh TAG CAND OPP NCPUS REPEAT "map1 map2 ..." PVN [FORK] [SEEDBASE] [PVDIR]
#
#   FORK=1       both sides of a (map, repeat) share one seed (arena.py --fork): a candidate that only changes the end
#                game plays the same game as its control until the change.
#   SEEDBASE     "-" keeps arena.py's default seed base; another base gives games distinct from every other run.
#   PVDIR        maps/<dir> for the second arena process (default pv: generated variants of the maps).
#
# Results land in <checkout>/lab/TAG/CAND_vs_OPP/{orig,pv}/ (summary.txt, cpu.txt, replays/) with a STATUS file and a
# DONE marker, which lab.py polls and collects. Optional hook: LAB_POSTPROCESS, a command run as
#   bash -c "$LAB_POSTPROCESS" _ CAND PAIR_DIR RUN_DIR...
# after the games (e.g. a replay KPI script writing PAIR_DIR.json, which lab.py also collects).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export PATH=$HOME/.local/bin:$PATH
TAG=$1; CAND=$2; OPP=$3; NCPUS=$4; REPEAT=$5; MAPS=$6; PVN=$7; FORK=${8:-0}; SEEDBASE=${9:--}; PVDIR=${10:-pv}
PY=${LAB_PYTHON:-python3}
BOTS=${BC_BOTS_DIR:-$ROOT/bots}
ARGS=()
[ "$FORK" = 1 ] && ARGS+=(--fork)
[ "$SEEDBASE" != "-" ] && ARGS+=(--seed-base "$SEEDBASE")
cd "$ROOT" || exit 1
D=$ROOT/lab/$TAG/${CAND}_vs_${OPP}
mkdir -p "$D"
echo "start $(date +%s) host=$(hostname -s) ncpus=$NCPUS repeat=$REPEAT pvn=$PVN fork=$FORK seedbase=$SEEDBASE pvdir=$PVDIR" > "$D/STATUS"
NM=$(echo $MAPS | wc -w)
GO=$((2 * NM * REPEAT)); GP=$((2 * PVN))
# split the cores between the two arena processes in proportion to their games
if [ $GO -eq 0 ]; then
  JO=0; JP=$NCPUS            # REPEAT 0: only the PVDIR maps
elif [ $GP -gt 0 ]; then
  JO=$(( NCPUS * GO / (GO + GP) )); [ $JO -lt 1 ] && JO=1
  JP=$(( NCPUS - JO )); [ $JP -lt 1 ] && JP=1
else
  JO=$NCPUS; JP=0
fi
[ $JO -gt $GO ] && JO=$GO
[ $JP -gt $GP ] && JP=$GP
# One unswbc cache per arena process: processes sharing a cache raced on purged or half-written wasm files. The
# compiled judge toolchain (clang / wasm-ld .cwasm, about a minute to compile) stays shared through symlinks.
CACHE_ROOT=${TMPDIR:-/tmp}
SHARED_CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/unswbc   # read before mkcache points XDG_CACHE_HOME elsewhere
mkcache() {
  export XDG_CACHE_HOME=$CACHE_ROOT/xdg-$TAG-$CAND-$OPP-$1-$$
  mkdir -p "$XDG_CACHE_HOME/unswbc"
  for f in "$SHARED_CACHE"/clang-*.cwasm "$SHARED_CACHE"/wasm-ld-*.cwasm; do
    [ -e "$f" ] && ln -sf "$f" "$XDG_CACHE_HOME/unswbc/"
  done
}
P1=
if [ $GO -gt 0 ]; then
  mkcache orig
  $PY infra/lab/arena.py "$CAND" "$OPP" --bots-dir "$BOTS" -j $JO -o "$D/orig" --sandbox --maps $MAPS \
      --repeat "$REPEAT" "${ARGS[@]+"${ARGS[@]}"}" > "$D/orig.log" 2>&1 &
  P1=$!
fi
P2=
if [ $GP -gt 0 ]; then
  [ -n "$P1" ] && sleep 75   # let the first process finish its native warm-up build of the shared bot folders
  mkcache pv
  $PY infra/lab/arena.py "$CAND" "$OPP" --bots-dir "$BOTS" -j $JP -o "$D/pv" --sandbox --map-dir "$PVDIR" \
      --map-count "$PVN" "${ARGS[@]+"${ARGS[@]}"}" > "$D/pv.log" 2>&1 &
  P2=$!
fi
RC1=0; [ -n "$P1" ] && { wait $P1; RC1=$?; }
RC2=0; [ -n "$P2" ] && { wait $P2; RC2=$?; }
echo "arena done $(date +%s) rc=$RC1/$RC2" >> "$D/STATUS"
RUNS=""; [ $GO -gt 0 ] && RUNS="$D/orig"; [ $GP -gt 0 ] && RUNS="$RUNS $D/pv"
if [ -n "${LAB_POSTPROCESS:-}" ]; then
  bash -c "$LAB_POSTPROCESS" _ "$CAND" "$D" $RUNS > "$D/postprocess.log" 2>&1 || echo "postprocess failed" >> "$D/STATUS"
fi
rm -rf "$CACHE_ROOT"/xdg-$TAG-$CAND-$OPP-*-$$
# keep the replays compressed (lossless): uncompressed, a few large censuses fill a disk quota
if command -v zstd > /dev/null; then
  for r in $RUNS; do zstd -q -6 --rm "$r"/replays/*.replay 2> /dev/null || true; done
fi
echo "done $(date +%s)" >> "$D/STATUS"
touch "$D/DONE"
