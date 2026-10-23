# bots

[English](README.md)

bot 源码和测试地图。

| 路径 | 说明 |
|---|---|
| [`abyss/`](abyss/README.zh-CN.md) | 最终版 bot，与提交版完全一致（C++，由 `unswbc` 编译为 WASM） |
| [`fixtures/`](#测试用-bot) | 两个用于最坏情况 CPU 普查的小型测试 bot，不参加比赛 |
| [`maps/unseen50/`](maps/unseen50/README.zh-CN.md) | 50 张没有任何 bot 调过参的生成地图，用于稳健性检查 |

官方比赛地图和模板 bot `starter` 不在本仓库中。它们随 `unswbc` 工具链发布（MIT 许可证，作者为 UNSW CPMSoc），用下面的命令即可生成。

## 搭建工作区

最简单的做法是直接把仓库自带的 `bots/` 目录当作工作区（见根 README 的[快速上手](../README.zh-CN.md#快速上手)；根目录的 `.gitignore` 已经忽略了 `unswbc` 在其中写出的文件）。也可以另建工作区：先安装工具链，再在本仓库之外建立一个工作区目录：

```sh
uv tool install unswbc==1.2.9     # 或（需 Python >= 3.11）：pip install unswbc==1.2.9（最终版 bot 构建和验证时使用的版本）
mkdir abyss-ws && cd abyss-ws
unswbc maps maps                  # 把工具链自带的官方地图复制到 ./maps（不覆盖已有文件）
unswbc init cpp starter           # 生成模板 bot ./starter（常用的基准对手）
cp -R path/to/repo/bots/abyss ./abyss
cp -R path/to/repo/bots/maps/unseen50 ./maps/unseen50
```

然后运行：

```sh
unswbc run maps/arena.map abyss starter                       # 一局原生对战，回放写入 replays/
unswbc run maps/arena.map abyss starter --sandbox --no-replay # 与评测机相同的运行方式：WASM，CPU 以点数计量
```

回放可以在 VS Code 查看器（`unswbc vscode`）或 https://game.battlecode.au 中打开。

另建的工作区应放在仓库之外。`unswbc run` 会写出 `replays/`，编译缓存位于 `${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/`。

最终版 bot 用 `unswbc` 1.2.9 构建和验证。要复现与提交版完全一致的 WASM（sha256 `0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e`），请使用该版本，见 [abyss/README.zh-CN.md](abyss/README.zh-CN.md#确定性与精确构建)。

## 测试用 bot

`fixtures/` 中有两个极简 bot，用于测试最坏情况 CPU 普查工具（`tools/abyss_tools/run/cpu_census.py`）。该工具在 `tools/abyss_tools/run/census_bots/` 中也带有同样的两个 bot。它们并不真正玩游戏。

| bot | 行为 |
|---|---|
| `fixtures/flooder` | 每条龙每回合向四个方向各发一条随机 64 位值的声呐射线，并绕 2x2 方块转圈，让被测 bot 每回合都收到满载的消息。从第 `CENSUS_END` 回合（默认 60）起不再行动，对局以全灭结束。 |
| `fixtures/cpuhog` | 每回合消耗已知数量的 CPU 点数：平时 50M；在第 5、15、25 回合（round % 10 == 5）消耗 95.5M，并输出兜底标记（`LOG fb95 selftest`）；还有一次 101M（第 7 回合、id 1），会被沙箱杀死。普查结果必须准确报告这些消耗。从第 `CENSUS_END` 回合（默认 30）起不再行动。 |

两者都是单文件 C++（`main.cpp` + `bot.toml`），没有 `helper.hpp`，也没有依赖。复制到工作区后可以像普通 bot 一样运行，例如 `unswbc run maps/arena.map flooder cpuhog --sandbox --no-replay`。
