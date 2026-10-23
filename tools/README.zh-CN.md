# abyss_tools

[English](README.md)

UNSW Battlecode 2026 "Abyss" 的 Python 工具集：读取 replay、度量 bot 的行为、用配对种子（paired seeds）在本地对局、检查发布候选版本的 CPU 与稳健性、扫描编译期开关，以及生成测试地图。

这里的一切都在本地 replay 和本地对局上离线运行，不会访问比赛网站。

## 安装

需要 Python 3.9+。核心依赖 `pycapnp` 和 `numpy`；`scipy`（地形距离、lead curve）和 `zstandard`（`.replay.zst` 文件）是可选的。运行对局需要官方工具包 `unswbc`（`uv tool install unswbc==1.2.9`）；decision harness、flood shim 测试和 region 检查需要支持 C++20 的编译器。

在仓库根目录执行：

```sh
pip install -e "tools[full]"          # 或：uv pip install -e "tools[full]"
abyss-replay-stats bots/replays/*.replay
```

不安装时，可在 `tools/` 目录下以模块方式运行任一工具：

```sh
cd tools
uv run --with pycapnp --with numpy python -m abyss_tools.replay.stats ../bots/replays/x.replay
```

## 配置

默认路径都相对于仓库根目录；每一项都可以用命令行参数或环境变量覆盖。

| 变量 | 含义 | 默认值 |
|---|---|---|
| `ABYSS_ROOT` | 仓库根目录 | 第一个包含 `bots/` 的上级目录 |
| `ABYSS_BOTS` | 存放 bot 目录的 unswbc workspace（`--bots`） | `<root>/bots` |
| `ABYSS_MAPS` | `.map` 文件所在目录（`--maps-dir`） | `<bots>/maps` |
| `ABYSS_CACHE` | 缓存（距离矩阵、每局的 lead curve） | `~/.cache/abyss_tools` |
| `UNSWBC` | unswbc 可执行文件（`--unswbc`） | `PATH` 上的 `unswbc` |
| `ARENA_SEED_BASE` | arena 的种子基数（`--seed-base`） | `1000003` |

## 工具

每个工具都可以用 `--help` 打印完整用法。下表列出 console script / 模块名：

### Replay（`abyss_tools.replay`）

| 命令 | 作用 |
|---|---|
| `abyss-replay-stats` / `replay.stats` | 每个 replay 的结果、按原因统计的死亡、分裂次数、峰值兵力、CPU、超时与引擎报错。只有当回放记录了每个动作的指令计数时，CPU 列才有数值；本地 `unswbc` 1.2.9（原生或 `--sandbox`）写出的回放不记录这一项，因此这一列显示 0.0M。本地对局请看 sandbox 运行时打印的每回合点数，或者使用 `abyss-cpu-census` |
| `abyss-replay-state` / `replay.state` | 还原引擎每回合发给每条龙的完整 round block 及其动作（行为克隆数据） |
| `abyss-replay-diff` / `replay.diff` | 逐事件比较两个 replay；用来证明关闭的编译期开关不改变对局 |
| `abyss-rules` / `replay.rules` | 按阵营度量第 500 回合的 queen 规则：对局如何结束、queen 的存活与长度、冲刺、正面击杀、按距离归属的新珍珠刷新，以及两项一致性检查 |

`replay.io.load` 可读取未压缩、gzip 和 zstd 格式的 replay。replay schema（`abyss_tools/schema/replay.capnp`）取自 unswbc 工具包（MIT，UNSW CPMSoc），并附带 Cap'n Proto 的 `c++.capnp`（MIT，Sandstorm）；两者都保留了原有的许可证头。

### KPI（`abyss_tools.kpi`）

每个工具接受一个或多个 replay 目录（例如 arena 运行输出的 `replays/`），按 bot 打印 markdown 表格，并用来源目录（"arm"）标注，便于把候选版本和基线并排比较。

| 命令 | 度量内容 |
|---|---|
| `abyss-opening-kpi` | 首次进食、第 40 回合长度、第 50 / 100 回合单位数及第 100 回合单位差、第 100 回合前的进食数、团灭 |
| `abyss-macro-measure` | 每个 replay 一行 JSON：单位数 / 长度曲线、分裂、吃到的珍珠、死亡、到达各级珍珠床的时间、早期觅食效率 |
| `abyss-census-open` | 基于 `macro_measure` 结果的配对 arm 对基线开局表（`collect` 生成数据行，`report` 做比较） |
| `abyss-lead-curve` | 第 10 至 320 回合在经济各维度（总长度、单位数、进食、覆盖、领地）上谁领先、lead index，以及带 bootstrap 区间的配对差值（需要 scipy） |
| `abyss-spawn-share` | 新刷新的珍珠中离各 bot 更近的比例 |
| `abyss-harvest-yield` | 进入小型封闭房间（珍珠陷阱）的净收益 |
| `abyss-queen-box` | queen 在自己回合被队友堵死的死亡 |
| `abyss-queen-feed` | queen 从第 350 回合起的成长与存活 |
| `abyss-queen-kill` | queen 的死因：敌方回合中由单步 / 免费冲刺 / 付费冲刺造成的正面击杀 |
| `abyss-queen-early-deaths` | 第 150 回合前的每次 queen 死亡，附近的敌方龙头及她最佳落点的安全余量 |
| `abyss-value-trace` | VALUE 信息 trace 标签的计数（仅限 trace 构建） |
| `abyss-region-graph` | 完全已知时地图的房间划分；`--check` 将其与 bot 的房间模块（`bots/abyss/region.hpp`）对比 |

### 运行器（`abyss_tools.run`）

| 命令 | 作用 |
|---|---|
| `abyss-arena A B` | A 对 B，在每张地图上双方各执一边并行对局；使用配对种子，使对同一对手的两个候选版本遇到相同的珍珠刷新和随机数 |
| `abyss-cpu-census` | 在评测计量下检查最坏情况 CPU：满编的大地图、64 单位且收件箱被灌满的地图以及新生龙；报告 p50 / p99 / 最大点数、超过 85M / 95M 的回合、超时和内存；`--scenarios selftest` 用于校验工具本身 |
| `abyss-safety-line CAND DIR` | 对候选版本的 replay 做未见地图检查：0 超时、0 崩溃、queen 检查无误 |
| `abyss-sweep` | 生成只在 `#define` 取值上不同的 bot 变体（追加到其 `ablation.hpp`），与同种子基线一起对局，报告配对差值（lead index 一列需要 scipy，缺少时显示 `-`） |
| `abyss-decision-harness` | 把录制对局中的原始输入喂给 bot 的本地（debug）构建，逐回合打印其 LOG 理由；`--actions` 检查还原的输入能否复现每一步录制动作 |

### 地图（`abyss_tools.maps`）

| 命令 | 生成内容 |
|---|---|
| `abyss-mapgen` | 官方地图池风格的随机对称地图；`--check` 对每张图试玩一局 |
| `abyss-mapgen-cls` | 按类别生成地图：分隔的两半、需经传送门的远距离地图、资源丰富、小图、大图、分散珍珠床；`--check` 对其分类并用引擎解析 |
| `abyss-pearl-variants` | 同一地图但重新抽取珍珠刷新时间 |

## 常见流程

**A/B 测试一个改动。** 复制 bot（`bots/abyss` 到 `bots/cand`），修改后让两者对同一个对手比赛（工具包模板可生成参照对手：`cd bots && unswbc init cpp starter`）：

```sh
abyss-arena cand starter --sandbox -j 8          # -> bots/arena-out/cand_vs_starter
abyss-arena abyss starter --sandbox -j 8
abyss-opening-kpi bots/arena-out/cand_vs_starter/replays bots/arena-out/abyss_vs_starter/replays
abyss-lead-curve bots/arena-out/cand_vs_starter --vs bots/arena-out/abyss_vs_starter
```

判断改动时，应与在相同种子上对局的、未修改 bot 的改名副本比较差值，而不是与 50% 比较：一组固定的配对种子本身就带有偏差。

**发布检查。**

```sh
abyss-cpu-census --bot cand                      # 没有回合超过 95M、没有超时、内存低于 48 MiB
abyss-mapgen --out runs/maps/held --count 25 --seed 900000 --prefix h
abyss-arena cand starter --maps-dir runs/maps/held --sandbox -o runs/held
abyss-safety-line cand runs/held/replays
```

**它为什么走到那里？**

```sh
abyss-decision-harness game.replay A --build bots/abyss --from 120 --to 140
```

bot 的 LOG 输出只在定义 `BC_DEBUG` 时才编译进去；harness 会在它自己的副本中定义该宏。读取 trace 标签的 KPI（`value-trace` 以及 `queen-box` 的牺牲列）需要 trace 构建（`BC_DEBUG 1`、`BC_ECON_TRACE 1`）产生的 replay。

## 测试

```sh
cd tools
uv run --with pycapnp --with numpy --with scipy --with pytest --with zstandard python -m pytest -q
ABYSS_SLOW_TESTS=1 uv run --with pycapnp --with numpy --with scipy --with pytest --with zstandard python -m pytest -q
```

测试数据（`tests/fixtures/*.replay`）是在官方 Arena 地图上用 unswbc 1.2.9 在评测 sandbox 中录制的两局对局：最终版 bot 对工具包的 starter 模板（`--seed 1`，45 回合），以及 bot 对其改名副本（`--seed 2`，179 回合）。在 sandbox 中种子决定整局对局，因此按 [`tests/helpers.py`](tests/helpers.py) 中列出的命令重新录制会得到逐字节相同的文件。缺少可选依赖（pycapnp、scipy、zstandard、unswbc、C++ 编译器）时，相关测试会自动跳过；CPU census 的 sandbox 自检只在设置 `ABYSS_SLOW_TESTS=1` 时运行。
