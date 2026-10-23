# bots

[简体中文](README.zh-CN.md)

Bot sources and test maps.

| path | what it is |
|---|---|
| [`abyss/`](abyss/README.md) | the final bot, exactly as submitted (C++, compiled to WASM by `unswbc`) |
| [`fixtures/`](#fixtures) | two tiny test bots for the worst-case CPU census; not competition bots |
| [`maps/unseen50/`](maps/unseen50/README.md) | 50 generated maps that no bot was tuned on, for robustness checks |

The official contest maps and the template `starter` bot are not part of this repository. They ship with the `unswbc`
toolkit (MIT licence, by UNSW CPMSoc), and the commands below create them.

## Setting up a workspace

The quickest way is the repository's own `bots/` folder (see the root README's
[quick start](../README.md#quick-start); the root `.gitignore` covers what `unswbc` writes there). A separate
workspace works too. Install the toolkit, then make a workspace folder outside this repository:

```sh
uv tool install unswbc==1.2.9     # or, on Python >= 3.11: pip install unswbc==1.2.9 (the version the final bot was built and verified with)
mkdir abyss-ws && cd abyss-ws
unswbc maps maps                  # copies the bundled official maps into ./maps (existing files are kept)
unswbc init cpp starter           # creates the template bot ./starter (the usual baseline opponent)
cp -R path/to/repo/bots/abyss ./abyss
cp -R path/to/repo/bots/maps/unseen50 ./maps/unseen50
```

Then:

```sh
unswbc run maps/arena.map abyss starter                       # one native game, replay in replays/
unswbc run maps/arena.map abyss starter --sandbox --no-replay # as the judge runs it: WASM, CPU metered in points
```

Replays open in the VS Code viewer (`unswbc vscode`) or at https://game.battlecode.au.

A separate workspace should live outside the repository. `unswbc run` writes `replays/`, and the build caches live in
`${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/`.

The final bot was built and verified with `unswbc` 1.2.9. To reproduce the exact submitted WASM (sha256
`0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e`), use that version. See
[abyss/README.md](abyss/README.md#determinism-and-the-exact-build).

## Fixtures

`fixtures/` holds two minimal bots used to test the worst-case CPU census (`tools/abyss_tools/run/cpu_census.py`).
The same two bots also ship with that tool, in `tools/abyss_tools/run/census_bots/`. They do not play the game.

| bot | behaviour |
|---|---|
| `fixtures/flooder` | every dragon casts four sonar rays a turn with random 64-bit values and circles a 2x2 square. The bot under test therefore reads a full inbox every turn. It stops acting at round `CENSUS_END` (default 60), so games end by elimination. |
| `fixtures/cpuhog` | burns a known number of CPU points per turn: 50M normally, 95.5M plus a fallback marker (`LOG fb95 selftest`) on rounds 5, 15 and 25 (round % 10 == 5), and 101M once (round 7, id 1), which the sandbox kills. A census must report exactly these costs. It stops acting at round `CENSUS_END` (default 30). |

Both are plain single-file C++ (`main.cpp` + `bot.toml`), with no `helper.hpp` and no dependencies. Run them like
any bot, for example `unswbc run maps/arena.map flooder cpuhog --sandbox --no-replay` after copying them into a
workspace.
