#!/usr/bin/env bash
# The learning half of one expert-iteration round: self-play games (selfplay.py runs, each with an index.jsonl) ->
# F2 records of the expert's own turns with every label (consequences, lineage intake, the longest dragon's growth, the
# team labels; rounds 300+ sampled 3x) -> train_f1.py from the last net, on the demonstration records (RL_DEMO, optional)
# plus the self-play records at RL_SP_SHARE of the weight.
#
#   RL_ROOT=~/rl_runs NCPUS=8 RL_NAME=ei1 RL_SP=~/rl_runs/sp/it1a:~/rl_runs/sp/it1b \
#   RL_INIT=~/rl_runs/f2/model.pt RL_DEMO="$HOME/rl_runs/f2/f2_*.bin" rl/selfplay/ei_round.sh
#
# Variables (defaults in brackets):
#   RL_ROOT [./rl_runs]    working folder: bin/ (featurizers), RL_NAME/ (records, model, f1_weights.hpp)
#   NCPUS [8]              parallel record shards and torch threads
#   RL_NAME [ei1]          this round's output folder under RL_ROOT
#   RL_SP (required)       colon-separated selfplay.py output folders
#   RL_INIT (required)     the model.pt to continue from (an F2 net with --mem 24 and six Q heads)
#   RL_DEMO                glob of demonstration F2 records (build_f2.py --aux --returns --lineage --team-labels)
#   RL_SP_SHARE [0.2]      the self-play records' share of the weight; RL_SP_POLICY [1.0]: x their policy weight
#                          (0: they train the value / Q heads only); RL_SP_LOSER [0.1]: their policy weight in lost games
#   RL_AWR_BETA [0]        advantage weighting of the self-play policy loss (0: off, the game result is the signal)
#   RL_EXPERT              the expert's model.pt: with RL_EXPERT_TARGET agree | rerank | soft (see train_f1.py)
#   RL_EPOCHS [1] RL_LR [5e-4] RL_HOURS [1.0] RL_MOVE1 [0.1] RL_SEED [0] RL_CHANNELS [64] RL_HIDDEN [128] (= RL_INIT's)
#   RL_MIN_SAMPLES [10000]  PYTHON [python3] (an interpreter with numpy, pycapnp and torch)
# What we learned running it (rl/README.md, lessons): self-play targets for the POLICY did not help in our rounds; keep
# the demonstrations as the anchor, use self-play mainly for the value / Q heads, and judge every new net on two seeds.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
RL=$(cd "$HERE/.." && pwd)
ROOT=${RL_ROOT:-./rl_runs}
NCPUS=${NCPUS:-8}
: "${RL_SP:?set RL_SP to the selfplay.py output folders (colon separated)}"
: "${RL_INIT:?set RL_INIT to the model.pt to continue from}"
O=$ROOT/${RL_NAME:-ei1}
mkdir -p "$O/src" "$ROOT/bin"
PY=${PYTHON:-python3}

: > "$O/src/index.jsonl"
IFS=: read -r -a SP <<< "$RL_SP"
for d in "${SP[@]}"; do cat "$d/index.jsonl" >> "$O/src/index.jsonl"; done
echo "round ${RL_NAME:-ei1}: $(wc -l < "$O/src/index.jsonl") expert game sides"

BIN=$ROOT/bin/featurize_f2
if [ ! -x "$BIN" ] || [ "$BIN" -ot "$RL/featurize/featurize_f2.cpp" ] || [ "$BIN" -ot "$RL/runtime/f2_memory.hpp" ]; then
  "$RL/featurize/build.sh" "$ROOT/bin"
fi
NS=$NCPUS
for s in $(seq 0 $((NS - 1))); do
  [ -s "$O/sp_$s.bin" ] && continue
  "$PY" "$RL/data/build_f2.py" --data "$O/src" --featurize "$BIN" --nmem 24 --shard "$s" --nshards "$NS" \
    --aux --returns --lineage --team-labels --late-boost 3 --move1-rate "${RL_MOVE1:-0.1}" --out "$O/sp_$s.bin" \
    > "$O/build_$s.log" 2>&1 &
done
wait
grep -h "^shard" "$O"/build_*.log || true

DEMO=()
[ -n "${RL_DEMO:-}" ] && DEMO=("$RL_DEMO@24")
EXPERT=()
[ -n "${RL_EXPERT:-}" ] && EXPERT=(--expert-model "$RL_EXPERT" --expert-target "${RL_EXPERT_TARGET:-agree}")
NCPUS=$NCPUS "$PY" "$RL/train/train_f1.py" ${DEMO[@]+"${DEMO[@]}"} --sp "$O/sp_*.bin@24" \
  --sp-share "${RL_SP_SHARE:-0.2}" --sp-policy "${RL_SP_POLICY:-1.0}" --sp-loser-weight "${RL_SP_LOSER:-0.1}" \
  --awr-beta "${RL_AWR_BETA:-0}" ${EXPERT[@]+"${EXPERT[@]}"} --seed "${RL_SEED:-0}" \
  --mem 24 --q-team --q-early-w 0.3 --init "$RL_INIT" --out "$O" --channels "${RL_CHANNELS:-64}" --hidden "${RL_HIDDEN:-128}" \
  --min-samples "${RL_MIN_SAMPLES:-10000}" \
  --epochs "${RL_EPOCHS:-1}" --hours "${RL_HOURS:-1.0}" --lr "${RL_LR:-5e-4}" 2>&1 | tee "$O/train.log"
[ -s "$O/f1_weights.hpp" ] && echo "round ready: $O/f1_weights.hpp (F1_NPRIV 24: use with rl/runtime/f2_run.hpp)"
