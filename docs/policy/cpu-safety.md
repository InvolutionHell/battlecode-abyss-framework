# CPU safety

The judge prices every WASM instruction and kills a dragon whose turn costs more than 100 M points, even if a valid
action was already written that turn. The first turn of every process also pays for its start-up (map setup and
de-quantising the network weights). A bot that searches "until the time is up" therefore needs a budget for every module
and a guaranteed way out. This page describes both, and how they were checked. Pointers are `file:line` in `bots/abyss`;
the measured facts about the meter itself are in
[../rules/engine-notes.md](../rules/engine-notes.md#2-the-virtual-clock-and-instruction-metering).

## The clock

In the sandbox `std::chrono::steady_clock` is virtual: it advances 1 ns per point spent, so 100 M points are 0.1 s on
`clockNow()`. All budgets are times on that clock. Because the clock is the instruction count, the same build on the
same map, seed and side stops its search at the same place in every run, and any code change, even code that never
triggers, moves that place.

## Budgets per stage of a turn

| stage | budget | how it is bounded |
|---|---|---|
| parsing, `World::observe`, sonar decoding, map-network update | not clock-checked | bounded by design: per-turn work linear in the map (at most 64 x 64 tiles) or in the 7 x 7 view; the portal-gate rebuild (at most about 16 k edge checks, about 0.3 M points) runs only on turns that learn a new edge or portal; VALUE scans every 4 rounds; room floods capped at 33 tiles |
| network prior (RL or R4) | inside the search budget | runs before the search, so the search's deadline accounts for it; skipped on lean turns ([net-priors.md](net-priors.md)) |
| plans and BFS fields | inside the search budget | at most 12 rival fields (the nearest heads), skipped on lean turns |
| anytime lookahead | until `turnStart` + 75 M (`BC_TURN_BUDGET` 0.075) | iterative deepening; a depth cut by the deadline is discarded (loop at policy.hpp:729, discard at policy.hpp:742) |
| lean turn | if 30 M are already gone when `decide()` reaches its lean test (after the queen strikes and roles, before the networks) | one-step evaluation only: no networks, no rival fields, no deepening |
| Queen Safety Kernel | until min(deadline + 10 M, 95 M - 3 M) | stops scoring landings when the limit passes (policy.hpp:987) |
| R4 free-step landing check | none needed | O(number of enemy heads), no threat map and no clock read |
| post-search rules, `main.cpp` guards | small | constant or bounded floods |
| output | about 2.5 M per write + 4 000 per byte | one buffered write per turn (io.hpp:160); at most four SONAR lines; `LOG` output only in debug builds |

`turnStart` is 0 (the process start) on a process's first turn and the clock read after parsing on later turns. A
typical sandbox turn spends its 75 M on the search and totals about 78-80 M.

## The 95 M hard line (`BC_FALLBACK95` 1)

Our own hard line is 95 M points, measured from the start of the metered turn (the `ROUND` line; the process start on
the first turn; io.hpp:98). Every code path that can see the clock pass it falls back at once and leaves at least 3 M
points for the output (common.hpp:1234):

| check point | where | fallback |
|---|---|---|
| policy | the lean test in `decide()`, before the networks and the search, reusing its clock read (policy.hpp:346) | the cheapest legal action: one step to a free neighbour (no wall, no dragon, not our body), eating a pearl seen there; with no free neighbour, straight on (a move that may die, never no action) (policy.hpp:246) |
| Queen Safety Kernel | its limit is clamped to the hard line - 3 M | the kernel stops scoring landings and picks among those scored so far, or hands the decision back to the search's own move |
| rays | a new clock read after the action is decided and before the sonar rays are built (main.cpp:191) | no rays this turn |
| flush | the last clock read, just before the write (main.cpp:253) | the SONAR lines already built are dropped, the action is kept (io.hpp:180) |

The check points reuse clock reads the bot already makes during the search, and the only new reads come after all
clock-dependent work. A new clock read inside the search would itself shift the search's stopping point and change
games. A fourth check point guards the king library's rays, which never run in this build.

## Rules for changing anything that runs every turn

- **Gate at compile time.** A feature switched off by a run-time `if` still costs its test on every call, moves the
  search's stopping point and changes games from the first rounds on. New modules were added behind `#if` (older ones
  test compile-time constants in plain `if` statements, which the optimiser folds away; a few are disabled only by tuned
  values such as a weight of 0, and those tests do run). A change that should be inert is verified by the WASM hash or
  by event-identical replays on fixed seeds, never by win rates.
- **Give every module its own budget, counted from its own start.** A module that runs after the search and compares the
  clock with the search's deadline gets almost nothing; one that ignores the clock can push a turn over the line. For
  example, the first version of the R4 free steps checked each landing against the Queen Safety Kernel's threat map (up
  to about 10 M points); an unguarded test arm reached 87.4 M where its baseline peaked at 81.3 M. It was first given a
  slack guard, then replaced by an O(heads) rule.
- **Keep dead code.** Removing unused code changes the binary and so the games; see [Compiled but inactive
  code](README.md#compiled-but-inactive-code).
- **Re-run the census** after any change (below) and keep the worst turn well below 95 M.

## How it was checked

**CPU census.** Sandbox games (`unswbc run --sandbox -v` prints the points and the memory of every turn) over four
scenarios:

- **big**: the largest official maps, both sides, full games, with our team at or near the 64-unit limit;
- **worst64**: a synthetic 64 x 64 map on which our team reaches the 64-unit limit early;
- **newborn**: a newborn child's first turn (start-up charged) with a flooded inbox of 512 messages;
- **self-test**: a deliberately CPU-hogging bot against itself, to prove that the census catches turns over 95 M and
  timeouts.

Pass: no timeout, no turn above 95 M, memory well below the sandbox's 48 MiB per process. Release builds of the final
line measured about 78-80 M points per turn at the median and a worst turn of about 82-86 M over about 200,000-410,000
dragon-turns per census, and 5.4-6.4 MiB of memory per process (the R4 net's weights account for about 1 MiB of it).

**Forced fallback.** A test build (`BC_T13_BURN` 1) burns the clock just past the line at one check point a round (round
mod 10: 3 the policy, 5 the rays, 9 the flush). In one census it forced 15,018 turns: every forced turn emitted its
fallback marker at the check point that was forced, none timed out, the worst forced turn cost 98.1 M points, and no
turn that was not forced fell back. Native unit tests cover the same paths (25 checks; all 10 seeded mutants were
caught). With the switch on and the line never reached, games were event-identical to the build without the switch.
