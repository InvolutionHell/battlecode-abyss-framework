# abyss_tools

[简体中文](README.zh-CN.md)

Python tooling for UNSW Battlecode 2026 "Abyss": read replays, measure what a bot does, play local matches with paired
seeds, check a release candidate's CPU and robustness, sweep compile-time switches, and generate test maps.

Everything here works offline on local replays and local games. Nothing talks to the contest website.

## Install

Python 3.9+. The core needs `pycapnp` and `numpy`; `scipy` (terrain distances, the lead curve) and `zstandard`
(`.replay.zst` files) are optional. Running games needs the official toolkit `unswbc`
(`uv tool install unswbc==1.2.9`); the decision harness, the flood-shim test and the region check need a C++20
compiler.

From the repository root:

```sh
pip install -e "tools[full]"          # or: uv pip install -e "tools[full]"
abyss-replay-stats bots/replays/*.replay
```

Without installing, run any tool as a module from the `tools/` folder:

```sh
cd tools
uv run --with pycapnp --with numpy python -m abyss_tools.replay.stats ../bots/replays/x.replay
```

## Configuration

Defaults are relative to the repository root; every one can be overridden by a flag or an environment variable.

| variable | meaning | default |
|---|---|---|
| `ABYSS_ROOT` | repository root | the first parent folder that holds `bots/` |
| `ABYSS_BOTS` | the unswbc workspace with the bot folders (`--bots`) | `<root>/bots` |
| `ABYSS_MAPS` | the folder of `.map` files (`--maps-dir`) | `<bots>/maps` |
| `ABYSS_CACHE` | caches (distance matrices, per-game lead curves) | `~/.cache/abyss_tools` |
| `UNSWBC` | the unswbc executable (`--unswbc`) | `unswbc` on `PATH` |
| `ARENA_SEED_BASE` | the arena's seed base (`--seed-base`) | `1000003` |

## Tools

Every tool prints its full usage with `--help`. Console script / module:

### Replays (`abyss_tools.replay`)

| command | what it does |
|---|---|
| `abyss-replay-stats` / `replay.stats` | result, deaths by reason, splits, peak army, CPU, timeouts and engine complaints per replay. The CPU column has a value only when the replay records per-action instruction counts; replays written by a local `unswbc` 1.2.9 run (native or `--sandbox`) do not, so it reads 0.0M there. For local games use the points per turn the sandbox run prints, or `abyss-cpu-census` |
| `abyss-replay-state` / `replay.state` | rebuilds the exact round block the engine sent each dragon every turn, plus its action (behaviour-cloning data) |
| `abyss-replay-diff` / `replay.diff` | event-for-event comparison; proves a compile-time switch that is off leaves play unchanged |
| `abyss-rules` / `replay.rules` | the round-500 queen rule measured per side: how the game ended, queen survival and length, sprints, head-on kills, fresh pearl spawns by nearer side, and two consistency checks |

`replay.io.load` reads plain, gzip and zstd replays. The replay schema (`abyss_tools/schema/replay.capnp`) is vendored
from the unswbc toolkit (MIT, UNSW CPMSoc) together with Cap'n Proto's `c++.capnp` (MIT, Sandstorm); both keep their
licence headers.

### KPIs (`abyss_tools.kpi`)

Each takes one or more folders of replays (for example an arena run's `replays/`) and prints markdown tables per bot,
labelled with the folder ("arm") they came from, so a candidate and its baseline can be read side by side.

| command | what it measures |
|---|---|
| `abyss-opening-kpi` | first eat, length at r40, units at r50 / r100 and the r100 unit difference, eats before r100, wipes |
| `abyss-macro-measure` | one JSON row per replay: unit / length curves, splits, pearls eaten, deaths, bed tiers reached, early foraging yield |
| `abyss-census-open` | paired arm-vs-baseline opening table from `macro_measure` rows (`collect` builds the rows, `report` compares) |
| `abyss-lead-curve` | who leads the economy (total, units, eats, coverage, territory) at r10..r320, a lead index, paired deltas with bootstrap intervals (needs scipy) |
| `abyss-spawn-share` | the share of fresh pearl spawns nearer to each bot |
| `abyss-harvest-yield` | net yield of entries into small closed rooms (pearl traps) |
| `abyss-queen-box` | queen deaths on her own move, boxed in by a teammate |
| `abyss-queen-feed` | the queen's growth and survival from r350 |
| `abyss-queen-kill` | how a queen dies: enemy-turn head-on kills by step / free sprint / paid sprint |
| `abyss-queen-early-deaths` | every queen death before r150 with the nearby enemy heads and her best landing's margin |
| `abyss-value-trace` | counts of the VALUE-fact trace tags (trace builds only) |
| `abyss-region-graph` | rooms of a map at full knowledge; `--check` compares them with the bot's room module (`bots/abyss/region.hpp`) |

### Runners (`abyss_tools.run`)

| command | what it does |
|---|---|
| `abyss-arena A B` | A against B on every map, both sides, in parallel; paired seeds so two candidates against the same opponent meet the same pearls and random numbers |
| `abyss-cpu-census` | worst-case CPU check under the judge's metering: big maps at full army, a 64-unit flooded-inbox map and newborns; reports p50 / p99 / max points, turns over 85M / 95M, timeouts, memory; `--scenarios selftest` validates the tool itself |
| `abyss-safety-line CAND DIR` | unseen-map check over a candidate's replays: 0 timeouts, 0 crashes, clean queen checks |
| `abyss-sweep` | builds arms of a bot that differ in `#define` values (appended to its `ablation.hpp`), plays them next to a same-seed baseline, reports paired differences (the lead-index column needs scipy and shows `-` without it) |
| `abyss-decision-harness` | feeds a native (debug) build of a bot the exact inputs of a recorded game and prints its LOG reasons per turn; `--actions` checks the rebuilt inputs reproduce every recorded move |

### Maps (`abyss_tools.maps`)

| command | what it makes |
|---|---|
| `abyss-mapgen` | random symmetric maps in the style of the official pool; `--check` plays each once |
| `abyss-mapgen-cls` | maps by class: separated halves, portal-gated far maps, rich, small, large, scattered beds; `--check` classifies and engine-parses them |
| `abyss-pearl-variants` | the same maps with redrawn pearl timings |

## Typical workflows

**A/B test a change.** Copy the bot (`bots/abyss` to `bots/cand`), change it, then play both against the same opponent
(the toolkit's template makes a reference opponent: `cd bots && unswbc init cpp starter`):

```sh
abyss-arena cand starter --sandbox -j 8          # -> bots/arena-out/cand_vs_starter
abyss-arena abyss starter --sandbox -j 8
abyss-opening-kpi bots/arena-out/cand_vs_starter/replays bots/arena-out/abyss_vs_starter/replays
abyss-lead-curve bots/arena-out/cand_vs_starter --vs bots/arena-out/abyss_vs_starter
```

Judge a change by its difference to a renamed copy of the unchanged bot played on the same seeds, not by 50%: a fixed set
of paired seeds carries its own bias.

**Release check.**

```sh
abyss-cpu-census --bot cand                      # no turn over 95M, no timeout, memory under 48 MiB
abyss-mapgen --out runs/maps/held --count 25 --seed 900000 --prefix h
abyss-arena cand starter --maps-dir runs/maps/held --sandbox -o runs/held
abyss-safety-line cand runs/held/replays
```

**Why did it move there?**

```sh
abyss-decision-harness game.replay A --build bots/abyss --from 120 --to 140
```

The bot's LOG lines are compiled in only with `BC_DEBUG`; the harness defines it in its private copy. KPIs that read
trace tags (`value-trace`, the sacrifice column of `queen-box`) need replays of a trace build (`BC_DEBUG 1`,
`BC_ECON_TRACE 1`).

## Tests

```sh
cd tools
uv run --with pycapnp --with numpy --with scipy --with pytest --with zstandard python -m pytest -q
ABYSS_SLOW_TESTS=1 uv run --with pycapnp --with numpy --with scipy --with pytest --with zstandard python -m pytest -q
```

The fixtures (`tests/fixtures/*.replay`) are two games on the official Arena map, recorded with unswbc 1.2.9 in the
judge's sandbox: the final bot against the toolkit's starter template (`--seed 1`, 45 rounds), and the bot against a
renamed copy of itself (`--seed 2`, 179 rounds). In the sandbox a seed fixes the whole game, so the commands listed in
[`tests/helpers.py`](tests/helpers.py) write the same bytes again. Tests skip themselves when an optional dependency
(pycapnp, scipy, zstandard, unswbc, a C++ compiler) is missing; the sandbox self-test of the CPU census runs only with
`ABYSS_SLOW_TESTS=1`.
