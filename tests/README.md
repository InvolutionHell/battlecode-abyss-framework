# tests

[简体中文](README.zh-CN.md)

Native C++ unit tests for the released bot in [`../bots/abyss`](../bots/abyss). Each test includes the bot's own headers,
so it is compiled from exactly the code that `unswbc` builds into the WASM. The tests build each module on a small
synthetic board (usually 20 x 20) and check what it decides. No toolkit, sandbox or game is needed: a C++20 compiler is
enough.

The Python tools have their own tests in [`../tools/tests`](../tools/tests) (pytest; see
[tools/README.md](../tools/README.md#tests)). The RL pipeline's tests are in [`../rl/tests`](../rl/tests).

## Running

```sh
tests/native/run.sh                  # all builds in tests/native/manifest.txt
tests/native/run.sh mapnet queen     # only the lines that contain one of these words
tests/native/run.sh -v               # also print each test's full output
CXX=g++ tests/native/run.sh          # another compiler
```

The tests reach private members with `#define private public`, which libstdc++'s own headers do not accept. When the
compiler uses libstdc++ (g++, the default `c++` on most Linux systems), `run.sh` therefore adds
`-include bits/stdc++.h`, so every standard header is read before the macro. With libc++ (Apple clang) nothing is
added.

`run.sh` compiles every manifest line as `$CXX -std=c++20 -O2 $CXXFLAGS <flags> -I bots/abyss tests/native/<file>`, then
runs the binary in a temporary folder. A line passes if the binary exits with 0 and ran at least one check. The script
exits with 1 if any line failed. On a laptop the full run takes about two minutes, since every test compiles the whole
bot.

| variable | meaning | default |
| --- | --- | --- |
| `CXX` | the C++20 compiler | `c++` |
| `CXXFLAGS` | extra flags for every build | none (plus `-include bits/stdc++.h` with libstdc++) |
| `BOT_DIR` | the bot under test (for example a modified copy) | `bots/abyss` |
| `BUILD_DIR` | where binaries and logs go (`-k` keeps the default temporary folder) | a temporary folder |
| `ABYSS_MAPS` | a folder with the official `.map` files, for `region_test.cpp`'s real-map case | unset: that case is skipped |
| `TEST_TIMEOUT` | seconds per test binary | `300` |

The official maps ship with the `unswbc` toolkit (`unswbc maps DIR` copies them into `DIR`). Without them,
`region_test.cpp` runs only its synthetic boards (36 checks instead of 85).

One test by hand, from the repository root:

```sh
c++ -std=c++20 -O2 -DBC_FALLBACK95=1 -DBC_V11_TRACE=1 -I bots/abyss tests/native/fallback95_test.cpp -o fallback95_test
./fallback95_test        # 25 / 25 checks passed
```

With g++ / libstdc++ add `-include bits/stdc++.h` to that command.

## What is tested

The bot's behaviour modules are guarded by compile-time switches (`BC_*`, see
[docs/policy/switches.md](../docs/policy/switches.md)). The manifest has two groups.

- **Modules of the final build**, tested at their submitted settings. These lines have no `-D` flag, or only a
  test-only trace flag.
- **Inactive modules**, which are compiled out of the final build and switched on with `-D` for the test. Their code is
  still shipped. Removing it could change the instruction-metered search and so the submitted binary. These tests
  document what the switched-off code does, and they keep it buildable.

Some switches are set unconditionally in `ablation.hpp`, or redefined in its final overrides, for example
`BC_QUEEN_BREATHE 2`, `BC_LONGEST_RESERVE 3`, `BC_MAPNET_EVIDENCE 2`, `BC_OPEN_EXPAND 1` and `BC_OPEN_EXPAND_GATE 28`. A
`-D` flag cannot change these: the value in `ablation.hpp` wins (the compiler warns about the redefinition, except where
`ablation.hpp` `#undef`s the macro first). Their tests therefore run at the final values, and branches written for other
modes compile only in a bot copy whose `ablation.hpp` was edited.

| test | module (switch) | in the final build | what it checks |
| --- | --- | --- | --- |
| `comms_test.cpp` | sonar protocol (`comms.hpp`) | on | facts survive encode/decode; accepted in the send round and the next only; the other team's key and a flipped bit are rejected |
| `fallback95_test.cpp` | 95 M CPU fallback (`BC_FALLBACK95`) | on | the line and its clock start, the fallback move (never no action), Policy's check point, dropping sonar rays before the flush, the trace marker (`BC_V11_TRACE`, a test-only flag) |
| `portal_gate_test.cpp` | portal gates (`BC_QGATE`, `BC_DEADPORTAL`) | on | which portals the queen and the other dragons treat as walls (unseen, 1-tile, corridor, dead-end and small closed landings) |
| `mapnet_test.cpp` | map network (`BC_MAPNET`) | on | GATE / REGION / TRAPSET codec, the gate state machine and its timeouts, mirroring, portal pairing from a CLEARED landing, DEAD / TRAP gates as walls, the queen's bed-room allowance, the relay |
| `mapnet_evidence_test.cpp` | evidence grading (`BC_MAPNET_EVIDENCE` 2) | on | an inferred TRAP needs evidence: prober silence over 12 rounds, the 60-round TRAP age, the prober id, the bed-room relay |
| `mapnet_value_test.cpp` | VALUE facts (`BC_MAPNET_VALUE`) | on | the VALUE fact, one entry per heat sector, the scan, the relay, `planValue`'s target |
| `queen_breathe_test.cpp` | queen breathing room (`BC_QUEEN_BREATHE` 2) | on | QSK drops tight landings, its escape after 2 boxed turns, the teammates' veto bubble round her head |
| `queen_boxguard_test.cpp` | box guard (`BC_QUEEN_BOXGUARD` 1) | on | nobody takes the queen's last free side, not even as a trapped fallback (with `BC_ECON_TRACE`: the sacrifice tag) |
| `queen_growfirst_test.cpp` | queen grows first (`BC_QUEEN_GROWFIRST`) | on | before r200 a short queen eats first: who and when, her eat weight, QSK's food preferences |
| `queen_keepaway_test.cpp` | keep-away (`BC_QUEEN_KEEPAWAY`) | on | among QSK's kept landings, the one farther by path from the nearest known enemy head |
| `longest_reserve_test.cpp` | longest reserve (`BC_LONGEST_RESERVE` 3) | on | which dragon is the reserve (rounds, queen, fact freshness, ties); the reserve never splits |
| `reserve_feed_test.cpp` | reserve feeders (`BC_LONGEST_RESERVE` 3) | on | the two nearest teammates leave the reserve its food; no early feeding phase |
| `harvest_test.cpp` | room harvest (`BC_HARVEST`) | on | which closed rooms qualify, who goes in, the step in, eating inside and the splits of the rear (also built with `-DBC_EXPAND_OPENING=1`: the harvest rules under the few-unit opening mode) |
| `unit_slot_test.cpp` | unit slot, forage path (`BC_UNIT_SLOT`, `BC_FORAGE_PATH`) | on | the last unit slot is the queen's; a teammate's remote-yield distance is a path over the known map |
| `open_expand_test.cpp` | opening expansion (`BC_OPEN_EXPAND`, gate 28) | on | the target bed area, home and held beds, countdowns, the escort rule, the unseen centre, the map gate |
| `bedraid_test.cpp` | opening food probe (`BC_BEDRAID`) | off | without the flag: no probe (the final behaviour); with it: a hungry small dragon probes an unpaired portal |
| `fast_bed_field_test.cpp` | fast-bed field (`BC_T64_FIELD`) | off | without the flag: the switch is 0; with it: the field test along the walkable graph, gap and tolerance rules |
| `region_test.cpp` | rooms of the known map (`BC_MAPNET_ROOMS`) | off | rooms on synthetic boards, split and merge, the touched-tile budget and sweep; with `ABYSS_MAPS`, incremental vs full rebuild on 8 official maps. `--dump MAP` prints a map's rooms |
| `expand_test.cpp` | idle expansion (`BC_EXPAND`) | off | an idle dragon contests a free bed core |
| `expand_opening_test.cpp` | few-unit opening mode (`BC_EXPAND_OPENING`) | off | entering and leaving the mode, the queen's relaxed gate, the probe, the mother's bud |
| `queen_growup_test.cpp` | queen grow-up (`BC_QUEEN_GROWUP`, `BC_GROWUP_FEED`) | off | the boxed-turns latch, her ask on the SELF fact, who leaves her food, the feeder |
| `queen_yield_test.cpp` | queen yield (`BC_QUEEN_YIELD`) | off | a teammate next to a hemmed-in queen steps away first |
| `qsk_pathheat_test.cpp` | QSK path heat (`BC_QSK_PATHHEAT`) | off | QSK's enemies-about test measured by path; portal mouths as threat sources |
| `queen_feed350_test.cpp` | late queen feeding (`BC_QUEEN_FEED350` 1-4) | off | from r350 the two nearest non-queens leave a short queen her pearls; modes 2-4 add her side |
| `trap_pocket_test.cpp` | trap pocket (`BC_TRAPPOCKET`) | off | a dead-end tile whose door leads into a dead room is a wall for a dragon of 2+ |
| `sprint_target_test.cpp` | contested sprint (`BC_SPRINT_TARGET`) | off | free steps only toward a contested fresh pearl |
| `open_risk_test.cpp` | opening risk (`BC_OPEN_RISK`, bits 1/2/4/8) | off | before r100 a 1-2 long non-queen stops yielding and waiting, may take a teammate's exit, parks at the best bed |
| `bed_keep_test.cpp` | bed keeping (`BC_BED_KEEP`, `BC_TIER2`) | off | a non-queen keeps a fast bed while its cluster has room; second-tier beds |

## Adding a test

Copy the pattern of an existing file:

- Include the module header, and `policy.hpp` for anything in the search.
- Tests reach private members with `#define private public` around the include. This works for these header-only
  modules and is for tests only. A manual g++ build needs `-include bits/stdc++.h` (`run.sh` adds it itself).
- Count checks with the local `CHECK` macro and end with `printf("%d / %d checks passed\n", ...)`.
- Return non-zero on failure.
- Guard the tests of a switch with `#if BC_...`, so the file still compiles with the switch off.
- Add one manifest line per flag set.

A test must not change the bot. Any code change in `bots/abyss` (a compiled line, a header in the build or a compile-time
define) can give a WASM different from the submitted one (see [bots/abyss/README.md](../bots/abyss/README.md)).
