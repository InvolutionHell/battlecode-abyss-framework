# Results

[简体中文](results.zh-CN.md)

Team **中国人必须飞👍🏿👍🏿👍🏿**, UNSW Battlecode 2026 "Abyss".

## Ladder

| | |
| --- | --- |
| peak Elo | **1917** |
| peak rank | **#3** |

The ladder rating is the platform's Kalman-filtered Elo, kept per submission and per map (see
[rules/game-rules.md](rules/game-rules.md#ladder-and-tournaments-short)).

## The final submitted version

The bot in [`bots/abyss`](../bots/abyss) is the final submitted version, released exactly as it was submitted: only
comments, files that are not compiled and one `#error` message inside a disabled `#if` group (see
[bots/abyss/README.md](../bots/abyss/README.md)) were changed for the release. Built by `unswbc` 1.2.9 with
`--sandbox`, its WASM has the SHA-256

```
0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e
```

[dev-guide.md](dev-guide.md#verifying-the-released-bot) shows how to check it. Because the release is byte-exact,
code that is compiled but switched off stays in the source; [policy/README.md](policy/README.md) describes what runs
and what does not.

The two network weight files used by the final bot are published as they are: `rl_weights.hpp` (the opening prior
used by the queen's process before round 200) and `r4_weights.hpp` (the move prior for the non-queen dragons).

Data statement: trained by imitation on aggregated public ladder replays of top-ranked teams; contains no per-team model; the training data is not included.

## Measured effects

Every number below comes from sandbox games on paired seeds and is read as a difference against a same-seed
baseline arm (see [methodology.md](methodology.md)). "Sparring panel" means a fixed set of internal benchmark
opponents (not part of this release).

### Opening expansion on small maps (the last change before the final version)

Until round 120, non-queen dragons head for the known bed area with the most beds that no teammate holds, away from
the enemy's start (for the unseen centre of the map while no such area is known). An earlier version without a gate
lost games on several open maps, mostly larger ones, so the final version runs the expansion only on maps of at most
700 tiles.

950 paired games on the five official maps of at most 700 tiles that we used for opening work, against the sparring
panel:

| map | games | wins: expansion / baseline | eliminated: expansion / baseline |
| --- | ---: | ---: | ---: |
| Devil | 190 | 135 / 90 | 53 / 91 |
| Prisoners Dilemma | 190 | 134 / 139 | 55 / 50 |
| Portals | 190 | 115 / 108 | 1 / 0 |
| Tower Defense | 190 | 149 / 62 | 32 / 102 |
| Trophy | 190 | 133 / 84 | 55 / 104 |
| **all** | **950** | **666 / 483** | **196 / 347** |

- Mechanism KPIs over the same games: the unit difference to the opponent at round 100 was +4.2 with the expansion
  and -1.7 without it; pearls eaten by round 100 rose by 12.7 +/- 1.7 per game; our queen was alive at the end in
  416 games instead of 318.
- Confirmation on fresh seeds: +35 wins over 646 games.

### The weight of the non-queen move prior

The imitation prior adds `w * log p(first step)` to the search's root values. Lowering `w` from 0.05 to 0.02 gave
+7 wins over 304 paired mirror games (+5 on the first seed set, +2 on fresh seeds), +12 over 152 against the
sparring panel and -1 over 76 against an aggressive sparring set. At 0.02 the prior changes the chosen first step on
about 7% of the turns where it applies and gives up 0.006 of search value on average: it breaks near-ties rather
than overruling the search.

### The Queen Safety Kernel

When the kernel was first added (to a build that already identified the queen by `id < 2`), 188 games against our
previous release gave about the same number of wins (90 against 92 without the kernel), while our queen was alive at
round 500 in 14% of the games instead of about 2%. Later queen-safety modules were built on this kernel.

### CPU safety

- A test build that burns the virtual clock past the 95 M line at one check point per round forced 15,018 turns:
  every forced turn emitted its fallback, none timed out, and the worst forced turn cost 98.1 M points (the judge's
  limit is 100 M).
- A CPU census of an earlier build of the final line (sandbox, both sides, the largest official maps, a synthetic
  64x64 map at the 64-unit limit and a newborn child with a flooded inbox) measured 409,503 dragon-turns with a worst
  turn of 83.8 M points, no turn over 95 M, no timeout and at most 5.4 MiB of memory per dragon.
- A spot check of the released build itself (toolkit 1.2.9, sandbox): a full 500-round mirror game on Australia
  (64x64) took 46,445 dragon-turns with a median of 78.7 M points, a worst turn of 81.9 M, no timeout and at most
  6.4 MiB per dragon; a mirror game on Arena peaked at 79.1 M.

### Parameter tuning

On an earlier line of the bot, a build tuned by CMA-ES beat its untuned starting point 251-145 (63.4%) over the
official maps and pearl-schedule variants of them. The same line also showed the limit of tuning: gains measured
against our own earlier versions stopped carrying over to the ladder (see [history.md](history.md)).
