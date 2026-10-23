# unseen50: an unseen-map robustness set

[简体中文](README.zh-CN.md)

Tournament maps are not known in advance, and pearl spawns are seeded per match. A bot tuned only on the official
maps can therefore look fine and still time out, crash or fall apart on a map shape it has never met. This folder
holds 50 generated maps that no bot was tuned on. They serve as a fixed robustness check: play every candidate on all
50 maps, on both sides, in the sandbox. Then look for timeouts, crashes, illegal actions, early queen deaths, and the
score change against a baseline bot.

The files are numbered `s00` to `s49` so that every run uses the same order.

## Contents

| files | source | what they cover |
|---|---|---|
| `s00_h900000.map` - `s24_h900024.map` | the generic generator, seeds 900000-900024 | random symmetric maps in the style of the official pool: sizes 16x16 to 64x64, x / y / xy symmetry, kelp walls, rooms and corridors, paired portals, and pearl beds that are uniform-slow, sparse, fountain clusters or medium; 2-4 dragons per team |
| `s25_SEP_00.map` - `s28_SEP_03.map` | the class generator, class SEP | separated: two halves with no path between them, each team confined to its half; fountain boxes entered only through a portal |
| `s29_FAR_gated_00.map` - `s32_FAR_gated_03.map` | class FAR_gated | far, gated beds: a spawn room walled off from a kelp maze, reachable only through portals; every fountain needs a portal; heads start far apart |
| `s33_RICH_00.map` - `s36_RICH_03.map` | class RICH | rich: large, many beds and fountains, 5-8 dragons per team with a long starting total length; the unit cap binds early |
| `s37_SMALL_00.map` - `s40_SMALL_03.map` | class SMALL | small: at most 1100 tiles, early contact |
| `s41_LARGE_00.map` - `s44_LARGE_03.map` | class LARGE | large: more than 1100 tiles, open, sparse beds |
| `s45_SCATTER_00.map` - `s49_SCATTER_04.map` | class SCATTER | scatter: no fountains (every bed refills slowly), beds spread evenly on a jittered grid, small or large |

So the set is 25 maps from the generic generator plus 25 class-targeted maps (4 SEP, 4 FAR_gated, 4 RICH, 4 SMALL,
4 LARGE, 5 SCATTER). Every map is exactly symmetric under its declared `SYMMETRY`. The starting dragons alternate
teams in file order, so unit ids 0 and 1 are the two queens, as the engine requires. The format is the official
`.map` format (https://game.battlecode.au/docs/map-files).

## Use

From a `unswbc` workspace (see [bots/README.md](../../README.md)), with this folder copied or linked as
`maps/unseen50`:

```sh
for m in maps/unseen50/*.map; do
  unswbc run "$m" abyss starter --sandbox --no-replay
  unswbc run "$m" starter abyss --sandbox --no-replay
done
```

For batches, use the tools package ([tools/README.md](../../../tools/README.md)). From the repository root,
`abyss-arena` plays both sides on every map in parallel, and `abyss-safety-line` then checks the replays:

```sh
abyss-arena CAND OPP --maps-dir bots/maps/unseen50 --sandbox -o runs/unseen50
abyss-safety-line CAND runs/unseen50/replays
```

## Regenerating

The generators are in `tools/abyss_tools/maps/`. The generic generator (`mapgen`) is deterministic in seed + index:
25 maps from seed 900000 with prefix `h` give `h900000` to `h900024`. The class generator (`mapgen_cls`) makes maps
per class and regenerates with the next derived seed until a map's features land in its class. The class maps here
are the first maps of each class from its default seed: the first 4 of each class, and the first 5 of SCATTER. With
the generators in this repository these two commands reproduce all 50 files byte for byte:

```sh
abyss-mapgen --out OUT --count 25 --seed 900000 --prefix h      # OUT/h900000.map ... OUT/h900024.map
abyss-mapgen-cls --out OUT2 --count 5                           # OUT2/<CLASS>/<CLASS>_00.map ... _04.map
```

After generating, rename the files to the `sNN_` order above. A different generator version can give different maps,
so treat the files in this folder as the reference set.
