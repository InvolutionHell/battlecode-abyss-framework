# Game rules: UNSW Battlecode 2026 "Abyss"

[简体中文](game-rules.zh-CN.md)

A condensed, implementer-oriented summary of the rules. It was written from the official documentation at
<https://game.battlecode.au/docs> and from the engine source of the official toolkit `unswbc`
(MIT licence, UNSW CPMSoc, <https://github.com/unswcpmsoc/unswbc>). It matches rules version **1.2.3** and toolkit
versions 1.2.3 to 1.2.9; the engine, sandbox and compiler are byte-identical across that range. Statements marked
**(probe)** were checked with small test bots. When this page and the engine disagree, the engine is right.

Lower-level measurements (sonar details, the virtual clock, build cache) are in [engine-notes.md](engine-notes.md).

## At a glance

| item | value |
| --- | --- |
| players | two teams; each controls dragons (snakes) |
| processes | **one process per dragon**, no shared memory between teammates |
| board | torus, W x H with each side 10-64 tiles |
| game length | 500 rounds (0-499) |
| win | elimination; otherwise after round 500: longer **queen**, then longest dragon, then total length |
| units | at most 64 living dragons per team |
| CPU | 100 M points per dragon per turn (instruction-priced, virtual clock) |
| memory | 48 MiB per dragon |
| language used here | C++20 compiled to WASM (`-O2 -msimd128`) by the judge's clang |

## Board

- The grid wraps on both axes: walking off an edge brings you back on the other side. (0, 0) is the top left and
  y grows to the south.
- Each edge between two tiles is empty, **kelp** or a **portal**. Kelp is a wall: moving into it kills the dragon. A
  portal sends you out on the far side of its partner edge with the heading unchanged; both ends share an id and an
  orientation. An unpaired portal leads nowhere useful. Vision does not reach through a portal; sonar does.
- **Pearl beds.** Some tiles spawn pearls. Each such tile shows a countdown (`pearlIn`; -1 means it never spawns)
  that every dragon with the tile in view can read. At the start of each round, before any dragon moves, every
  countdown drops by 1. At 0 the tile spawns a pearl if it is free (no pearl and no dragon on it), and it then draws
  its next countdown uniformly from a hidden per-tile range [min_gap, max_gap] whether or not it spawned.
- **Mirrored beds share one countdown.** Maps are symmetric (`SYMMETRY` header: vertical mirror, horizontal mirror or
  180-degree rotation) and a bed and its mirror tile use the same countdown, so spawns are symmetric unless one side
  is blocked. Seeing a bed's countdown also tells you its mirror's.
- **Pearl spawns are seeded per match.** The replay records the seed, and `unswbc run --seed 0x...` reproduces a
  match; with `--sandbox` the bots' own random numbers repeat too. There is no fixed pearl timetable to learn.
- Tournament maps are unseen. A competitive bot therefore needs general play, not map databases, opening books or
  per-map tuning.

## Dragons

- A dragon is a body of at least 2 segments with a head and a facing (the direction from its first body segment to
  its head).
- The map defines the starting dragons. Ids are assigned in map-file order starting at 0, and the file must alternate
  teams, so **ids 0 and 1 are the two queens**, one per team.
- All bundled maps list team A's dragon first, but the format does not require it, and ladder games swap sides, so in
  about half of all games team B owns id 0. Detect the queen without the team letter: **a dragon is the queen if and
  only if its id is below 2; a visible segment with an id below 2 belongs to that team's queen.**
- **The queen is fixed for the whole game.** A split leaves the parent's id on the front part, so a queen that splits
  stays queen. A dead queen is never replaced. A split child's id says nothing about its team or about the queen.
- Starting lengths are set per map (2-25 on the 22 maps bundled with the toolkit) and the queen is not always the
  longest starting dragon (for example, on Autarky each queen starts at length 3 while a teammate starts at length 14).
  Each team starts with 1 to 10 dragons on the bundled maps.
- Unit limit: `UNIT_LIMIT` (default 64) living dragons per team; `UNIT_COUNT` in each round block is your team's
  living dragons.
- **Turn order.** Each round: pearl countdowns tick, then every living dragon takes one turn in ascending id order
  (so the queens act first). A dragon killed earlier in the round is skipped. A child created by a split gets the
  next unused id (game-wide) and acts later in the same round.

## Actions (one per turn)

| command | effect |
| --- | --- |
| `MOVE <dirs>`, e.g. `MOVE NNE` | Multi-step move (sprint). A dragon of length L (fixed when the move starts) takes its first **ceil(L/4)** steps for free; every further step removes one extra tail segment, so x steps cost max(0, x - ceil(L/4)) segments. A paid step needs length >= 3 at that moment; at length 2 the dragon dies (`noValidAction`): the move is not truncated and the steps already taken stand. Collisions and pearls are checked at every step; a pearl grows the dragon by 1 (on a paid step it cancels that step's cost). Without pearls the reach is ceil(L/4) + L - 2 steps. |
| `SPLIT n` | The rear n segments become a new dragon, reversed (the child's head is the old tail, facing away from the parent). The parent keeps its front, its id and its turn. Needs n >= 2, the parent keeping >= 2, and the team below the unit limit; otherwise the parent dies (`noValidAction`). The child gets the next unused id and acts later this round as a fresh process (new init block, no memory). |
| nothing, or an invalid action | The default action is **suicide** (death reason `noValidAction`). Always send a valid action unless the death is intended. |

- Free steps by length: L 2-4 -> 1, 5-8 -> 2, 9-12 -> 3, 13-16 -> 4, 17-20 -> 5, ..., 61-64 -> 16.
  Reach without pearls: L 3 -> 2 steps, 4 -> 3, 8 -> 8, 12 -> 13, 16 -> 18, 20 -> 23.
  (probe) L 3 moving 2 steps ends at length 2; 3 steps dies with `noValidAction`; L 7 moving 2 steps over 2 pearls
  ends at length 9.
- **Each step, in order:** (1) a paid step checks that it can pay; (2) the facing turns to the step's direction;
  kelp kills, a portal exits at its partner edge, otherwise the head goes to the adjacent (wrapped) tile; (3) the
  destination is checked against the dragon's own body first (tail included, since the tail has not moved yet; step 2
  of a sprint sees the body as step 1 left it), then against other dragons: if another head is there, that dragon dies
  first and then this one; any other segment kills only this dragon; (4) the head moves, a pearl there is eaten, the
  tail advances unless a pearl was eaten, and a paid step removes one more tail segment. A death discards the
  remaining steps and that turn's sonar.
- **Split chains** (probe): a child may split again in the same round and the grandchild also acts that round, so a
  long enemy body can create a new head in the middle of its body and strike within one round.
- `MOVE` and `SPLIT` overwrite each other; the last one read before `ENDTURN` is applied. A line that does not parse
  is skipped and leaves the earlier action in place.
- **Deliberate suicide is legal.** A dead dragon drops pearls (below), so a short dragon can die next to a teammate on
  purpose to feed it. The released bot uses this in its end-game feeding.

Per turn a bot may also send: sonar (protocol 3: up to four `SONAR <dir> <u64>` rays, one per direction, or the
legacy `SONAR <u32>` along the facing), `INDICATOR`, `LOG`, `DOT`, `LINE` (debug output), then `ENDTURN`. Sending
`PROTOCOL 3` (the official helper does) turns on `ECHOES` and values above 32 bits from the next turn. The init block
(`ID`, `TEAM`, `MAP W H`, `UNIT_LIMIT`) arrives at every process start; each round block carries `ROUND`, `DIR`,
`LENGTH`, `UNIT_COUNT`, `NUM_MSGS` plus the values, `ECHOES`, the 49 tiles in view, `DRAGON_BODIES` and the edges. At
the end of the game the engine closes stdin.

## Death

- Moving into kelp: `HitWall`. Into your own body, tail included (also stepping back into the neck): `HitSelf`. Into
  another dragon's body: `HitOtherBody`.
- Head-to-head (teammate or enemy): **both** die; the dragon that was hit dies, and drops its pearls, first.
- Invalid or missing action, unpayable sprint step, illegal split, crash: `noValidAction`.
- **Timeout is death**, even after a valid action was printed and flushed (probe: `MOVE` plus flush, then a busy loop,
  under `--sandbox` gives "exceeded CPU limit" and death by `noValidAction`).
- A dead dragon of length L drops ceil(L/2) pearls on its body where it stands (head, skip one, next, ...). Tile
  countdowns are unchanged.
- **A dead queen counts as length 0 in the first tie-break for the rest of the game.**

## Sensing and communication

- **Vision:** the 7x7 square around the head (Chebyshev distance <= 3, wrapping, not through portals): tiles (pearl,
  pearl countdown), edges, and every dragon segment inside as `team id x y facing isHead`. Both queens are therefore
  recognisable on sight by their ids.
- **Sonar is the only communication.** A ray carrying a u64 (protocol 3) is cast from the head in a chosen direction
  *after* the move, at most one per direction per turn (cast in the order N, E, S, W). A ray opposite to the facing
  leaves from the tail, in the tail's direction. It passes through portals and wraps, stops at kelp (the value is
  lost) or at the first dragon segment it meets, including the sender's own, and lands in that dragon's inbox, friend
  or foe. It is lost if it meets nothing within W + H tiles or if the sender died this turn.
- Inboxes are read at the recipient's next turn: a higher id hears a ray the same round, a lower id the next round.
  No sender id or team is attached. The next round block tells the sender what each of its rays stopped at
  (`ECHOES`: kelp / ally / ally head / enemy / enemy head counts).
- There is no shared memory: each dragon is a separate process that must rebuild its own picture of the map and of
  the team. Enemies can read rays that hit them and can inject arbitrary values, so treat every received value as
  untrusted. The exact ray mechanics are in [engine-notes.md](engine-notes.md#1-sonar).

## Winning

1. **Elimination:** the game ends in the round in which a team loses its last dragon; that team loses (both teams in
   the same round: draw).
2. **After round 500**, in order: (a) **the longer queen** (id 0 against id 1; a dead queen counts 0, so any living
   queen beats a dead one); (b) the longest living dragon; (c) the greatest total length; (d) draw. `unswbc run`
   prints which criterion decided, e.g. "(longer queen, 12 to 5)".

Before rules 1.2.3 the round-500 order was only (longest dragon, total length). Tools that compute the winner
themselves instead of reading the engine's result must use the current order.

## Ladder and tournaments (short)

- A **ranked** ladder battle is 5 games on random maps from the ranked pool and updates ratings; an **unranked**
  battle is one game per chosen map and changes nothing. A team keeps its colour for the whole battle.
- Ratings are a Kalman-filtered Elo, kept per submission and per map (an overall rating plus a per-map offset, each a
  mean with an uncertainty). The expected score includes a +20 rating bonus for the side that moves first (owns id 0),
  which is one reason a fixed set of seeds and sides is not a neutral test bed (see
  [../methodology.md](../methodology.md)).
- Submissions are zip files of at most 4 MB with `bot.toml` at the top level.
- The 2026 season had three knockout tournaments (Sprint, Qualifier, Grand Final), all played on unseen maps.

## Judge limits

| limit | value |
| --- | --- |
| CPU per dragon per turn | 100 M points (priced per WASM instruction, below) |
| memory per dragon | 48 MiB |
| threads per dragon | 1 (`std::thread` throws) |
| backstop | 1 s of real CPU / 10 s wall per turn (only for a bot that evades the meter) |

- Bots run in a WASM sandbox. `unswbc run --sandbox` reproduces it locally (`-v` prints per-turn output). A plain
  `unswbc run` has no CPU limit, only 10 s of wall time per turn, so CPU checks must use `--sandbox`.
- A turn that runs out of points delivers nothing and the dragon dies; the first turn is charged too, including
  process setup. The meter runs from reading the round block to the flush in `end_turn()`; work done after
  `end_turn()` is charged to the next turn.
- **Prices:** 1 point for constants, locals and globals, compares, integer and float arithmetic (sqrt and min/max
  included) and conversions; 2 for loads and stores, branches and every SIMD op (4-16 lanes for the price of two
  scalar ops); 3 for division and remainder and `br_table`; 4 for `call`; 6 for indirect calls; 10 + 1 per 8 bytes for
  `memory.copy` / `memory.fill`; 50 for `memory.grow`. Host calls: a stdout or stderr write costs 2.5 M + 4 000 per
  byte, stdin 6 per byte, random bytes 40 000 + 3 per byte, a file open 40 000.
- **The sandbox clock is virtual: 1 point = 1 ns.** A 100 M point turn is 0.1 s on `steady_clock`, not 1 s (the 1 s is
  the real-CPU backstop). Budget searches against this clock.
- Output is expensive: make one write per turn. The C/C++ helpers set a full output buffer in `init` and flush in
  `end_turn`; do not set `std::unitbuf`. The judge keeps at most 500 000 log, indicator and drawing lines and 16 MB per
  team per game; indicators are cut to 512 characters.
- C/C++: clang 20, `-std=c++20` / `-std=c17`, `-O2 -msimd128`, libc++ and wasi-libc, no external libraries (vendor
  them as source); `__DATE__`, `__TIME__` and `__TIMESTAMP__` fail the build. Python bots get CPython 3.13 with a
  reduced standard library and NumPy.
- **Determinism:** the match seed fixes pearl respawns and every dragon's random draws. A dragon gets its own random
  numbers only from the sandbox (WASI `random_get`, seeded per dragon). `rand()` without `srand`, a
  default-constructed `std::mt19937` or `srand(time(NULL))` give every dragon the same sequence.

## Our CPU line: 95 M

The judge's hard limit is 100 M points per turn. The released bot treats **95 M** as its own hard line:

- the search runs against a deadline of 75 M points on the virtual clock (typical turns measure about 78-80 M in
  total, the worst observed turns on large maps at the full unit count between about 82 and 86 M);
- every check point that can see the virtual clock pass 95 M (the policy before its search, the sonar stage, the
  final flush) falls back at once: before the search the policy takes the cheapest legal step, later check points
  keep the action already decided, and the sonar rays are dropped, leaving at least 3 M points for writing the
  action;
- the fallback is covered by unit tests and by a "burn" test build that forces the clock past the line.

Details and measurements: [engine-notes.md](engine-notes.md#3-the-95-m-line-and-the-forced-fallback).

## Changelog of the rules

- **Rules 1.2.3 (queen update):** dragons 0 and 1 are queens; after round 500 the longer queen wins (dead = 0), then
  the longest dragon, then total length, then a draw. A sprint's first ceil(L/4) steps are free (previously 1). Ladder
  ratings moved to the per-submission, per-map Kalman-Elo.
- **Toolkit 1.2.3 to 1.2.9:** no rule change; only the version string, the replay viewer and the bundled template maps
  differ. The same bot on the same seeds (three seeds checked) produced identical event streams on both versions.
- **Earlier:** pearl respawns became seeded per match (they had been a fixed schedule per map).
