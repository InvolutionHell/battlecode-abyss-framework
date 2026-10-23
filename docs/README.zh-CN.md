# 文档

[English](README.md)

这里是我们参加 UNSW Battlecode 2026 "Abyss" 的 bot 及其配套框架的文档。最终提交的 bot 位于 [`../bots/abyss`](../bots/abyss)；这里的所有内容都围绕这个 bot、它所玩的游戏，以及我们如何构建和测量它。

## 目录

| 文档 | 内容 |
| --- | --- |
| [rules/game-rules.zh-CN.md](rules/game-rules.zh-CN.md)（[English](rules/game-rules.md)） | 面向实现者的游戏规则：环面棋盘、显示倒计时的珍珠床（镜像床共用一个倒计时）、龙与女王规则、前 ceil(L/4) 步免费的冲刺、分裂、声呐（sonar）、传送门和海藻、胜负判定、CPU 点数预算与沙箱。 |
| [rules/engine-notes.md](rules/engine-notes.md)（英文） | 官方文档没有写明的引擎实测事实：声呐的精确机制、虚拟时钟与按指令计价、95 M 兜底、忽略头文件改动的 WASM 构建缓存、女王身份、可复现性。 |
| [policy/README.zh-CN.md](policy/README.zh-CN.md)（[English](policy/README.md)） | 依据代码写成的最终 bot 架构：世界模型、搜索核心、经济、女王安全、终局、网络先验、CPU 安全，以及哪些编译进去的代码处于关闭状态。 |
| [methodology.md](methodology.md)（英文） | 我们如何评估改动：从回放复盘到改动、机制指标优先于胜率、配对种子加同种子基线组、预先登记的两阶段决策，以及安全门槛（等价性、CPU 普查、未见地图）。 |
| [history.md](history.md)（英文） | 一页纸讲 bot 的演进：从贪心觅食者到以女王为中心的最终版本线，以及没有走通的路线。 |
| [results.zh-CN.md](results.zh-CN.md)（[English](results.md)） | 天梯最好成绩、最终版本的身份（WASM 哈希）、网络权重的数据说明，以及实测效果。 |
| [dev-guide.md](dev-guide.md)（英文） | 修改 bot 的工作规则：每回合必须输出合法动作、在多张地图上对比此前最好的版本、sandbox 下的 CPU 检查、编译期开关、清理 WASM 缓存，以及如何校验发布的 bot。 |

## 从哪里开始

- **初次接触这个游戏：** 先读 [rules/game-rules.zh-CN.md](rules/game-rules.zh-CN.md)，再读 [rules/engine-notes.md](rules/engine-notes.md)（英文）。
- **阅读 bot 的代码：** [policy/README.zh-CN.md](policy/README.zh-CN.md)，配合 [history.md](history.md)（英文）了解它为什么是现在这个样子。
- **修改 bot 或构建自己的 bot：** [dev-guide.md](dev-guide.md)（英文）和 [methodology.md](methodology.md)（英文）。

实现这些方法的工具（对局运行器、回放解析器、指标脚本）在 [`../tools`](../tools) 和 [`../infra`](../infra) 中；网络先验的模仿学习流水线在 [`../rl`](../rl) 中。

这些文档以英文为准；有简体中文版本的页面会在顶部给出链接。
