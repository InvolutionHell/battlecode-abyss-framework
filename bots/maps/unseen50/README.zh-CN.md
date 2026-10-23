# unseen50：未见地图稳健性测试集

[English](README.md)

正式比赛的地图事先未知，珍珠刷新也按场次设定随机种子。只在官方地图上调优的 bot 看起来可能没问题，遇到没见过的地图形状时却可能超时、崩溃或表现失常。本目录收录 50 张生成地图，没有任何 bot 在这些地图上调过参，用作固定的稳健性检查：让每个候选版本在全部 50 张地图上、以双方两种身份、在沙箱中各打一局，然后检查超时、崩溃、非法动作、女王早亡，以及相对基准 bot 的得分变化。

文件按 `s00` 到 `s49` 编号，保证每次运行顺序相同。

## 内容

| 文件 | 来源 | 覆盖范围 |
|---|---|---|
| `s00_h900000.map` - `s24_h900024.map` | 通用生成器，种子 900000-900024 | 仿照官方地图池风格的随机对称地图：尺寸从 16x16 到 64x64，x / y / xy 对称，海藻墙、房间和走廊、成对传送门；珍珠床分为均匀慢速、稀疏、喷泉簇和中等几种；每队 2-4 条龙 |
| `s25_SEP_00.map` - `s28_SEP_03.map` | 分类生成器，SEP 类 | 隔离型：地图分成互不连通的两半，每队被限制在自己一半；喷泉盒只能经传送门进入 |
| `s29_FAR_gated_00.map` - `s32_FAR_gated_03.map` | FAR_gated 类 | 远距门控型：出生房与海藻迷宫隔开，只能经传送门进入迷宫；每个喷泉都要过传送门；双方龙头起始距离很远 |
| `s33_RICH_00.map` - `s36_RICH_03.map` | RICH 类 | 富饶型：地图大，床和喷泉多，每队 5-8 条龙，起始总长度大；单位上限很早就成为约束 |
| `s37_SMALL_00.map` - `s40_SMALL_03.map` | SMALL 类 | 小图：不超过 1100 格，很早就接触 |
| `s41_LARGE_00.map` - `s44_LARGE_03.map` | LARGE 类 | 大图：超过 1100 格，开阔，床稀疏 |
| `s45_SCATTER_00.map` - `s49_SCATTER_04.map` | SCATTER 类 | 分散型：没有喷泉（所有床都刷新得慢），床在抖动网格上均匀分布，尺寸有大有小 |

整个测试集由 25 张通用生成器地图和 25 张按类别生成的地图组成（SEP 4 张、FAR_gated 4 张、RICH 4 张、SMALL 4 张、LARGE 4 张、SCATTER 5 张）。每张地图都严格符合其声明的 `SYMMETRY`。起始龙按文件顺序在两队之间交替，所以单位 id 0 和 1 就是双方女王，这是引擎的要求。文件采用官方 `.map` 格式（https://game.battlecode.au/docs/map-files）。

## 使用方法

在 `unswbc` 工作区中（见 [bots/README.zh-CN.md](../../README.zh-CN.md)），先把本目录复制或链接为 `maps/unseen50`：

```sh
for m in maps/unseen50/*.map; do
  unswbc run "$m" abyss starter --sandbox --no-replay
  unswbc run "$m" starter abyss --sandbox --no-replay
done
```

批量运行时使用 tools 包（见 [tools/README.zh-CN.md](../../../tools/README.zh-CN.md)）。在仓库根目录，`abyss-arena` 在每张地图上并行跑双方两种身份，然后用 `abyss-safety-line` 检查回放：

```sh
abyss-arena CAND OPP --maps-dir bots/maps/unseen50 --sandbox -o runs/unseen50
abyss-safety-line CAND runs/unseen50/replays
```

## 重新生成

生成器位于 `tools/abyss_tools/maps/`。通用生成器（`mapgen`）由"种子 + 序号"完全确定：从种子 900000 开始、前缀为 `h` 生成 25 张，即得到 `h900000` 到 `h900024`。分类生成器（`mapgen_cls`）按类别生成地图；如果某张地图的特征不属于目标类别，就换用下一个派生种子重新生成。本目录中的分类地图是各类别在默认种子下生成的前几张：每类前 4 张，SCATTER 取前 5 张。用本仓库中的生成器，下面两条命令可以逐字节复现全部 50 个文件：

```sh
abyss-mapgen --out OUT --count 25 --seed 900000 --prefix h      # OUT/h900000.map ... OUT/h900024.map
abyss-mapgen-cls --out OUT2 --count 5                           # OUT2/<CLASS>/<CLASS>_00.map ... _04.map
```

生成后，按上表的 `sNN_` 顺序重命名即可。不同版本的生成器可能生成不同的地图，因此请以本目录中的文件为准。
