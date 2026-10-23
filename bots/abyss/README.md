# abyss: the final bot

[简体中文](README.zh-CN.md)

This folder is the bot we submitted as our final version for UNSW Battlecode 2026 "Abyss". It is released exactly as
submitted. Only comments changed. The non-compiled history files were dropped, and the header line of the two weight
files was rewritten. Every code token, macro, `#if` block and string that reaches the compiler is the same as in the
submitted build. The one other edit is the message of an `#error` inside a disabled `#if V10K_KING_VETO` group (it
named an internal design note); the preprocessor skips that group, so the compiler never sees it. The WASM that
`unswbc` 1.2.9 builds from this folder with `--sandbox` is bit-for-bit the submitted one (see
[Determinism](#determinism-and-the-exact-build)).

For a design walkthrough written from this code, see [docs/policy](../../docs/policy/README.md).

## Build and run

The bot is a standard `unswbc` C++ project (`bot.toml` with `language = "c++"`, entry point `main.cpp`, header-only
modules, no `helper.hpp`, no external libraries). From a `unswbc` workspace (a folder with `maps/`; see
[bots/README.md](../README.md)):

```sh
cp -R path/to/repo/bots/abyss ./abyss   # skip this line when the workspace is the repository's own bots/ folder
unswbc run maps/arena.map abyss starter                       # native build, writes replays/*.replay
unswbc run maps/arena.map abyss starter --sandbox --no-replay # the judge's sandbox: WASM, CPU metered in points
```

`unswbc run --sandbox` caches the compiled WASM in `${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots/<bot>-<hash>.wasm`.
The key covers only the `.c/.cc/.cpp` files, so a change to a `.hpp` file is not picked up. Delete the cached file
before running again:

```sh
find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss-*.wasm' -delete
```

To debug outside the toolkit, build it natively (one translation unit):

```sh
c++ -std=c++20 -O2 -o abyss_native main.cpp
```

Compile-time switches can be overridden with `-D` for experiments (for example `-DBC_QGATE=0`). This works for every
switch whose default is wrapped in `#ifndef`. Switches that `ablation.hpp` sets unconditionally, or redefines in its
final overrides, have to be edited in the file. Any change gives a different binary from the released one.

## Architecture in brief

Every dragon is its own process with no shared memory. A child created by a split starts with an empty world model.
It rebuilds its knowledge from its own 7x7 view and from its teammates' sonar. Each turn, in `main.cpp`:

1. **Observe** (`world.hpp`, `board.hpp`): update the world model from vision only. It covers walls and portals,
   pearl beds and their refill gaps, map symmetry (mirrored beds and kelp), our own body, other dragons, enemy
   sightings and the two queens (unit ids 0 and 1).
2. **Hear** (`comms.hpp`, `region.hpp`): decode the tagged 64-bit sonar facts and update the relay table. Facts
   include positions, the longest dragon, enemies, heat, and the map network's portal gates, closed rooms and trap
   set, which also feed its danger layer. (The protocol defines a DROPS fact for death drops; this build never sends
   it, because the scavenger planner is off.)
3. **Parameters** (`tuned_params.hpp`, `common.hpp`): pick the tuned `Params` for the map group the dragon believes
   it is on (judged online, never by map name) and the game phase.
4. **Decide** (`policy.hpp`):
   - Plans: tactics, local team (`team.hpp`), targets, the opening expansion, the reserve, harvest, portal probes,
     squads, feeding and regicide.
   - Then an anytime lookahead over the dragon's own move sequences, deepened until the 75M-point budget is used.
   - Two small imitation-learned networks add priors to the root moves and to the split trigger: `rl_net.hpp` for
     the queen process before round 200, and `r4net.hpp` for every other dragon.
5. **Queen safety** (`qsk.hpp`): our queen's landing is picked by the Queen Safety Kernel's hard tiers. The search
   values only order moves inside a tier. Teammates follow friend discipline around her (room guard, breathing
   room, last exit).
6. **Guards and output** (`main.cpp`, `io.hpp`): last legality checks (the queen multi-step check, the unit slot, a
   legal split, a payable sprint). The action and up to four sonar rays go out in one write, quiet before round 300
   except for event facts. The 95M fallback runs last.

The king end-game library (`v10k.hpp`, `king/`) and the local fight search (`fight.hpp`) are compiled in but never
change a decision in this build (see [Off or inactive](#off-or-inactive)).

## File map

| file | contents |
|---|---|
| `bot.toml` | `unswbc` project file (C++; every source in the folder is included) |
| `main.cpp` | entry point: the per-turn loop, parameter selection, last legality guards, output and the 95M checks |
| `common.hpp` | compile-time switches with their defaults, `Params` (every tunable number), the turn budget and `fb95` |
| `ablation.hpp` | the final build's switch settings and the overrides that define the submitted configuration |
| `tuned_params.hpp` | generated: tuned `Params` per (map group, game phase), applied by `applyTuned` |
| `io.hpp` | stdin line reader, turn parser and the single buffered write per turn |
| `board.hpp` | torus geometry, tile edges (kelp, portals), dead-end detection |
| `nav.hpp` | time-aware BFS and flood fill on the torus (own body frees from the tail) |
| `world.hpp` | the world model of one dragon process: map knowledge, beds, symmetry, sightings, sonar facts, queens |
| `comms.hpp` | sonar protocol: fact types, bit layouts, the tag that authenticates our frames, relay planning |
| `region.hpp` | map network state (`BC_MAPNET`): portal gate states (probe / cleared / trap / dead) and danger; its incremental room graph (`BC_MAPNET_ROOMS`) is off |
| `team.hpp` | local teams: membership, leaders and target assignment among dragons that see each other |
| `policy.hpp` | the decision policy: roles, plans, the anytime lookahead, scoring, splits, probes, feeding, priors |
| `qsk.hpp` | the Queen Safety Kernel: reach model, landing tiers T1-T5, margins (its growth arm, `BC_QUEEN_GROW`, is off) |
| `fight.hpp` | local fight search (compiled, weights 0 in the final parameters, so inactive) |
| `rl_features.hpp` | observation features shared by the training featurizer and by play |
| `rl_net.hpp` | opening prior network (policy / value), evaluated in-bot with int16 weights |
| `rl_weights.hpp` | generated weights of the opening prior network |
| `r4net.hpp` | move-prior network of the non-queen dragons (move, multi-step and split heads) |
| `r4_weights.hpp` | generated weights of the move-prior network |
| `v10k.hpp` | adapter between the search core and the king end-game library (inactive: `V10K_R0 = 501`) |
| `king/core.hpp` | king library entry header and per-turn order |
| `king/proto.hpp` | king library part 1: its sonar frames, checksum and blackboard |
| `king/crown.hpp` | king library part 2: who is king (crown claims, heirs) and the mode machine |
| `king/sigma.hpp` | king library part 3: the lead / lag estimate |
| `king/hunger.hpp` | king library part 4: the king's growth target over the end game |
| `king/feed.hpp` | king library part 5: feeding roles and the capped cull |

## Switches of the final build

This is a summary of the effective macro values of the submitted build, read with `c++ -std=c++20 -E -dM main.cpp`
(that dump also lists `BC_BASE_MAX`, `BC_DIM_MAX`, `BC_SCALE_MAX` and `BC_STRING_MAX`: POSIX limits from `<limits.h>`,
not bot switches).
Defaults live in `common.hpp` and `ablation.hpp`. The overrides at the end of `ablation.hpp` set the final values. The
complete list, every macro with its status and `file:line` pointers, is
[docs/policy/switches.md](../../docs/policy/switches.md). A switch set to 1 does not always act: some modules are
switched off by a tuned run-time value, shadowed by another module, or only label a group of tuning knobs. Those are
listed separately below.

### On and acting

| switch | meaning |
|---|---|
| search core (`BC_TUNED`) | anytime lookahead with hand-written value and danger terms; every knob tuned per (map group, phase) |
| the swarm (no switch of its own) | until `swarmUntil` every dragon of 4+ that is not a king splits in half when the split is safe |
| `BC_MIRROR` | map symmetry: mirrored beds, fountains and kelp are known before they are seen; a 1.23x stronger pull toward fountains known only from their mirror image (`wMirrorHot`) |
| `BC_FAR_EXPLORE` | long-range pull to the nearest unexplored frontier tile |
| `BC_HEAT` | presence heat map of ours and theirs on a 5 x 4 sector grid: no feeding and no idle bed where the enemy holds the sector, HEAT facts, the kernel's fog. Its spread-out pull is off (`wHeatSpread` 0) |
| `BC_ENGAGE` | attack / hold / retreat from a head-on trade value model fitted on ladder replays |
| `BC_V9_EKING` | early king: from a tuned round (200) our longest dragon, if at least 8 long, keeps its segments and the others leave its food |
| `BC_V9_FINISH` | from round 300 with 40+ units, each free dragon of length <= 8 heads for the nearest enemy head seen or heard within 6 rounds |
| `BC_TRAPPED_PORTAL`, `BC_EAT_NO_DISPERSE` | a doomed dragon tries an unpaired portal; eating steps are not taxed by dispersal |
| `BC_RL_PRIOR` | opening prior network: queen process only, rounds < 200, root weight `wPrior` 0.05, split at `rlSplitP` 0.4 |
| `BC_QUIET` | quiet opening: no routine sonar before round 300 (event facts still go out) |
| `BC_PORTAL2` | portal tactics: a closed, poor region is left through its unpaired portals (hungry probes). The one-guard-per-pocket rule is not part of this switch; it runs under `Params::pocketOn` (1, not tuned) |
| `BC_QUEEN` | the queen rule: our queen (id 0 or 1) decides round 500; she never trades and never sacrifices herself; no rule sends feeding to her (she is fed only when she happens to be the team's longest dragon); protection centres on her |
| `BC_QUEEN_QSK` | Queen Safety Kernel: her landing by hard tiers T1-T5 |
| `BC_QUEEN_QKILL` | non-queens take the enemy queen when in reach (QKILL) and guard ours from a later striker (QGUARD) |
| `BC_QUEEN_HUNT` | enemy-queen tracking and sharing, emergent hunters (regicide), free-step pursuit |
| `BC_QUEEN_ROOMGUARD` | friend discipline: a teammate's step within Chebyshev 2 of the queen that leaves her fewer than max(L, 2) + 2 reachable tiles costs 30 |
| `BC_QUEEN_EXITFIX` | QSK tier T5 ranks free landings by margins under enemy lengths calibrated on replays |
| `BC_QUEEN_NODEAD` | the queen's landings avoid dead-end tiles while another landing exists |
| `BC_QUEEN_BREATHE` = 2 | the queen keeps 2 free sides; teammates pay for stepping into the 8 tiles around her head |
| `BC_QUEEN_GROWFIRST` | before round 200, while shorter than 5, the queen eats first |
| `BC_QUEEN_KEEPAWAY` | among the landings QSK keeps, the queen takes the one farther by path from known enemy heads |
| `BC_QUEEN_BOXGUARD` | the queen's last free side is never taken by a teammate, not even as its trapped fallback |
| `BC_UNIT_SLOT` | a non-queen never takes the last unit slot (the queen keeps it for her split hold) |
| `BC_ECON_B0` | economy guard: the queen's multi-step moves come only from QSK |
| `BC_ECON_DIP` | dead-end / fountain lanes: a short dragon dips in to eat and leaves by a reverse split |
| `BC_ECON_BED` | spawn clock: wait value, countdown completion (mirror beds share one countdown), unlock, no-block, idle-to-bed |
| `BC_HARVEST` | small fountain rooms behind a portal are harvested: eat inside, split the rear out through the portal |
| `BC_LONGEST_RESERVE` = 3 | from round 300 the team's longest non-queen dragon keeps every segment and eats first (eatBonus x2); the two nearest teammates leave it food |
| `BC_QGATE` | the queen never probes a portal; unsafe or unknown portal landings are walls for her |
| `BC_DEADPORTAL` | for every dragon, a portal whose known landing is a 1-tile room is a wall |
| `BC_MAPNET` (FACTS, REGION, EXPLORE, TRAPSET; `EVIDENCE` = 2, `VALUE` = 1) | shared map network: portal gate and room facts, inferred traps with evidence grading, a danger layer, bed-area VALUE facts |
| `BC_R4NET` | move-prior net for non-queen dragons: weight 0.02, all game (`UNTIL` 500), split trigger at P 0.4 before round 200 (`SPLIT_ON`; redundant with the swarm), net-gated free steps (`SPRINT` 6, `SPRINTP` 0.3, `MAXSTEPS` 2) with the cheap landing rule (`LAND` 1) |
| `BC_OPEN_EXPAND` (W 0.4, `GATE` 28) | opening expansion of non-queen dragons toward free bed areas, until round 120. The gate (bits 4, 8, 16) admits maps with <= 2 starting dragons and <= 700 tiles, dragons born later on maps <= 700 tiles, and any map <= 700 tiles; since bit 16 contains bits 4 and 8, in effect: every map of at most 700 tiles (W x H) |
| `BC_FALLBACK95` | hard 95M-point CPU fallback (see below) |

End-game feeding uses the search core's one-king rule. It is enabled from round 350 (`Params::oneKingFrom`) but acts
only inside the feeding window, rounds 400-489 (`feedRound` 400, `feedStop` 490 in every tuned context): a non-queen
teammate that sees the king (the dragon named by the LONGEST facts), at least as long as itself, walks to it and dies
next to its head. The queen never feeds anyone, and no rule sends feeding to her (`BC_QUEEN_FEED` is 0): she is fed
only when she happens to be the dragon the LONGEST facts name.

### Set to 1 but partly active, inert, shadowed or a label

| switch | status | what runs, and why the rest does not |
|---|---|---|
| `BC_FULL` | partly | the final push is off (`finalFrom` 500); its late reverse protection runs (dragons of 10+ from round 350) |
| `BC_KING` | partly | the longest-enemy record runs; its feeding block is off (`kingOn` 0) |
| `BC_SLAY` | partly | the longest-enemy record and EKING facts run; the slay planner is shadowed by the regicide planner (`BC_QUEEN_HUNT`) |
| `BC_SCAV` | partly | death drops are detected and feed the danger layer; the scavenger planner is off (`wScav` 0), so no DROPS facts are sent |
| `BC_DEADEND` | partly | dead-end tiles are computed (lane dips, NODEAD, the queen's gate); the dead-end veto in the search is off (`deadEndOn` 0) |
| `BC_V9_DENSITY` | inert | density cap on voluntary splits; `kDensity` 1 makes the cap the whole map |
| `BC_FORAGE_PATH` | inert | path distance for heard teammates, but its result is multiplied by `remoteYield` 1.0 |
| `BC_QUEEN_FLEE` | shadowed | a 2-3 step flee; its only call site needs the kernel off and `BC_QUEEN_SAFE` on |
| `BC_V9_TAIL` | off | count on a reverse split before the tail is tracked; `tailOn` 0 |
| `BC_V9_SCOUT` | off | opening scouts per unexplored sector; `scoutUntil` 0 |
| `BC_ROOMCAP` | off | room capacity for closed rooms; `roomOn` 0 in every tuned context |
| `BC_QUEEN_LOCKALL` | off | LOCK mode's no-trade scope; needs `BC_QUEEN_MODES`, which is 0 |
| `BC_COVER`, `BC_SWARM`, `BC_PHASED`, `BC_FINAL` | label | no code reads them; they name groups of knobs in the tuning tools' search space (the swarm itself always runs) |

### Off or inactive

| switch | meaning, and why it does not run |
|---|---|
| `BC_QUEEN_KING`, `_SPLIT`, `_SAFE`, `_ZONE`, `_FEED`, `_GROW` | early queen modules, explicitly 0 in ablation.hpp: superseded by QSK and the one-king rule |
| king end-game library (`v10k.hpp`, `king/`) | compiled, but `V10K_R0` = 501 is past the last round, so only a few constant-time checks per turn run |
| FightSearch (`BC_FIGHT`, `fight.hpp`) | compiled, but `wFight` is 0 in every tuned context, so it never changes a decision |
| `BC_MAPNET_ROOMS` | the rooms of the known map, recomputed incrementally; no decision reads them |
| `BC_EXPAND_OPENING` | an opening "risk mode" for few-unit starts with food behind doors |
| `BC_BED_KEEP` | keep a fast bed and wait for its spawns |
| `BC_QUEEN_GROWUP` (and `BC_GROWUP_FEED`) | the queen grows on boxed maps, with teammates feeding her |
| `BC_V9_KING` | the late-king feeding module (replaced by the one-king rule) |
| `BC_OPEN_RISK` | the opening's small dragons contest food without yielding |
| `BC_R6` | per-dragon map-class nets. Orphan: its headers are not shipped, so it must stay 0 |
| `BC_EXPAND` | idle dragons contest bed cores no teammate holds |
| `BC_QUEEN_FEED350` | from round 350 teammates feed a short queen |
| `BC_ECON_TRACE` | economy trace tags in LOG lines (trace builds only) |
| `BC_T64_FIELD` | a stronger move prior in the field of a fast bed |
| `BC_TIER2` | keep a second-tier bed when the fast beds are full |
| `BC_TRAPPOCKET` | trap pockets (a junction whose only way on is a trap) as walls |
| `BC_QSK_PATHHEAT` | QSK's "enemies about" test by path distance |
| `BC_QUEEN_YIELD` | teammates next to a cramped queen step away first |
| `BC_R7_KINDW_ON` (and `BC_R7_TOPB_ON`) | alternative weightings of the move prior |
| `BC_R4_NOISE` | control setting: a pseudo-random prior in place of the net |
| `BC_SPRINT_TARGET` | free steps toward a contested fresh pearl |
| `BC_R5_GATE` (and the other `BC_R5_*` variants at their neutral defaults) | a stochastic free-step gate |
| `BC_BEDRAID` | opening food probes through unknown portals by starving short dragons |
| other queen variants: `BC_QUEEN_TINY`, `_HOME`, `_ESCORT`, `_MODES`, `_KINAVOID`, `_SLACK` | a 2-long budding queen, a home preference, escorts, team modes, a no-contact rule and a reach-slack filter |
| `BC_V9_ESCAPE`, `BC_V9_KFEED`, `BC_KINGGUARD` | unpaired-portal escapes whatever the unit count, king-only feeding, the longest dragon as a guard |
| `BC_FREESPRINT`, `BC_R4_CULL`, `BC_R4_CALM` | free steps toward the nearest food, sacrifices next to our queen, a prior only with no enemy in view |
| `BC_ECON_CROWN` | defined, but no code reads it |

Code under a 0 switch is compiled out, or reduces to a constant test. It stays in the source on purpose: the turn
clock is instruction-metered, and deleting code, even code that never runs, can change inlining and code layout. That
shifts how deep the anytime search gets within its budget, so play would change.

## Network weights

`rl_weights.hpp` holds the opening prior. It is used by the queen process before round 200. `r4_weights.hpp` holds
the move prior of the non-queen dragons. Both are generated C++ headers: int16 values per row with a float scale,
dequantised once per process.

> Data statement: trained by imitation on aggregated public ladder replays of top-ranked teams; contains no per-team
> model; the training data is not included.

## CPU budget and the 95M fallback

The judge meters each turn in points (about 1 ns of virtual clock per point) and kills a dragon that passes 100M
points in a turn.

- The search budget is 75M points (`BC_TURN_BUDGET` 0.075 s).
- QSK's limit is the search deadline + 10M, clamped below the hard line.
- With `BC_FALLBACK95` every check point that sees the turn's metered clock past 95M falls back at once. It keeps the
  action already decided, or at the policy check point (before any search) takes the cheapest legal step: one step to
  a free neighbour, else straight on, never no action. The sonar rays are dropped, so the single flush (about 2.5M
  points) stays inside 100M.
- The policy check point reuses the clock read of its lean test, and QSK's limit is clamped below the line; the two
  later check points (before the sonar rays, main.cpp:191, and before the flush, main.cpp:253) read the clock again
  only after all clock-dependent work. A new clock read inside the search would shift its budget and change play. A
  fourth check point guards the king library's rays, which never run in this build.
- A fresh child whose setup eats most of its first turn plays a lean one-step turn instead of searching.
- Test builds can force the fallback (`BC_T13_BURN 1`) and log it (`BC_V11_TRACE 1`). Both are 0 in the final build.

A worst-case CPU census and its fixture bots are in `tools/abyss_tools/run/` and [`bots/fixtures/`](../fixtures).

## Determinism and the exact build

The submitted build, compiled by `unswbc` 1.2.9 with `--sandbox` (clang to `wasm32`, `-O2 -msimd128`), has this
sha256:

```
0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e
```

To check it from a workspace with `unswbc` 1.2.9:

```sh
cp -R path/to/repo/bots/abyss ./abyss_check
find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss_check-*' -delete
unswbc run maps/arena.map abyss_check starter --sandbox --no-replay
shasum -a 256 "${XDG_CACHE_HOME:-$HOME/.cache}"/unswbc/wasmbots/abyss_check-*.wasm
```

The bot reads no random source. Where it needs a random choice it hashes the game state (round, id, head). The search
is anytime, so its depth depends on the clock. In the sandbox that clock is the instruction-metered virtual clock, so
a sandboxed game with a fixed `unswbc run --seed` replays identically. As a check, two sandboxed runs against
`starter` on `default.map` with `--seed 7` wrote byte-identical replays. In a native run the clock is real time,
so search depths, and with them the moves, can vary from run to run. Another toolkit version can also give different
games if its clang or its CPU metering differs.
