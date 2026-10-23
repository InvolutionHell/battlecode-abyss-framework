# Methodology: how we evaluated changes

Most of what we learned in this competition is about measurement. A Battlecode bot is a large, noisy, deterministic
system: one extra instruction changes where the search stops, a fixed set of seeds has its own bias, and the best of
eight small screens is usually luck. This page describes the procedure the final bot was built under and the reasons
behind each rule. The tools that implement most of it (match runners, replay parsers, KPI scripts) are in
[`tools/`](../tools/); the two parts that are not included in this release are marked below.

The order of evidence was:

1. **Replays of our own games** define the problem (what loses, and how).
2. A change targets one **mechanism** and is judged first on that mechanism's **KPI**.
3. **Paired, same-seed censuses**, read as differences against a baseline, measure the effect on results.
4. **Safety gates** (equivalence, CPU, unseen maps) must pass regardless of results.
5. The **ladder** has the last word on a released version.

## 1. From a replay review to a change

The unit of a review is a **loss pattern**: for example "eliminated early by a larger army", "behind at round 400
and out-grown", "queen killed head-on", "queen boxed in by her own teammates". Each pattern gets an **impact** (its
share of our losses over a window of at least about 100 games) and **evidence** (replay rounds plus the mechanism
KPIs that show it). Then three questions:

1. **Coverage:** is there a module whose job is this situation?
2. **Execution:** when the situation came up, did that module fire and do what it was designed to do? (Trace builds
   log each module's decisions into the replay.)
3. **Effect:** where it executed as designed, did the game go right?

| answers | change |
| --- | --- |
| no module covers it, and the impact is large (around 10% of losses or more) | **new component**, behind a compile-time switch, designed from replays of games in which the situation is handled well |
| covered, but it does not execute as designed | **fix the implementation** |
| executes as designed, right idea, wrong timing or threshold | **knob or trigger** (small-dimensional tuning) |
| executes as designed and still loses, or the same pattern survives two fix cycles | **direction change** (team plan, mode controller, architecture) |

An ablation (turning one module off) is a diagnosis, not a verdict: a module that does not help may simply be rough.
Before a module is dropped it goes through a strengthening stage: mine the games where it acted and the game went
wrong, fix those cases, re-test on the maps where the module matters, and only then tune its own few knobs (at most
about 30 dimensions, with SPSA or separable CMA-ES on paired seeds).

## 2. Mechanism KPIs over win rate

Every change names the mechanism it targets and the KPI that should move. KPIs we used repeatedly (all computed from
replays):

- **economy:** our units at rounds 50 and 100 and the difference to the opponent's; pearls eaten by round 100; pearls
  per 100 dragon-turns in the opening; the round at which we first hold a fast pearl bed; share of fresh spawns we
  are nearer to;
- **survival:** games in which we were eliminated and the median elimination round; deaths per game by cause
  (`HitSelf`, `HitWall`, `HitOtherBody`, head-to-head); our queen alive at round 500; queen deaths before round 100;
  queen turns spent with no free exit;
- **end game:** queen length at round 500; longest dragon; which tie-break decided the game.

Why KPIs first:

- a KPI moves with far fewer games than a win rate, and its sign is interpretable;
- a change that "wins" without moving the KPI it targets is suspicious: it is usually noise or a side effect;
- win rates against a fixed opponent can be overfit; a version tuned against our own previous versions improved
  against them while it did not improve, or got worse, on the ladder.

Rules we kept: a change must move the KPI it targets; a self-play edge of 55% or less, on its own, counts as no gain;
win rates are always reported but never gate the early development of a new component.

We also built a multi-dimensional score per bot (win, length share, pearl share, exploration, survival, combat; each
a per-game share against the opponent, rated with a Bradley-Terry model; not included in this release). Calibrated
against our own ladder results, the fitted weights put almost everything on the win dimension, because ladder ratings
are made of results only. The other dimensions stayed in every table as diagnostics of what a bot is good or bad at.

## 3. Paired censuses

A **census** is a batch of sandbox games (`unswbc run --sandbox --seed ...`) between a candidate and a set of
opponents.

- **Paired seeds.** The candidate and the reference play the same maps, seeds, sides and repeats, so each game of
  the candidate has a twin. With `--sandbox` a seed fixes the pearl schedule and every dragon's random draws, so the
  twins differ only by the code change.
- **The same-seed baseline arm.** On a fixed set of paired seeds, two identical bots do **not** score 50%: each
  (map, seed, side) is one deterministic outcome, the side that owns id 0 moves first, and a finite seed set carries
  its own bias (in one six-game smoke, a bot against a renamed copy of itself went 1-5). Therefore every census either
  plays each (map, seed) from both sides, or carries a **baseline arm**: a renamed copy of the reference bot playing
  the same opponents on the same maps, seeds and repeats. Every verdict is **candidate minus baseline**, never the
  candidate's absolute score.
- **Many games per verdict.** Because of the instruction-metered clock, even a switch that never triggers changes
  where the search stops and splits the twins after a few rounds (see
  [rules/engine-notes.md](rules/engine-notes.md#2-the-virtual-clock-and-instruction-metering)). A small smoke test
  measures that drift, not the feature. We required at least about 80 paired games for a difference verdict, and used
  several hundred for anything close. For scale: with 44 games the standard error of a win rate is about 7.5
  percentage points, roughly 50 Elo, which is larger than most single components' effects.
- **Per map and per map class.** The same change often flips sign between maps (a portal rule that helps on one maze
  can ruin another), so results are reported per map and per class (small confined maps, open maps, portal mazes,
  bed-rich maps, very large maps), and a candidate must not lose a class it was not aimed at.
- **Opponent sets.** A census used up to three sets: a mirror (the previous release), a fixed panel of sparring
  opponents (not part of this release), and an aggressive set, for queen safety. Absolute win rates against a fixed
  panel are biased, so only differences against the baseline arm are read.
- **Frozen, uniquely named copies.** Each arm is a frozen copy of its bot folder with a unique name. The WASM cache is
  keyed by folder name and `.cpp` contents only, so a shared or edited folder can silently run a stale build (see
  [rules/engine-notes.md](rules/engine-notes.md#4-the-wasm-build-cache-header-edits-are-invisible)).
- **The engine decides the winner.** Read the result line the engine prints. One of our scripts kept an outdated
  tie-break after the queen rule changed and mislabelled games until it was fixed; recomputing the winner yourself is a
  trap.
- **Report uncertainty.** Each KPI difference is reported with the standard error of the paired difference (for
  example "units at round 100: +2.5 +/- 0.5").

## 4. Pre-registered two-stage decisions

When a parameter sweep or several variants compete, the best of many small screens is mostly luck (the winner's
curse). In one early sweep the best of eight 40-game screens won 25-15 and then went 74-86 in its 160-game
confirmation; every screened winner shrank. Our rule from then on:

1. **Write the acceptance rule down before running anything:** which opponent sets, how many games, and the
   thresholds (for example "difference >= 0 over the whole sparring panel, no loss on the mirror set, no loss on the
   aggressive set").
2. **Stage 1 (screen):** every candidate plays the whole panel with a small fixed batch per opponent.
3. **Stage 2 (confirmation):** the candidates that pass are replayed on **fresh seeds** with a larger batch per
   opponent plus the mirror and safety sets. A candidate ships only if the same written rule holds again.

For automated merges in the self-play tuning pipeline we used a truncated sequential probability ratio test instead
of a fixed threshold (the SPRT merge code is not included in this release): Elo bounds -30 / +30, alpha = beta = 0.1,
batches of 44 games (11 maps x 2 sides x 2), at most four batches. After 44 / 88 / 132 / 176 games it accepts with
at least 29 / 51 / 73 / 95 wins and rejects with at most 15 / 37 / 59 / 81; a test still undecided after the fourth
batch merges only if the candidate scored over 50% (89 or more wins of 176). The earlier rule ("over 45% in 44 games")
would have merged a -35 Elo component about half the time; the SPRT merges it about 8% of the time and a +35 Elo
component about 90% of the time.

## 5. Safety gates before a release

These must pass whatever the results say.

### Equivalence

- A new feature is added behind a compile-time switch whose default reproduces the previous build exactly: with the
  switch off, the WASM is byte-identical (same SHA-256) or, where that is not possible, replays on fixed seeds are
  event-identical.
- The released bot itself is frozen the same way: only comments, non-compiled files and text inside disabled
  preprocessor groups may change, and the WASM built by `unswbc` 1.2.9 with `--sandbox` must keep its hash (see
  [dev-guide.md](dev-guide.md#verifying-the-released-bot)).

### CPU census

All in the sandbox with per-turn points and memory recorded:

- **large maps:** the largest official maps, both sides, full games;
- **worst64:** a synthetic 64x64 map where our team reaches the 64-unit limit;
- **newborn:** a newborn child's first turn (process start-up is charged) with a flooded 512-value inbox;
- **self-test:** a deliberately CPU-hogging bot against itself, to prove the census catches turns over 95 M and a
  timeout (the sonar flooder drives the worst64 and newborn scenarios).

Pass: no timeout, no turn above 95 M points, the forced fallback present and tested (unit tests plus a build that
burns the clock past the line), memory well under the 48 MiB cap. Builds of the final line measured worst turns
between about 82 and 86 M points and 5.4 to 6.4 MiB per dragon.

### Unseen-map safety line

Tournament maps are unseen, so before a release the candidate plays a set of generated and held-out maps it was
never developed on (50 maps, both sides, sandbox). Pass: no timeout, no crash, no engine error about the candidate's
dragons, and clean queen checks (the replay parser's final standings and the round-500 rule, recomputed from the
replay, agree with the engine's result). Every `noValidAction` death of the candidate is counted and read in the
replay: a deliberate feeding death is legal, a missing or malformed action is a failure. The difference against the
previous release on these maps is reported as a robustness check, not as a gate.

### Ladder trial

After the local gates, the ladder decides: we followed a new release's ranked results and replaced it if it performed
clearly below its predecessor.

## 6. The generality rule

From the moment pearl spawns were seeded per match and tournament maps were announced as unseen, the bot was not
allowed any map database, pearl timetable, opening book or per-map-name tuning. Everything the bot knows about a map
it learns in play (beds from visible countdowns, mirror symmetry, kelp, portals) and shares by sonar. Training and
validation use generated maps as well as the official ones, with a held-out set for validation only, and a map
"class" is defined by observable features, never by a name.
