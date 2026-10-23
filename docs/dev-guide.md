# Developer guide

The rules we worked by while developing the bot, written for anyone who changes this code or builds their own
Abyss bot on top of it. How a change is judged is in [methodology.md](methodology.md); the engine facts behind several
rules are in [rules/engine-notes.md](rules/engine-notes.md).

## Setup

- Toolkit: the official `unswbc` (MIT licence, UNSW CPMSoc). The released bot was built and checked with **1.2.9**:
  `uv tool install unswbc==1.2.9` (or `pip install unswbc==1.2.9`; installing `unswbc` with pip needs Python 3.11 or
  newer, while `uv tool install` picks a suitable Python itself). Versions 1.2.3 to 1.2.9 share the same engine,
  sandbox and compiler; a later toolkit may ship a different compiler and produce a different WASM.
- Language: C++20 compiled to WASM by the judge's clang (`-O2 -msimd128`, libc++ and wasi-libc). No external
  libraries: vendor anything you need as source.
- Run every `unswbc` command from `bots/`:

```sh
cd bots
unswbc maps                                                  # add the bundled maps to bots/maps
unswbc init cpp starter                                      # an untouched template bot to play against
unswbc run maps/arena.map abyss starter --sandbox --seed 0x1 # one game priced like the judge; replay in replays/
unswbc run maps/arena.map abyss starter --sandbox -v         # per-turn points and memory of every dragon
unswbc submit <folder> -n <version> -d "<what changed>"      # upload (needs `unswbc auth` first)
```

Replays open in the replay viewer that `unswbc vscode` installs, or on the platform's web viewer.

## Rules

1. **Always emit a valid action.** The engine's default action is suicide. Guard every code path, including
   exceptions, early returns and a search that found nothing: end with the cheapest legal move, never with silence.
   A deliberate death (for example to feed a teammate) must be an explicit decision, not a fall-through.
2. **Test every change against the previous best on several maps** before it replaces the main bot: at least a tiny
   map (Arena), an open medium map (Default), a small enclosed map (Colosseum) and a portal- or kelp-heavy map
   (Portals, Maze), plus generated maps the bot was not developed on. Use paired seeds and a same-seed baseline arm,
   judge the difference, and record the numbers with the change ([methodology.md](methodology.md)).
3. **Check the CPU in the sandbox before submitting.** Only `unswbc run --sandbox` prices instructions; a plain run has
   no CPU limit. The judge kills a dragon at 100 M points per turn; keep a margin. Our hard line was 95 M: every code
   path that can see the virtual clock pass 95 M falls back at once (to the action already decided, or to the cheapest
   legal move if none is decided yet), drops optional output and leaves at least 3 M points for the output. Give each
   module its own clock budget counted from its own start, and add a test that forces the fallback. Run the check on
   the largest maps at the full unit count and on a newborn child with a full inbox (process start-up is charged to
   its first turn).
4. **Keep debug output behind compile-time flags.** `LOG`, `DOT`, `LINE` and `INDICATOR` lines cost 4 000 points per
   byte and every extra write costs 2.5 M; one write per turn, and debug output compiled out of the submitted build.
5. **Purge the WASM cache after editing a header.** `unswbc` caches the build by folder name and the contents of
   `.c` / `.cc` / `.cpp` files only, so a header-only edit silently runs the old build:

   ```sh
   find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name '<folder>-*.wasm' -delete
   ```

   Use `find -delete`, not `rm` with a glob (in zsh an unmatched glob aborts the command). For batch experiments, give
   every arm a frozen copy of its folder under a unique name.
6. **Switch features at compile time.** Because the search stops at a fixed point budget, even a runtime `if` that
   never fires changes where the search stops and therefore changes games. Add each feature behind `#if` / `#define`,
   with a default that reproduces the previous build byte for byte, and check that by comparing WASM hashes.
7. **Keep the bot general.** No map names, map databases, pearl timetables, opening books or per-map-name tuning:
   tournament maps are unseen and pearl spawns are seeded per match. Learn the map in play.
8. **Take random numbers from the sandbox.** Seed any generator from the sandbox's WASI `random_get`, which is seeded
   per dragon. `rand()` without `srand`, a default-constructed `std::mt19937` or `srand(time(NULL))` give every dragon
   the same sequence. (The released bot uses no random numbers at all, so a sandbox game repeats exactly for a given
   map, seed and side.)
9. **Do not edit toolkit-managed files or the reference bot.** `unswbc update` replaces the template helper files, so
   keep your own code in separate files; keep the `starter` folder untouched as a fixed opponent.
10. **Never commit secrets.** The upload key belongs in the toolkit's key store (`unswbc auth`) or in the `UNSWBC_KEY`
    environment variable, never in the repository.
11. **Fix the rules page when the engine disagrees.** If a probe shows that [rules/game-rules.md](rules/game-rules.md)
    is wrong, correct it in the same change; the engine is the authority.

## Verifying the released bot

The final bot in `bots/abyss` is frozen: its WASM, built by `unswbc` 1.2.9 with `--sandbox`, must have the SHA-256
`0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e`.

```sh
cd bots
find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss-*.wasm' -delete   # never trust a cached build
unswbc run maps/arena.map abyss abyss --sandbox --no-replay --seed 0x1                # builds and plays one game
shasum -a 256 "${XDG_CACHE_HOME:-$HOME/.cache}"/unswbc/wasmbots/abyss-*.wasm          # or sha256sum on Linux
```

The game takes about half a minute (plus about 20 s for the first build) and prints the points per turn of both sides
(median about 78-79 M). Purging first matters: the cache key ignores headers and the toolkit version, so an old or
edited build can be reused silently.

Any change to a compiled source file, a header included in the build or a compile-time define can change the hash;
edits to comments normally do not. If you change the bot, give the folder a new name so the released one stays a
reference.
