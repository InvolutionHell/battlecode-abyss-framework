# History: how the bot evolved

A one-page account of the main design lines, from the first greedy forager to the queen-centred bot in
[`bots/abyss`](../bots/abyss). Stages are the major design lines we worked in; the final bot's architecture is
described from its code in [policy/README.md](policy/README.md), and how each step was judged is in
[methodology.md](methodology.md).

## The main line

| stage | core idea | what it taught us |
| --- | --- | --- |
| **Greedy forager** | One process per dragon scores its legal moves with a one-step evaluation (discounted pearl value by BFS distance on the torus, a safety veto, a crowding term) and always emits a legal action. Teammates coordinate by running the same rules on the same inputs instead of messaging. | A safe, legal, never-idle bot beats the template easily, but one-step greed cannot plan around bodies, beds or other dragons. |
| **Incremental rules** | Map memory and symmetry, a split policy, sprint trades, fountain farming with a reverse split to leave a dead end, and emergent end-game feeding (a short dragon dies next to a longer teammate to feed it). | Every rule helped on some maps and hurt on others. The early versions also recognised the bundled maps and replayed a deterministic pearl schedule; once pearl spawns were seeded per match and tournament maps were announced as unseen, that knowledge was worthless, and we adopted the generality rule: no map databases, pearl timetables, opening books or per-map-name tuning. |
| **Search framework** | A world model learnt from vision only (kelp, portals, beds and their refill gaps from visible countdowns, mirror symmetry), an anytime lookahead over each dragon's own move sequences inside a fixed point budget, sonar facts shared between teammates, every numeric knob in one parameter struct, and compile-time module switches for clean ablations. The knobs were tuned by CMA-ES in an island model against a league of earlier champions. | Search plus tuning lifted the bot a long way, and compile-time switches made "this module is off" mean exactly the previous build. |
| **Module merge** | Strategy modules from parallel branches in one bot: swarm splitting in the opening, splits while eating, an early king (the longest dragon keeps its length and the others leave its food), finishing a beaten army, feeding windows. | In self-play the side ahead in units at round 100 won 77% of the games: the opening economy decides most games. |
| **Team plan and fight model** | (1) team-level target assignment (teammates do not chase the same pearls; coverage of the map), (2) a local fight model (a small search over nearby dragons that refuses even swaps), (3) an end-game plan (one king fed by its teammates, a final push), plus parameters per map group and game phase (12 contexts of 132 knobs each, 1,584 tuned values). | Tuning still paid (a tuned build beat its untuned start 251-145), but wins against our own earlier versions stopped carrying over. From then on a new line had to differ in policy, not in parameters. |
| **Queen-centred line** | The 1.2.3 rules made dragons 0 and 1 queens whose length decides round 500. Built on the economy core of the previous stage: queen identity by `id < 2`; the Queen Safety Kernel (QSK), which picks the queen's landing by enemy reach, free exits and dead ends, with friend discipline around her; economy modules (dead-end and fountain harvest, a bed spawn clock using visible and mirrored countdowns); MAPNET, a shared map network over authenticated sonar (portal gates, rooms, traps, value facts); hunting the enemy queen; two small imitation-learned priors used as tie-breakers in the search; the 95 M CPU fallback; and, last, an opening expansion that sends non-queen dragons toward unclaimed bed areas on small maps. | Safety first for the queen, economy first for everyone else. Each module entered behind a switch whose default reproduced the previous build byte for byte, and was judged on its own mechanism KPI before its effect on results. |

The final submitted bot is the last build of the queen-centred line, released in [`bots/abyss`](../bots/abyss) with the
same WASM as submitted (see [results.md](results.md)).

## Lines that did not make it

- **Map recognition and a pearl timetable:** made worthless when pearl spawns became seeded per match.
- **An opening book** (paths planned over the known map): plans broke at the first contact with enemies or
  teammates, and the bot without the book was stronger.
- **Per-map parameter profiles keyed by map name:** removed under the generality rule; the final bot picks its
  parameter set by a map group it infers from what it has seen.
- **Evolutionary tuning of every parameter against a self-evolving league:** it kept improving against our own
  versions while ladder results stayed flat; it survived only as a small-dimensional tuning track for single modules.
- **An imitation prior at a high weight:** it had learnt map-specific habits from the replays (a large gain on one
  map, a collapse on another); only a light weight, a tie-breaker, helped.
- **Ground-up rewrites:** one built around emergent roles and a team-mode controller, another around a persistent
  king that the rest of the team feeds; none beat the previous main line, in lab games or on the replay KPIs. The king
  library of the latter is still compiled into the final bot but switched off.
- **A learnt value function inside the search:** no measurable gain in a first stage.
- **Heavier queen layers** (the queen as the team's king from round 0, zones, escorts, a home preference): each cost
  wins in the lab; the shipped queen logic is mostly safety, plus eating first while she is short in the opening
  (GROWFIRST); her length at round 500 comes from ordinary eating after the swarm stops at round 400.
- **A mirror-bed signal field** (dragons broadcasting the mirrored beds they know, with a decaying pull): never beat
  the base build.
- **Map-class conditioned priors** with an in-game class selector: a single prior trained on all maps predicted better
  on every map, and the selector abstained on unseen maps.
- **A larger search budget** (80 M instead of 75 M points per turn): worse in paired internal benchmarks and closer to
  the CPU line; the final bot keeps 75 M.
