# Engine notes: measured facts for bot authors

These are facts about the `unswbc` engine and sandbox that the official documentation does not spell out, or that
cost us time to discover. Each was measured with small probe bots, read from the toolkit source, or both. The probes
ran on toolkit 1.2.3; the engine, sandbox and compiler are byte-identical up to 1.2.9, and the build-cache behaviour
in section 4 was re-checked on 1.2.9. The rules themselves are summarised in [game-rules.md](game-rules.md).

## 1. Sonar

| # | fact |
| --- | --- |
| S1 | The payload is a full u64. The line is `SONAR <N\|E\|S\|W> <decimal>`. A malformed line is skipped and the turn's `MOVE` still applies. |
| S2 | At most one ray per direction; a later line for the same direction overrides the earlier one. Rays are cast in the order N, E, S, W. |
| S3 | Rays are cast after the whole action. A sprint casts from its final head position; a `SPLIT` casts from the parent's head and from its new tail. |
| S4 | A sender that dies during its own turn loses all its rays. A sender killed later in the same round (on another dragon's turn) still delivers. |
| S5 | The direction opposite the post-action facing leaves from the **tail**, along (tail - pretail). The other three leave from the head. At length 2 the tail is the neck. This also holds after crossing a portal. |
| S6 | A ray stops at the first dragon segment it meets, the sender's own included. A ray on an empty line wraps around the torus to the sender's own head and arrives in its inbox next round (its echo counts as "ally head"). |
| S7 | The range is exactly W + H cells; portals add none. Kelp stops the ray and the value is lost. |
| S8 | Cast-time snapshot: dragons with a lower id are seen after their move, higher ids before it. The inbox carries values only, ordered by (round, sender id, N-E-S-W). So a dragon's inbox is [last round's rays from senders with id >= mine] followed by [this round's rays from senders with id < mine], with enemy values interleaved by sender id. |
| S9 | A ray from a lower-id sender is read in the same round; every other ray (including the sender's own) in the next round. The queen has the lowest id of her team, so her rays reach every teammate within the same round. A newborn child reads the rays of its birth round. |
| S10 | Values >= 2^32 reach only receivers that had declared `PROTOCOL 3` before their inbox was built. In round 0, starters with a higher id never receive them. Children inherit protocol 3 and do receive such values in their birth round. `ECHOES` still counts a ray whose value was dropped this way. |
| S11 | `ECHOES kelp ally ally_head enemy enemy_head` gives last turn's totals; lost rays are not counted; hits on the sender's own body count as ally / ally head. A split's tail ray hits the child's **tail** (ally). |
| S12 | One ray line costs about 0.12 M points (about 29 bytes at 4 000 points per byte). A stdout write costs 2.5 M on top, so all output goes out in one write per turn. |
| S13 | There is no inbox cap. With at most 64 living dragons per team (at most 128 senders), 4 rays each, a dragon receives at most about 512 values per turn. |
| S14 | Parsing cost per 20-digit inbox value under `--sandbox`: about 2 400 points with a generic tokenizer plus `strtoull`, about 1 000 points with `fgets` plus a hand-written digit loop. A worst-case 512-value inbox therefore costs 0.5-1.2 M points. |
| S15 | `random_get` is seeded **per dragon**, not per team: `sha256("<match seed as 16 hex digits>-<team>" \0 "<dragon id>")` feeds a `xoshiro256**` generator (as in the judge's sandbox). Teammates share no secret or random stream at process start. |
| S16 | The init block is only `ID`, `TEAM`, `MAP W H` and `UNIT_LIMIT`; the map is seen only through the 7x7 window. The `TEAM` letter does not determine who owns id 0. |
| S17 | A deliberate death is a feeding tool: an L3 dragon at (8,5) facing east sending `MOVE EW` dies `HitSelf` and leaves pearls on (9,5) and (7,5); `MOVE EEW` dies `noValidAction` with one pearl on (10,5). The self-collision check (step 3) comes before payment (step 4) for every length. |

Consequences we designed around:

- Enemies read every ray that hits them and can inject arbitrary values, and replays carry each ray's round and
  sender. A team protocol therefore needs authentication and freshness, must never let a single message flip a team
  mode or kill a dragon, and must have a sensible default when every ray is lost. The authentication rests on a
  secret key: the released bot's key is public now (see [policy/mapnet.md](../policy/mapnet.md)), so a bot that reuses
  its protocol must change the key constants.
- The queen's rays are the fastest channel (same-round delivery), which makes her a natural hub.
- Shared state should merge order-independently (maxima, set unions, newest-wins under a total order), because
  dragons hear different subsets of the messages at different times.

## 2. The virtual clock and instruction metering

- The sandbox prices every executed WASM instruction (see the price list in [game-rules.md](game-rules.md#judge-limits))
  and exposes the running total as the clock: **1 point = 1 ns** on `steady_clock` / `clock_gettime`, plus any time
  the bot sleeps. A 100 M point turn is 0.1 s of virtual time.
- The meter for a turn starts when the bot reads the round block and stops at the flush in `end_turn()`. Work after
  the flush is charged to the next turn. The first turn also pays for process start-up, so static initialisation
  (for example de-quantising network weights, which costs several million points) must fit into turn 0's budget.
- **Determinism.** Under `--sandbox`, the same build, map, seed and side produce the same point counts, so an anytime
  search stops at the same place and the whole game replays identically. Native (non-sandbox) runs use real time and
  do not reproduce. Use `--sandbox` for every comparison and every CPU measurement.
- **Every executed instruction changes play.** Because the search's deadline is measured in executed instructions, a
  feature that is "switched off" by a runtime `if` still costs its guard on every call, moves the point at which the
  search stops, and changes games from the first few rounds on. Consequences:
  - feature switches in the released bot are compile-time (`#if` / `#define`), so a disabled feature compiles to
    nothing;
  - a change that should be inert is verified by comparing WASM hashes (byte-identical) or replays on fixed seeds
    (event-identical), not by win rates;
  - fallback checks reuse clock reads the bot already makes during the search; adding a new clock read inside the
    search would itself shift the budget;
  - comparisons between two builds need many games: even a switch that never triggers splits the games through this
    drift, so a small smoke test measures the drift, not the feature (see [../methodology.md](../methodology.md)).
- Output is the most expensive single operation: 2.5 M points per write plus 4 000 per byte. `LOG`, `DOT`, `LINE`
  and `INDICATOR` lines cost the same per byte, so debug output stays behind compile-time flags.
- **Timeout is death**, even if a valid action was already flushed in that turn.
- Memory: the sandbox caps a process at 768 WASM pages (48 MiB). The released bot used at most 6.4 MiB per dragon in a
  full mirror game on a 64x64 map (`-v` prints points and memory per dragon-turn). The WASM stack is small (64 KiB in
  this toolchain), so large per-dragon state lives in static storage, not on the stack.

## 3. The 95 M line and the forced fallback

The judge kills a dragon at 100 M points. The released bot uses 95 M as its own hard line (compile-time switch
`BC_FALLBACK95`, on in the release):

- The search deadline is 75 M points (`BC_TURN_BUDGET`, 0.075 s of virtual time). With parsing, sonar and output a
  typical turn totals about 78-80 M.
- Check points at the policy's "lean" test, at the sonar stage and just before the flush compare the turn's metered
  clock with 95 M. Past the line the policy check point returns the cheapest legal step (one step to a free neighbour,
  else straight on; never no action); the queen kernel picks among the landings it has scored so far; the sonar stage
  sends nothing; the flush drops the rays and keeps the action. At least 3 M points remain for the output (one write
  is about 2.5 M).
- A test build (`BC_T13_BURN`) burns the clock past the line at one check point per round. In one census it forced
  15 018 turns: every forced turn emitted its fallback marker, none timed out, the worst forced turn cost 98.1 M
  points, and no unforced turn fell back. Native unit tests cover the same paths (25 checks; all 10 seeded mutants
  were caught).
- The release CPU census (sandbox, both sides, the largest official maps plus a synthetic 64x64 map at the full unit
  count and a newborn-child scenario with a 512-value inbox) is described in
  [../methodology.md](../methodology.md#cpu-census).

## 4. The WASM build cache: header edits are invisible

`unswbc run --sandbox` compiles each bot with the judge's clang and caches the result. The cache key (toolkit
1.2.9, `clangtool.build`) is a SHA-256 over the compiler and linker flags, the sorted list of source paths and the
contents of the **C and C++ source files only** (`.c`, `.cc`, `.cpp`, `.cxx`, `.c++`). Headers are not part of the
key, and neither is the toolkit version. The cached file is

```
${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots/<bot folder name>-<32 hex digits>.wasm
```

(`%LOCALAPPDATA%\unswbc\...` on Windows). So:

- **An edit that touches only `.hpp` files does not rebuild the bot.** We checked this directly on toolkit 1.2.9:
  after appending `#error` to one header of the released bot, the build step that `unswbc run --sandbox` uses returned
  the old cached WASM (same SHA-256) without compiling anything; after purging the cache entry the same build failed on
  the `#error`, as it should.
- Purge the bot's entry before any sandbox run that follows a header edit:

  ```sh
  find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss-*.wasm' -delete
  ```

  Use `find ... -delete` rather than `rm` with a glob: in zsh an unmatched glob aborts the whole `rm` command, and the
  stale build then runs silently.
- Two folders with the same name and the same `.cpp` contents share one cache entry even if their headers differ (for
  example two checkouts, or an experiment folder copied over another). For batch experiments give every arm a
  uniquely named, frozen copy of its folder, and purge before building.
- The metered (instrumented) module is cached separately under a key derived from the WASM bytes themselves, so it is
  always consistent with the WASM it came from.
- The cached WASM is also the easiest way to identify a build: `sha256sum` (or `shasum -a 256`) of the file above.
  The released bot's WASM must hash to the value given in [../dev-guide.md](../dev-guide.md#verifying-the-released-bot).

## 5. The queen's identity

- Ids 0 and 1 are the queens because map files alternate teams (see [game-rules.md](game-rules.md#dragons)). A process
  knows it is the queen iff its `ID` is below 2. The `TEAM` letter does not tell who owns id 0: on the ladder the
  sides are swapped in about half the games.
- A split child cannot infer the queen from its own id. It learns where its queen is by sight (any visible segment
  with an id below 2 and its own team) or from a teammate's message.
- A build that derived the queen from the team letter picks the wrong dragon in every game where the sides are swapped
  (about half of all ladder games); "id < 2" is the only robust test.

## 6. Randomness and reproducibility

- Each dragon's `random_get` stream depends on the match seed, its team and its id (S15), so dragons do not share
  random numbers. Library generators that are not seeded from `random_get` give every dragon the same sequence.
- With `unswbc run --sandbox --seed 0x...` the pearl schedule and every dragon's random draws repeat, so a game is
  exactly reproducible. Replays store the seed.
