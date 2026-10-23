# Learning pipeline (`rl/`)

[简体中文](README.zh-CN.md)

The final bot (`bots/abyss`) is a per-dragon lookahead search with hand-written evaluation terms. Two small
convolutional networks, trained by imitation, sit inside that search as **priors**: they nudge the choice of the first
step, give a split trigger and gate free multi-step moves. The search keeps the last word on tactics (collisions,
traps, portals, the queen's safety), which also limits the harm a wrong network can do.

This folder holds everything needed to build such networks again from replays: the featurizers (the bot's own C++
parser and feature code), the record builders, the training and export scripts, the in-bot runtime headers, the
self-play and evaluation tools, a train / serve consistency test, an end-to-end example and unit tests.

- [What is in the released bot](#what-is-in-the-released-bot)
- [Pipeline](#pipeline)
- [Quick start: the end-to-end example](#quick-start-the-end-to-end-example)
- [Integration in the bot](#integration-in-the-bot)
- [Train / serve consistency](#train--serve-consistency)
- [Self-play and expert iteration](#self-play-and-expert-iteration)
- [Evaluation tools](#evaluation-tools)
- [Tests](#tests)
- [Directory map](#directory-map)
- [Lessons learned](#lessons-learned)
- [Glossary](#glossary)

## What is in the released bot

| | opening prior | move prior |
|---|---|---|
| code in the bot | `bots/abyss/rl_net.hpp` (switch `BC_RL_PRIOR`) | `bots/abyss/r4net.hpp` (switch `BC_R4NET`) |
| weights | `bots/abyss/rl_weights.hpp` | `bots/abyss/r4_weights.hpp` |
| net | 9-class policy + split fraction + value (`pretrain.py`) | 16-class policy + split fraction + second step + value (`train_f1.py`) |
| size | conv 17->64, conv 64->64, dense 3,147->128 | the same trunk |
| used by | the queen's process (dragon id 0 / 1), rounds 0-199 | every other dragon, all rounds |
| uses | root move prior (weight 0.05), split trigger (p >= 0.4) | root move prior (weight 0.02), split trigger before round 200 (p >= 0.4), free-step gate (P(multi-step) >= 0.3) |
| training summary (first line of the weights file) | 5.06 M turns of rounds 0-200, 6 epochs; held-out top-1 accuracy 73.1% | 6.0 M sampled non-queen turns, 3 epochs; held-out accuracy 68.3% at natural class frequencies, single-step direction 73.4%, split AUC 0.97 |

**Data statement for both weight files:**
trained by imitation on aggregated public ladder replays of top-ranked teams; contains no per-team model; the training data is not included.

Nothing else learnt is published: no training replays, no records, no other weights. The pipeline below reproduces the method
on your own data; the [example](#quick-start-the-end-to-end-example) builds its data from self-play of the released
bot with both networks switched off, so its output has no third-party origin.

Each network costs about 9 M of the judge's 100 M CPU points per dragon-turn (the forward pass is written so the
compiler vectorises it; the plain version cost about 30 M). Each dragon process builds only the net it uses, once.
The released bot also contains network code that is compiled out by default (class-conditioned nets, a cull rule,
measurement switches): it stays in the source because the bot is published exactly as submitted, and removing code
can change the instruction-metered search. The in-bot side is documented in
[docs/policy/net-priors.md](../docs/policy/net-priors.md).

## Pipeline

```
 .replay files (unswbc run)
      |  abyss_tools.replay.state.turns(): rebuild the game, render the exact round block each dragon received
      v
 protocol records  "REC <game> <id> <team> <w> <h> <unitLimit> <labels...> <action>" + round block + "END"
      |  rl/featurize/featurize_{rl,f1,f2}  (C++: the bot's io.hpp parser + rl_features.hpp featurizer)
      v
 byte records: header (label, length, round, outcome bits, ...) + 17 x 7 x 7 + 10 features (+ memory inputs)
      |  rl/train/pretrain.py (9 classes)   rl/train/train_f1.py (16 classes, optional Q heads)   [torch]
      v
 weights: int16 values with one float scale per output row (export_bot.py / train_f1.py)
      |  rl/runtime/*.hpp = bots/abyss/rl_net.hpp, r4net.hpp
      v
 in the bot: total[d] += w * log p(first step d)  at the search root; split trigger; free-step gate
```

### 1. Replays to protocol blocks

`rl/data/replay_src.py` uses the repository's `abyss_tools` package (`tools/`). `turns(replay, side, sample, rng)`
replays the events, keeps the full state, and at every turn renders the round block the engine sent that dragon
(protocol 3: vision tiles with pearl countdowns, visible bodies, edges, 64-bit sonar values, echo counts), together
with the action it took (`MOVE <dirs>`, `SPLIT <n>`, `NONE`). Rendering text instead of computing features in Python
keeps one implementation of the features: the bot's.

`rl/data/index_replays.py` indexes replay folders (one JSON line per game side: path, bot name, side, the engine's
result). The builders read one shard of an index each, so many can run in parallel.

### 2. Features

`rl/featurize/rl_features.hpp` is byte-identical to `bots/abyss/rl_features.hpp`, and `rl/featurize/io.hpp` has the
same protocol structs and parser as the bot's `io.hpp`; `rl/featurize/check_features.py` verifies both (run it after
any change to either side). The view is rotated so the dragon's facing is "up", so every class is relative to the
facing.

| index | feature (7 x 7 grid, rotated to the facing) |
|---|---|
| 0 | pearl on the tile |
| 1, 2 | pearl bed; exp(-countdown / 8) of its next spawn |
| 3, 4, 5 | own body, a teammate's body, an enemy's body |
| 6, 7 | a teammate's head, an enemy's head |
| 8-11, 12-15 | kelp edges, portal edges, by side relative to the facing (front, right, back, left) |
| 16 | heading of another dragon's head relative to ours, (rel + 1) / 4 |

Scalars (10): round / 500, min(length, 32) / 16, units / 64, min(messages, 4) / 4, the five echo counts (kelp, ally,
ally head, enemy, enemy head) as min(n, 4) / 2, round >= 400. The nets also take the round itself. Features are
stored as bytes, round(clip(v, 0, 2.55) x 100), and the bot quantises its own inputs the same way before the forward
pass, so training and play see identical numbers.

### 3. Records

| builder | featurizer | record | label |
|---|---|---|---|
| `build_rl.py` | `featurize_rl` | 6 + 843 bytes | 9 classes: 0-3 a single step forward / right / back / left, 4-7 a multi-step move by its first step, 8 split (no-action turns are skipped) |
| `build_f1.py` | `featurize_f1` | 16 + 843 bytes | 16 classes: (steps 1 / 2 / 3+) x 4 first-step directions, split n = 2 / 3-4 / 5+, no action; plus split n, second step, final longest-dragon and total-length gaps, the replay's facts at the turn |
| `build_f2.py` | `featurize_f2` (`featurize_f2h`) | 16 + 843 + 24 (44) bytes | as F1, plus the dragon's own memory inputs (`rl/runtime/f2_memory.hpp`), and optionally the consequence labels and returns of `aux_labels.py` |

The exact byte layouts are in the headers of `rl/featurize/featurize_*.cpp`. The outcome byte holds the side's
result (bit 0), the row's weight class (bits 5-6) and, with `--aux`, four consequence labels (bits 1-4: the dragon
splits within 5 turns, eats within 5, dies by enemy contact within 10, an enemy dies on it within 10; bit 7 marks
labelled records). About 95% of all turns are single steps, so `build_f1.py` / `build_f2.py` thin them
(`--move1-rate`, raised x4 before round 100 and x2 before 200) and store each record's keep rate; the trainer weights
by 1 / rate and learns the natural class frequencies. `--no-queen` drops the queens' turns (the released move prior
was trained on the other dragons only). A builder warns when a replay's map text lists no pearl beds: the bed
channels would then be empty.

### 4. Training

`pretrain.py` (9-class net): AdamW with a cosine schedule; cross-entropy on the action class, weighted 1 for the
winning side's turns and `--loser-weight` (0.3) for the losing side's (or advantage-weighted with `--awr-beta`); the
split fraction n / length as a logistic target on split turns; the game result for the value head; optional
auxiliary heads on the consequence labels (`--aux-weight`).

`train_f1.py` (16-class net): the same trunk; records weighted by 1 / keep rate; heads for the 16 classes, the split
fraction, the second step of a multi-step move and three value targets; warm start from a `pretrain.py` model (the
9-class head is spread over the 16 classes) or from another F1 model. Optional: memory inputs (`--mem 24`, F2),
privileged inputs (`--priv`), action-value heads trained on the class that was played (`--q`, `--q-returns`,
`--q-team`), self-play records (`--sp`) with expert-iteration targets. `soup.py` averages replicas of one recipe.

### 5. Export

`export_bot.py` converts `pretrain.py`'s float header into `rl_weights.hpp`: int16 values with one float scale per
output row (argmax agreement with the float net above 99.9% on held-out records; int8 rows gave 98.7%), about half
the source size. `--prefix RLA` / `--prefix RLL` renames the symbols for a second net in one bot (`rl_aux.hpp`,
`rl_late.hpp`). `train_f1.py` writes its `f1_weights.hpp` in the same format directly. Generated headers name their
source by base name only and carry the training metadata on their first line; no local path is written.

### 6. Runtime

| header | what |
|---|---|
| `rl_net.hpp` | `RLNet`, the 9-class net (library copy of `bots/abyss/rl_net.hpp`; `forwardFast` is the pass the bot uses) |
| `rl_prior.hpp` | `g_rl`: the 9-class net as a drop-in move / split prior for any bot that shares `io.hpp` |
| `rl_f1.hpp` | `F1Net`, the 16-class net with every optional head (`bots/abyss/r4net.hpp` is a copy reading `r4_weights.hpp`) |
| `f2_memory.hpp`, `f2_comms.hpp` | the dragon's own memory inputs for F2 nets; the decoder of the bot's sonar facts (`F2_HEARD`) |
| `f2_run.hpp` | `f2Run(net, mem, init, turn)`: the one call per turn the consistency test checks |
| `rl_aux.hpp`, `rl_late.hpp` | a second 9-class net in one bot: auxiliary heads only, or a late-game prior |

## Quick start: the end-to-end example

```sh
cd <repository root>
rl/examples/make_example.sh            # 2 self-play games on Arena; RL_ROOT=./rl_runs/example
MAPS="arena default" NCPUS=8 rl/examples/make_example.sh 3     # 12 games
```

Needs `unswbc` (the official toolkit), a C++20 compiler (unswbc builds the bot natively with -std=c++20; the
featurizers need only C++17) and `uv` (or set `PYTHON` to an interpreter with numpy, pycapnp and torch). The script:

1. copies `bots/abyss` to `$RL_ROOT/ws/abyss_nonet` (`RL_ROOT` defaults to `./rl_runs/example`) built with `-DBC_RL_PRIOR=0 -DBC_R4NET=0`, written as `#undef` / `#define`
   lines at the end of its `ablation.hpp` (unswbc passes no compiler flags, and `ablation.hpp` sets `BC_R4NET` itself,
   so a command-line define would not reach it), and deletes both weight files, so the build itself proves that no
   network and no imitation-trained weight plays;
2. plays it against a twin copy of itself (`rl/selfplay/selfplay.py`, fixed seeds, native or `SANDBOX=1`);
3. builds the featurizers, 9-class records and non-queen F1 records;
4. trains a tiny 9-class net and a tiny F1 net warm-started from it (`CHANNELS` / `HIDDEN`, default 16 / 32);
5. exports them and installs them in `$RL_ROOT/ws/abyss_example` (`rl/integration/make_arm.py`), which is compiled and played;
6. runs the consistency test against `$RL_ROOT/ws/abyss_example`'s own code: both checks must print `PASS`.

The tiny nets are not useful players (a few hundred records); the point is a working, checkable pipeline. For real
training use thousands of game sides, the default sizes (64 / 128) and the sharded builders, for example:

```sh
python3 rl/data/index_replays.py RUNS_DIR --recursive --out DATA/index.jsonl
for s in $(seq 0 15); do python3 rl/data/build_f1.py --data DATA --featurize BIN/featurize_f1 --shard $s --nshards 16 \
    --no-queen --out DATA/f1_$s.bin & done; wait
NCPUS=16 python3 rl/train/train_f1.py "DATA/f1_*.bin" --out RUN --init PRETRAIN_RUN/model.pt --hours 2
python3 rl/integration/make_arm.py abyss_mynet --r4-weights RUN/f1_weights.hpp
```

## Integration in the bot

Per dragon process and turn, before the search and inside its clock (`bots/abyss/policy.hpp`, look for
`BC_RL_PRIOR` and `BC_R4NET`):

- **Opening prior** (the queen's process; rounds below `rlUntil` = 200): `RLNet::run`, then for every legal first step
  `d` the search value gets `wPrior * log(p_single(d) + p_multi(d) + 1e-3)`, `wPrior` = 0.05; `p(split) >= rlSplitP`
  (0.4) is one input of the bot's own split rules.
- **Move prior** (every other dragon): `F1Net::run`, then `BC_R4_W * log(p(d | the dragon moves) + 1e-3)`, with p
  normalised over the legal first steps, `BC_R4_W` = 0.02, rounds `BC_R4_FROM`..`BC_R4_UNTIL` (0..500).
- **Split trigger** (`BC_R4_SPLIT_ON`): before round 200 the F1 net's `splitProb() >= BC_R4_SPLITP` (0.4) replaces
  the 9-class trigger in those processes, so they never build the 9-class net.
- **Free-step gate** (`BC_R4_SPRINT` 6): where the net's P(more than one step | the chosen first step) >=
  `BC_R4_SPRINTP` (0.3), a non-queen dragon of length >= 5 takes up to `BC_R4_MAXSTEPS` (2) free steps along the net's
  second-step head, stopping on a pearl. The queen never does; the path keeps a Chebyshev distance > `BC_R4_QRING`
  (2) from our queen's head and avoids her last free side; the landing must lie outside every known enemy head's
  free-step reach plus a margin (`BC_R4_LAND` 1) and keep room for the dragon (`r4Sprint`).

All switches are compile-time macros in `bots/abyss/ablation.hpp`; `rl/integration/make_arm.py` makes an experiment
copy of the bot with other values or other weights. Trace builds (`BC_DEBUG 1`, `BC_ECON_TRACE 1`) log what the
priors did; `rl/eval/prior_flips.py` summarises the tags.

For another bot, `rl/runtime/rl_prior.hpp` shows the minimal integration of a 9-class net:

```cpp
#include "rl_prior.hpp"                       // + rl_net.hpp, rl_features.hpp, io.hpp, rl_weights.hpp
bc::g_rl.run(init, turn);                     // once per turn, before the search
if (bc::g_rl.priorOn) value[d] += bc::g_rl.w * bc::g_rl.logDir(d, turn.dir);   // each legal first step d
```

## Train / serve consistency

A net only works if every training record is computed exactly as the bot computes its input in play. Two checks:

1. `rl/featurize/check_features.py`: the training featurizer's `rl_features.hpp` equals the bot's, token for token
   (it reports byte identity), and the protocol structs and parser in both `io.hpp` files are the same.
2. `rl/eval/consistency/`: `make_vectors.py` takes whole dragon lives from replays, writes the round blocks
   (`turns.txt`) and, per weights file, the training featurizer's bytes and an independent float64 numpy forward of
   the exported weights (`ref_forward.py`). `check.cpp`, compiled against a bot folder, replays the blocks through
   that bot's own parser, featurizer and network code and compares every turn: feature and memory bytes exactly,
   probabilities within 1e-5, logits within 1e-4 relative.

```sh
rl/featurize/build.sh BIN
python3 rl/eval/consistency/make_vectors.py GAME.replay --featurize BIN/featurize_f2 --out VEC \
    --weights r4=bots/abyss/r4_weights.hpp --weights rl=bots/abyss/rl_weights.hpp
c++ -std=c++17 -O2 -ffp-contract=off -DF1_NET_HEADER='"r4net.hpp"' -I bots/abyss -I rl/runtime \
    rl/eval/consistency/check.cpp -o check_r4 && ./check_r4 VEC/turns.txt VEC/expect_r4.txt
c++ -std=c++17 -O2 -ffp-contract=off -DCHECK_RLNET -I bots/abyss rl/eval/consistency/check.cpp -o check_rl \
    && ./check_rl VEC/turns.txt VEC/expect_rl.txt
```

Both nets of the released bot pass this check, with 0 differing feature bytes (largest errors on the test replays:
probabilities below 1e-6, logits below 1e-5 relative).

## Self-play and expert iteration

`rl/selfplay/selfplay.py` plays a bot against a list of opponents (itself through a twin copy) with
`abyss_tools.run.arena` and indexes that bot's game sides; any record builder can then read them. `ei_round.sh` is
the learning half of an expert-iteration round for F2 nets: F2 records of the expert's own turns with every label,
then `train_f1.py` on demonstration records plus the self-play records at a fixed share of the weight. Configuration
is by environment variables (`RL_ROOT`, `NCPUS`, `RL_SP`, `RL_INIT`, ...; see the script's header). Read the lessons
below before relying on it: in our runs self-play targets did not improve the policy.

## Evaluation tools

| tool | what |
|---|---|
| `eval/probe_nets.cpp` | a 9-class net and an F1 net on the same F1 records: move accuracy and NLL at natural weights, split-probability deciles (to set a split threshold that flags the same share of turns), optional CSV |
| `eval/aux_eval.py` | calibration and AUC of a 9-class net's auxiliary heads by length and phase |
| `eval/q_calib.py` | calibration (ECE, AUC, Brier, reliability table) of an F2 net's qDie head on held-out records |
| `eval/prior_flips.py` | from trace-build replays: how often the move prior changed the search's best step and what it gave up; free-step activity and refusal reasons |
| `integration/make_arm.py` | experiment copies of the bot (switches, weights, `--no-nets`) for paired comparisons with the tools in `infra/` and `tools/` |
| `python -m abyss_tools.replay.diff A B` | the switch-off check (in `tools/`): an arm whose new module is switched off must replay its base's game event for event on the same map and seed |

Compare an arm with a renamed exact copy of its base played on the same seeds, never with absolute numbers: two
identical bots do not score exactly 50% on a fixed set of seeds.

## Tests

```sh
rl/tests/run_tests.sh          # uv provides pytest, numpy, pycapnp, torch
```

`rl/tests/test_rl.py` checks: the featurizer identity (and that the check catches a one-token change); the runtime
headers on tiny dummy weights (`dummy_weights.py`, `runtime_smoke.cpp`: normalised outputs, vectorised pass equal to
the scalar pass, memory inputs); record building on the fixture replays of `tools/tests/fixtures` (layouts, labels,
F1 and F2 records agree); tiny training runs whose int16 exports match the torch models; the consistency test on
dummy nets (with a negative control) and on the released bot's two nets.

## Directory map

```
rl/
  featurize/    rl_features.hpp (== bots/abyss/rl_features.hpp), io.hpp, common.hpp, featurize_{rl,f1,f2}.cpp,
                build.sh, check_features.py
  data/         replay_src.py, index_replays.py, merge_index.py, build_rl.py, build_f1.py, build_f2.py, aux_labels.py
  train/        pretrain.py, train_f1.py, export_bot.py, soup.py
  runtime/      rl_net.hpp, rl_prior.hpp, rl_f1.hpp, f2_memory.hpp, f2_comms.hpp, f2_run.hpp, rl_aux.hpp, rl_late.hpp
  integration/  make_arm.py
  selfplay/     selfplay.py, ei_round.sh
  eval/         probe_nets.cpp, aux_eval.py, q_calib.py, prior_flips.py, consistency/{make_vectors.py, ref_forward.py, check.cpp}
  examples/     make_example.sh
  tests/        test_rl.py, run_tests.sh, dummy_weights.py, runtime_smoke.cpp
```

Configuration is by flags and environment variables only: `RL_ROOT` (working folder of the shell drivers), `NCPUS`
(parallel jobs, torch threads), `ABYSS_BOTS` (the unswbc workspace), `ABYSS_TOOLS_PATH` (where `abyss_tools` lives),
`UNSWBC` (the toolkit executable), `RL_MPS=1` (train on an Apple GPU).

## Lessons learned

What we measured while building and tuning these networks, in paired games on internal benchmarks (mirror games and
a fixed set of sparring opponents) and on held-out replays.

**Priors inside a search**

- *The net nudges, the search decides.* The search values an eaten pearl at about 1. A log-prior weight of 0.3 on
  every move halved the number of dragon-turns in test games: the dragons followed the imitated movement habits
  instead of the pearls. Weights of 1.0, or a strong prior applied only among the near-best moves, lost. 0.02-0.05 on
  every move was the safe range; 0.6 cost about 6 win-rate points.
- *The opening prior pays through economy.* Removing it cost about 10 segments of total length and 23 eaten pearls by
  round 200 in paired games. Most of its value is the first-step prior; a split trigger adds little when the bot's own
  rules already split at the same lengths.
- *Free steps were the visible gap.* Imitated dragons of length 5 or more took a multi-step move on about half of
  their moves, our search almost never. Gating free steps by the net's P(multi-step), with the queen excluded and a
  safe-landing check, was the one move-prior component with a steady gain: about +5 win-rate points pooled over about
  700 paired games (2.3 sigma). Without the landing safety it killed more of our own dragons.
- *CPU matters.* The plain forward pass cost about 30 M of the 100 M points a turn and shortened the search; writing it
  with the output channel innermost (vectorisable) and skipping zero inputs brought it to about 9 M with the same
  outputs. Build one net per process and dequantise once.

**Imitation**

- *Accuracy saturates.* First-step accuracy stayed at 72-73% whatever we tried: a wider net (96 / 256 channels), more
  data, a non-local memory of everything the dragon had seen (+0.3 points). A 7 x 7 view carries too little to predict
  more. Gains in play came from how the prior is used, not from accuracy.
- *Rare actions are where the signal is.* The split probability ranks split turns very well (AUC about 0.97), and
  multi-step and no-action turns are predictable too (AUC 0.94-0.97). Sample by class, store the keep rate, weight by
  its inverse.
- *Map habits are a trap, map coverage is not.* A net can recognise a map from local walls and beds and replay
  per-map habits; that helped on some maps and hurt on others, and lost value when maps changed. Nets specialised to
  map families were better on their own family and worse elsewhere; one net trained on all map families pooled was
  better than the previous net on every held-out map. Use a generic net for unseen maps.
- *Do not give the policy inputs the demonstrations never had.* Global facts that the replay knows but a dragon does
  not (privileged inputs) did not help the policy. Decoded messages of our own team are zero in other teams' replays,
  so a policy trained on those replays never saw them non-zero; in play they shifted it (fewer splits). Such inputs
  belong in value heads trained on your own games.

**Learning from outcomes**

- *Short horizons punish growth.* Action values over 5 turns ("eats soon") made a re-ranked policy stop splitting: a
  split spends its turn standing still. Values over the dragon's lineage (its own pearls plus those of the dragons
  split off it) fixed that. Rule: a short-horizon value may reorder moves, never the growth decisions (split,
  multi-step, no action).
- *Expert iteration needs a better expert.* Six rounds of self-play targets for the policy were all negative: the
  "expert" (the net plus a value re-ranking behind a safety layer) was not stronger than the net, so distilling it
  distilled its biases, and its own states diluted the demonstrations. Filtering targets by agreement also
  under-represented the classes the safety layer vetoes most. Self-play data were useful for the value heads only.
- *A network-led bot lost to the search bot.* A per-dragon policy net behind a safety layer stayed far below the
  search bot through every variant: per-dragon imitation has no team plan (who grows, who feeds, who escorts). The
  nets earn their keep as priors inside the search.

**Evaluation**

- *Training noise is large.* Replicas of one recipe with different seeds scored from 39% to 54% against the same
  reference net over 124 paired games, and resuming an annealed net for one more epoch cost 10-20 games whatever the
  data. Judge a new recipe on two or more seeds. Model soups and logit ensembles gave no steady gain.
- *Screens shrink.* The best of eight 40-game screens regressed toward 50% at 160 games. Confirm before believing.
- *Use paired seeds and a same-seed baseline*, and take the winner from the engine's own result: a winner recomputed
  from final lengths ignored the queen rule and disagreed with the engine in about 15% of games.
- *Check what the records contain.* Replays without pearl-bed info silently empty two feature channels; a full disk
  once truncated record shards (the builders now check record sizes).

## Glossary

- **queen**: dragons 0 and 1; after round 500 the longer queen wins (rules 1.2.3).
- **king**: in label names, a team's longest dragon.
- **free steps / multi-step move**: a `MOVE` of several steps; the first ceil(L / 4) steps of a length-L dragon cost
  nothing.
- **F1 / F2**: the 16-class net without / with the dragon's own memory inputs.
- **keep rate**: the share of a class's turns a builder kept; training weights by its inverse.
- **arm**: an experiment copy of a bot that differs by switches or weights.
