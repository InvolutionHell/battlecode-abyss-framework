#!/bin/bash
# One pass of keep_islands.sh, on the PBS submit host from the repository root: every island index listed in
# $EVO_ROOT/islands_active.txt (one index per line; pause an island by removing its line) that has no queued or running
# bc-island job is submitted again as a plain job (qsub -v ISLAND_INDEX=i ... infra/pbs/islands.pbs). Islands
# checkpoint every generation, so a resubmitted index resumes where its last job stopped (walltime, preemption).
set -u
[ -f infra/pbs/islands.pbs ] || { echo "run from the repository root"; exit 1; }
if [ -n "${LAB_ENV:-}" ]; then set -a; . "$LAB_ENV"; set +a; fi
EVO=${EVO_ROOT:-${LAB_SCRATCH:-$PWD/data/pbs}/evo}
ACTIVE=$EVO/islands_active.txt
[ -f "$ACTIVE" ] || exit 0
ISLANDS=${ISLANDS:-infra/tuning/islands.example.txt}
Q=(); [ -n "${PBS_QUEUE:-}" ] && Q=(-q "$PBS_QUEUE")
# the ISLAND_INDEX of every queued (Q) or running (R) bc-island job of this user (PBS Pro `qstat -u` layout:
# state in column 10; the index is read from the job's variable list)
running=$(for j in $(qstat -u "$USER" 2> /dev/null | awk '/bc-island/ && ($10 == "R" || $10 == "Q") {sub(/[.].*/, "", $1); print $1}'); do
  qstat -f "$j" 2> /dev/null | tr -d '\n\t ' | grep -oE 'ISLAND_INDEX=[0-9]+' | head -1 | cut -d= -f2
done | sort -n | uniq)
for i in $(grep -oE '^[0-9]+' "$ACTIVE"); do
  if ! echo "$running" | grep -qx "$i"; then
    name=$(grep -v '^#' "$ISLANDS" | grep -v '^[[:space:]]*$' | sed -n "$((i + 1))p" | cut -d' ' -f1)
    V="REPO=$PWD,ISLAND_INDEX=$i,EVO_ROOT=$EVO,ISLANDS=$ISLANDS"
    [ -n "${LAB_ENV:-}" ] && V="$V,LAB_ENV=$LAB_ENV"
    id=$(qsub ${Q[@]+"${Q[@]}"} -v "$V" infra/pbs/islands.pbs 2>&1)
    echo "resubmitted island index $i ($name): $id"
  fi
done
