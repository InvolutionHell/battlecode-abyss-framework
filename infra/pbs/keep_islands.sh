#!/bin/bash
# Keep the tuning islands running across PBS walltimes: every EVERY seconds (default 600) run resubmit_islands.sh,
# locally (on the submit host) or, with PBS_SUBMIT_HOST set, over ssh in the checkout PBS_REPO on that host.
#   mkdir -p data && nohup bash infra/pbs/keep_islands.sh >> data/keep_islands.log 2>&1 &
# Environment (or the env file named by LAB_ENV): EVO_ROOT, ISLANDS, PBS_QUEUE, PBS_SUBMIT_HOST (an ssh alias),
# PBS_REPO (the repository checkout on that host), EVERY. Stop it with kill; pause one island by removing its index
# from $EVO_ROOT/islands_active.txt.
set -u
if [ -n "${LAB_ENV:-}" ]; then set -a; . "$LAB_ENV"; set +a; fi
EVERY=${EVERY:-600}
while true; do
  if [ -n "${PBS_SUBMIT_HOST:-}" ]; then
    out=$(ssh -o BatchMode=yes -o ConnectTimeout=20 "$PBS_SUBMIT_HOST" \
          "cd ${PBS_REPO:?set PBS_REPO to the checkout on $PBS_SUBMIT_HOST} && LAB_ENV=${LAB_ENV:-} EVO_ROOT=${EVO_ROOT:-} ISLANDS=${ISLANDS:-} bash infra/pbs/resubmit_islands.sh" 2>&1)
  else
    out=$(bash infra/pbs/resubmit_islands.sh 2>&1)
  fi
  [ -n "$out" ] && echo "[$(date '+%Y-%m-%d %H:%M')] $out"
  sleep "$EVERY"
done
