#!/bin/bash
# A census on a PBS cluster: every candidate against every opponent, each pair as two arena jobs (the MAPS x both sides
# x REPEAT games, and PVN generated variant maps of maps/PVDIR x both sides) plus, when $LAB_POSTPROCESS is set, a
# post-processing job that waits for both. Run it on the submit host, from the repository root:
#   bash infra/pbs/census.sh TAG "cand1 cand2" "opp1 opp2" [REPEAT]
# Environment (or the env file named by LAB_ENV): MAPS (colon-separated; default: the default map set of the map sets
# file), PVN (default 0), PVDIR (default pv), SEEDBASE, FORK=1, NCPUS (per arena job, default 12), MEM (default
# 56gb), WALLTIME (default 04:00:00), PBS_QUEUE, LAB_SCRATCH.
# Results: $LAB_SCRATCH/census/TAG/<cand>_vs_<opp>/{orig,pv}/ (summary.txt, cpu.txt, replays/) and <cand>_vs_<opp>.json
# from the hook - the layout lab.py collects, so `infra/sweep/arms.py report --lab-dir` and cpu_check.py read both.
# Every candidate of a census plays the same seeds (paired seeds); include a renamed copy of your baseline as one of
# the candidates and read the others as differences to it.
set -u
TAG=$1; CANDS=$2; OPPS=$3; REPEAT=${4:-2}
REPO=$(pwd)
[ -f infra/pbs/arena.pbs ] || { echo "run from the repository root"; exit 1; }
if [ -n "${LAB_ENV:-}" ]; then set -a; . "$LAB_ENV"; set +a; fi
PY=${LAB_PYTHON:-python3}
SCRATCH=${LAB_SCRATCH:-$REPO/data/pbs}
if [ -z "${MAPS:-}" ]; then
  MAPS=$($PY -c "import sys; sys.path.insert(0, 'infra/lab'); import labconf; labconf.load_env(); s, d = labconf.load_mapsets(); print(s[d][0].replace(' ', ':'))")
fi
PVN=${PVN:-0}; PVDIR=${PVDIR:-pv}
NCPUS=${NCPUS:-12}; MEM=${MEM:-56gb}; WALLTIME=${WALLTIME:-04:00:00}
Q=(); [ -n "${PBS_QUEUE:-}" ] && Q=(-q "$PBS_QUEUE")
POST=0; [ -n "${LAB_POSTPROCESS:-}" ] && POST=1
COMMON="REPO=$REPO,EXTRA=--sandbox,INLINE_POST=0,KEEP_REPLAYS=$POST"
[ -n "${LAB_ENV:-}" ] && COMMON="$COMMON,LAB_ENV=$LAB_ENV"
[ -n "${SEEDBASE:-}" ] && COMMON="$COMMON,SEEDBASE=$SEEDBASE"
[ "${FORK:-0}" = 1 ] && COMMON="$COMMON,FORK=1"
D=$SCRATCH/census/$TAG
mkdir -p "$D"
for c in $CANDS; do
  for o in $OPPS; do
    [ "$c" = "$o" ] && continue
    P=$D/${c}_vs_${o}
    rm -rf "$P"
    jobs=()
    if [ "$REPEAT" -gt 0 ]; then
      j=$(qsub ${Q[@]+"${Q[@]}"} -N bc-census -l "select=1:ncpus=$NCPUS:mem=$MEM" -l "walltime=$WALLTIME" \
          -v "$COMMON,A=$c,B=$o,MAPS=$MAPS,REPEAT=$REPEAT,OUTDIR=$P/orig" infra/pbs/arena.pbs | tail -1)
      jobs+=("$j")
    fi
    if [ "$PVN" -gt 0 ]; then
      j=$(qsub ${Q[@]+"${Q[@]}"} -N bc-census -l "select=1:ncpus=$NCPUS:mem=$MEM" -l "walltime=$WALLTIME" \
          -v "$COMMON,A=$c,B=$o,MAPDIR=$PVDIR,MAPN=$PVN,REPEAT=1,OUTDIR=$P/pv" infra/pbs/arena.pbs | tail -1)
      jobs+=("$j")
    fi
    msg="$c vs $o: arena ${jobs[*]+${jobs[*]}}"
    if [ "$POST" = 1 ] && [ ${#jobs[@]} -gt 0 ]; then
      # full job ids in the dependency list: a shortened id may never match and leaves the job held forever
      dep=$(IFS=:; echo "${jobs[*]}")
      PV=REPO=$REPO,CAND=$c,PAIR=$P,RUNS=orig:pv
      [ -n "${LAB_ENV:-}" ] && PV="$PV,LAB_ENV=$LAB_ENV"
      j=$(qsub ${Q[@]+"${Q[@]}"} -W "depend=afterany:$dep" -v "$PV" infra/pbs/postprocess.pbs | tail -1)
      msg="$msg -> postprocess $j"
    fi
    echo "$msg"
  done
done
