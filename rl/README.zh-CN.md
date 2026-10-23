# 学习管线（`rl/`）

[English](README.md)

最终提交的 bot（`bots/abyss`）是一个逐龙（per-dragon）的前瞻搜索，评估项为手写。两个小型卷积网络通过模仿学习训练，作为**先验（prior）**嵌在搜索之中：它们微调第一步的选择、提供分裂（split）触发信号，并为免费多步移动把关。战术上的最终决定（碰撞、陷阱、传送门、女王安全）始终由搜索做出，这也限制了网络出错时能造成的损害。

本目录包含从回放（replay）重新构建这类网络所需的一切：特征提取器（即 bot 自己的 C++ 解析与特征代码）、记录构建脚本、训练与导出脚本、bot 内运行时头文件、自对弈与评估工具、训练 / 部署一致性测试、端到端示例和单元测试。

- [已发布 bot 中包含什么](#已发布-bot-中包含什么)
- [管线](#管线)
- [快速上手：端到端示例](#快速上手端到端示例)
- [在 bot 中的集成](#在-bot-中的集成)
- [训练 / 部署一致性](#训练--部署一致性)
- [自对弈与专家迭代](#自对弈与专家迭代)
- [评估工具](#评估工具)
- [测试](#测试)
- [目录结构](#目录结构)
- [经验教训](#经验教训)
- [术语表](#术语表)

## 已发布 bot 中包含什么

| | 开局先验 | 走法先验 |
|---|---|---|
| bot 中的代码 | `bots/abyss/rl_net.hpp`（开关 `BC_RL_PRIOR`） | `bots/abyss/r4net.hpp`（开关 `BC_R4NET`） |
| 权重 | `bots/abyss/rl_weights.hpp` | `bots/abyss/r4_weights.hpp` |
| 网络 | 9 类策略 + 分裂比例 + 价值（`pretrain.py`） | 16 类策略 + 分裂比例 + 第二步 + 价值（`train_f1.py`） |
| 规模 | conv 17->64，conv 64->64，dense 3,147->128 | 相同的主干 |
| 使用者 | 女王所在进程（龙 id 0 / 1），第 0-199 回合 | 其余所有龙，全部回合 |
| 用途 | 根节点走法先验（权重 0.05），分裂触发（p >= 0.4） | 根节点走法先验（权重 0.02），第 200 回合前的分裂触发（p >= 0.4），免费步把关（P(多步) >= 0.3） |
| 训练摘要（权重文件第一行） | 第 0-200 回合的 506 万个回合样本，6 个 epoch；留出集 top-1 准确率 73.1% | 600 万个抽样的非女王回合，3 个 epoch；按自然类别频率的留出准确率 68.3%，单步方向 73.4%，分裂 AUC 0.97 |

**两个权重文件的数据声明：** trained by imitation on aggregated public ladder replays of top-ranked teams; contains no per-team model; the training data is not included.（基于排名靠前队伍的公开天梯回放的聚合数据，通过模仿学习训练；不包含任何针对单个队伍的模型；训练数据不随本仓库发布。）

除此之外不发布任何学习得到的东西：没有训练用的回放、没有记录、没有其他权重。下面的管线可以在你自己的数据上复现这套方法；[示例](#快速上手端到端示例)的数据来自关闭两个网络后的已发布 bot 的自对弈，因此其产物没有任何第三方来源。

在评测机的每龙每回合 100 M CPU 点数中，每个网络约消耗 9 M（前向计算的写法便于编译器向量化；朴素写法约 30 M）。每个龙进程只构建它用到的那个网络，且只构建一次。已发布 bot 中还有一些默认不编译的网络代码（按地图类别条件化的网络、一条剔除规则、测量用开关）：由于 bot 按提交时的原样发布，而删除代码可能改变按指令计量的搜索，这些代码保留在源码中。bot 侧的说明见 [docs/policy/net-priors.md](../docs/policy/net-priors.md)（英文）。

## 管线

```
 .replay 文件（unswbc run）
      |  abyss_tools.replay.state.turns()：重建对局，渲染每条龙收到的原样回合块
      v
 协议记录  "REC <game> <id> <team> <w> <h> <unitLimit> <labels...> <action>" + 回合块 + "END"
      |  rl/featurize/featurize_{rl,f1,f2}（C++：bot 的 io.hpp 解析器 + rl_features.hpp 特征提取器）
      v
 字节记录：头部（标签、长度、回合、结果位……）+ 17 x 7 x 7 + 10 个特征（+ 记忆输入）
      |  rl/train/pretrain.py（9 类）   rl/train/train_f1.py（16 类，可选 Q 头）   [torch]
      v
 权重：int16 数值，每个输出行一个 float 缩放（export_bot.py / train_f1.py）
      |  rl/runtime/*.hpp = bots/abyss/rl_net.hpp、r4net.hpp
      v
 bot 内：在搜索根节点 total[d] += w * log p(第一步 d)；分裂触发；免费步把关
```

### 1. 回放到协议块

`rl/data/replay_src.py` 使用仓库中的 `abyss_tools` 包（`tools/`）。`turns(replay, side, sample, rng)` 回放所有事件、维护完整状态，并在每个回合渲染引擎发给该龙的回合块（协议 3：带珍珠倒计时的视野格、可见身体、边、64 位声呐值、回声计数），以及它采取的动作（`MOVE <dirs>`、`SPLIT <n>`、`NONE`）。渲染文本而不是在 Python 中计算特征，使特征只有一份实现：bot 的那份。

`rl/data/index_replays.py` 为回放目录建立索引（每个对局的每一方一行 JSON：路径、bot 名、阵营、引擎给出的结果）。构建脚本每次读取索引的一个分片，因此可以多个并行运行。

### 2. 特征

`rl/featurize/rl_features.hpp` 与 `bots/abyss/rl_features.hpp` 逐字节相同，`rl/featurize/io.hpp` 中的协议结构体和解析器与 bot 的 `io.hpp` 相同；`rl/featurize/check_features.py` 会校验这两点（修改任意一侧后都要运行）。视野经过旋转，使龙的朝向朝“上”，因此所有类别都是相对朝向的。

| 通道 | 特征（7 x 7 网格，旋转到朝向） |
|---|---|
| 0 | 该格有珍珠 |
| 1, 2 | 珍珠床；下一次刷新的 exp(-倒计时 / 8) |
| 3, 4, 5 | 自身身体、队友身体、敌方身体 |
| 6, 7 | 队友的头、敌方的头 |
| 8-11, 12-15 | 海藻边、传送门边，按相对朝向的方位（前、右、后、左） |
| 16 | 其他龙头相对我方朝向的方向，(rel + 1) / 4 |

标量（10 个）：回合 / 500，min(长度, 32) / 16，单位数 / 64，min(消息数, 4) / 4，五个回声计数（海藻、友方、友方头、敌方、敌方头）取 min(n, 4) / 2，回合 >= 400。网络另外还输入回合数本身。特征以字节存储，round(clip(v, 0, 2.55) x 100)，bot 在前向计算前以同样方式量化自己的输入，因此训练与对局看到的数值完全一致。

### 3. 记录

| 构建脚本 | 特征提取器 | 记录 | 标签 |
|---|---|---|---|
| `build_rl.py` | `featurize_rl` | 6 + 843 字节 | 9 类：0-3 向前 / 右 / 后 / 左单步，4-7 按第一步区分的多步移动，8 分裂（无动作回合被跳过） |
| `build_f1.py` | `featurize_f1` | 16 + 843 字节 | 16 类：（步数 1 / 2 / 3+）x 4 个第一步方向，分裂 n = 2 / 3-4 / 5+，无动作；另含分裂 n、第二步、最终最长龙与总长度差、该回合时回放中的事实 |
| `build_f2.py` | `featurize_f2`（`featurize_f2h`） | 16 + 843 + 24（44）字节 | 同 F1，另加龙自身的记忆输入（`rl/runtime/f2_memory.hpp`），并可选 `aux_labels.py` 的后果标签与回报 |

精确的字节布局见 `rl/featurize/featurize_*.cpp` 的文件头。结果字节包含该方的胜负（bit 0）、该行的权重类别（bit 5-6），以及加 `--aux` 时的四个后果标签（bit 1-4：5 回合内分裂、5 回合内吃到珍珠、10 回合内因敌方接触死亡、10 回合内有敌方撞在它身上死亡；bit 7 标记已带标签的记录）。约 95% 的回合是单步移动，所以 `build_f1.py` / `build_f2.py` 会对其抽稀（`--move1-rate`，第 100 回合前 x4、第 200 回合前 x2），并存下每条记录的保留率；训练时按 1 / 保留率加权，从而学到自然的类别频率。`--no-queen` 丢弃女王的回合（已发布的走法先验只用其他龙训练）。当回放的地图文本中没有珍珠床时，构建脚本会给出警告：此时与珍珠床相关的通道会是空的。

### 4. 训练

`pretrain.py`（9 类网络）：AdamW 配余弦学习率；动作类别的交叉熵，胜方回合权重 1、负方回合权重 `--loser-weight`（0.3）（或用 `--awr-beta` 做优势加权）；分裂回合上以 n / 长度作为逻辑回归目标学习分裂比例；价值头学习对局结果；可选的辅助头学习后果标签（`--aux-weight`）。

`train_f1.py`（16 类网络）：相同主干；记录按 1 / 保留率加权；输出头包括 16 个类别、分裂比例、多步移动的第二步以及三个价值目标；可从 `pretrain.py` 模型热启动（9 类头被展开到 16 类），也可从其他 F1 模型热启动。可选项：记忆输入（`--mem 24`，即 F2）、特权输入（`--priv`）、只在实际采取的类别上训练的动作价值头（`--q`、`--q-returns`、`--q-team`）、带专家迭代目标的自对弈记录（`--sp`）。`soup.py` 对同一配方的多个副本取平均。

### 5. 导出

`export_bot.py` 把 `pretrain.py` 的浮点头文件转成 `rl_weights.hpp`：int16 数值，每个输出行一个 float 缩放（在留出记录上 argmax 与浮点网络的一致率超过 99.9%；int8 按行量化为 98.7%），体积约为源文件的一半。`--prefix RLA` / `--prefix RLL` 会重命名符号，以便在一个 bot 中放第二个网络（`rl_aux.hpp`、`rl_late.hpp`）。`train_f1.py` 直接以同样格式写出 `f1_weights.hpp`。生成的头文件只以基本文件名引用来源，并在第一行记录训练元数据；不会写入任何本地路径。

### 6. 运行时

| 头文件 | 内容 |
|---|---|
| `rl_net.hpp` | `RLNet`，9 类网络（`bots/abyss/rl_net.hpp` 的库版本；`forwardFast` 是 bot 使用的计算路径） |
| `rl_prior.hpp` | `g_rl`：把 9 类网络作为即插即用的移动 / 分裂先验，适用于任何共享 `io.hpp` 的 bot |
| `rl_f1.hpp` | `F1Net`，带全部可选输出头的 16 类网络（`bots/abyss/r4net.hpp` 是读取 `r4_weights.hpp` 的副本） |
| `f2_memory.hpp`、`f2_comms.hpp` | F2 网络的龙自身记忆输入；bot 声呐事实的解码器（`F2_HEARD`） |
| `f2_run.hpp` | `f2Run(net, mem, init, turn)`：一致性测试所校验的每回合单次调用 |
| `rl_aux.hpp`、`rl_late.hpp` | 在一个 bot 中放第二个 9 类网络：只用辅助头，或作为后期先验 |

## 快速上手：端到端示例

```sh
cd <仓库根目录>
rl/examples/make_example.sh            # 在 Arena 上自对弈 2 局；RL_ROOT=./rl_runs/example
MAPS="arena default" NCPUS=8 rl/examples/make_example.sh 3     # 12 局
```

需要 `unswbc`（官方工具包）、C++20 编译器（unswbc 原生构建 bot 时使用 -std=c++20；特征提取器只需 C++17）和 `uv`（或把 `PYTHON` 设为已安装 numpy、pycapnp 和 torch 的解释器）。脚本会：

1. 把 `bots/abyss` 复制为 `$RL_ROOT/ws/abyss_nonet`（`RL_ROOT` 默认为 `./rl_runs/example`），以 `-DBC_RL_PRIOR=0 -DBC_R4NET=0` 构建：这两个定义以 `#undef` / `#define` 行的形式写在其 `ablation.hpp` 末尾（unswbc 不传递编译器参数，而且 `ablation.hpp` 自己设置了 `BC_R4NET`，命令行定义无法生效），并删除两个权重文件，因此构建本身就证明对局中没有任何网络、也没有任何模仿训练得到的权重参与；
2. 让它与自己的孪生副本对弈（`rl/selfplay/selfplay.py`，固定种子，原生运行或 `SANDBOX=1`）；
3. 构建特征提取器、9 类记录和非女王的 F1 记录；
4. 训练一个很小的 9 类网络，以及从它热启动的很小的 F1 网络（`CHANNELS` / `HIDDEN`，默认 16 / 32）；
5. 导出它们并装入 `$RL_ROOT/ws/abyss_example`（`rl/integration/make_arm.py`），然后编译并对弈一局；
6. 针对 `$RL_ROOT/ws/abyss_example` 自己的代码运行一致性测试：两项检查都必须输出 `PASS`。

这些小网络不是有用的选手（只有几百条记录）；目的是得到一条能运行、可校验的管线。真正训练时请使用数千个对局方、默认规模（64 / 128）和分片构建，例如：

```sh
python3 rl/data/index_replays.py RUNS_DIR --recursive --out DATA/index.jsonl
for s in $(seq 0 15); do python3 rl/data/build_f1.py --data DATA --featurize BIN/featurize_f1 --shard $s --nshards 16 \
    --no-queen --out DATA/f1_$s.bin & done; wait
NCPUS=16 python3 rl/train/train_f1.py "DATA/f1_*.bin" --out RUN --init PRETRAIN_RUN/model.pt --hours 2
python3 rl/integration/make_arm.py abyss_mynet --r4-weights RUN/f1_weights.hpp
```

## 在 bot 中的集成

每个龙进程在每回合搜索之前、并在搜索自己的计时之内执行（`bots/abyss/policy.hpp`，搜索 `BC_RL_PRIOR` 和 `BC_R4NET`）：

- **开局先验**（女王所在进程；回合小于 `rlUntil` = 200）：`RLNet::run`，然后对每个合法的第一步 `d`，搜索值加上 `wPrior * log(p_single(d) + p_multi(d) + 1e-3)`，`wPrior` = 0.05；`p(split) >= rlSplitP`（0.4）是 bot 自身分裂规则的一个输入。
- **走法先验**（其余每条龙）：`F1Net::run`，然后加上 `BC_R4_W * log(p(d | 龙移动) + 1e-3)`，其中 p 在合法第一步上归一化，`BC_R4_W` = 0.02，作用于回合 `BC_R4_FROM`..`BC_R4_UNTIL`（0..500）。
- **分裂触发**（`BC_R4_SPLIT_ON`）：第 200 回合前，在这些进程中用 F1 网络的 `splitProb() >= BC_R4_SPLITP`（0.4）替代 9 类网络的触发，因此这些进程从不构建 9 类网络。
- **免费步把关**（`BC_R4_SPRINT` 6）：当网络给出的 P(多于一步 | 所选第一步) >= `BC_R4_SPRINTP`（0.3）时，长度 >= 5 的非女王龙沿网络的第二步输出头走至多 `BC_R4_MAXSTEPS`（2）个免费步，遇到珍珠即停。女王从不这样做；路径与我方女王头部的切比雪夫距离必须大于 `BC_R4_QRING`（2），并避开她最后一个空闲方向；落点必须在所有已知敌方龙头的免费步可达范围之外再留出余量（`BC_R4_LAND` 1），并为该龙保留足够空间（`r4Sprint`）。

所有开关都是 `bots/abyss/ablation.hpp` 中的编译期宏；`rl/integration/make_arm.py` 可以生成使用其他取值或其他权重的实验副本。跟踪构建（`BC_DEBUG 1`、`BC_ECON_TRACE 1`）会记录先验的作用；`rl/eval/prior_flips.py` 汇总这些标记。

对于其他 bot，`rl/runtime/rl_prior.hpp` 给出了 9 类网络的最小集成方式：

```cpp
#include "rl_prior.hpp"                       // + rl_net.hpp, rl_features.hpp, io.hpp, rl_weights.hpp
bc::g_rl.run(init, turn);                     // 每回合一次，在搜索之前
if (bc::g_rl.priorOn) value[d] += bc::g_rl.w * bc::g_rl.logDir(d, turn.dir);   // 每个合法的第一步 d
```

## 训练 / 部署一致性

只有当每条训练记录都与 bot 对局时计算输入的方式完全一致，网络才能正常工作。两项检查：

1. `rl/featurize/check_features.py`：训练特征提取器的 `rl_features.hpp` 与 bot 的逐 token 相同（并报告是否逐字节相同），两个 `io.hpp` 中的协议结构体与解析器也相同。
2. `rl/eval/consistency/`：`make_vectors.py` 从回放中取若干条龙的完整生命周期，写出回合块（`turns.txt`），并针对每个权重文件写出训练特征提取器的字节，以及对导出权重的独立 float64 numpy 前向计算结果（`ref_forward.py`）。`check.cpp` 针对某个 bot 目录编译，用该 bot 自己的解析器、特征提取器和网络代码重放这些回合块，逐回合比较：特征与记忆字节必须完全一致，概率误差在 1e-5 以内，logits 相对误差在 1e-4 以内。

```sh
rl/featurize/build.sh BIN
python3 rl/eval/consistency/make_vectors.py GAME.replay --featurize BIN/featurize_f2 --out VEC \
    --weights r4=bots/abyss/r4_weights.hpp --weights rl=bots/abyss/rl_weights.hpp
c++ -std=c++17 -O2 -ffp-contract=off -DF1_NET_HEADER='"r4net.hpp"' -I bots/abyss -I rl/runtime \
    rl/eval/consistency/check.cpp -o check_r4 && ./check_r4 VEC/turns.txt VEC/expect_r4.txt
c++ -std=c++17 -O2 -ffp-contract=off -DCHECK_RLNET -I bots/abyss rl/eval/consistency/check.cpp -o check_rl \
    && ./check_rl VEC/turns.txt VEC/expect_rl.txt
```

已发布 bot 的两个网络都通过了这项检查，特征字节差异为 0（在测试回放上的最大误差：概率低于 1e-6，logits 相对误差低于 1e-5）。

## 自对弈与专家迭代

`rl/selfplay/selfplay.py` 用 `abyss_tools.run.arena` 让一个 bot 对阵一组对手（与自己对弈时通过孪生副本），并为该 bot 的对局方建立索引；任何记录构建脚本都可以直接读取。`ei_round.sh` 是 F2 网络一轮专家迭代的学习部分：先用全部标签构建专家自身回合的 F2 记录，再用 `train_f1.py` 在示范记录加上按固定权重份额混入的自对弈记录上训练。通过环境变量配置（`RL_ROOT`、`NCPUS`、`RL_SP`、`RL_INIT` 等，见脚本文件头）。依赖它之前请先阅读下面的经验教训：在我们的实验中，自对弈目标没有改进策略。

## 评估工具

| 工具 | 内容 |
|---|---|
| `eval/probe_nets.cpp` | 在同一批 F1 记录上比较 9 类网络与 F1 网络：按自然权重的移动准确率与 NLL、分裂概率分位分布（用于设定标记相同比例回合的分裂阈值），可选 CSV 输出 |
| `eval/aux_eval.py` | 9 类网络辅助头按长度与阶段的校准度与 AUC |
| `eval/q_calib.py` | F2 网络 qDie 头在留出记录上的校准（ECE、AUC、Brier、可靠性表） |
| `eval/prior_flips.py` | 基于跟踪构建的回放：走法先验改变搜索最佳一步的频率及其代价；免费步的使用情况与拒绝原因 |
| `integration/make_arm.py` | 生成 bot 的实验副本（开关、权重、`--no-nets`），配合 `infra/` 和 `tools/` 中的工具做配对比较 |
| `python -m abyss_tools.replay.diff A B` | 关闭开关检查（位于 `tools/`）：新模块关闭时，实验组在相同地图和种子下必须逐事件重现原版的对局 |

比较实验组时，要与在相同种子下对弈的、改名后的原版精确副本比较，而不要看绝对数值：两个完全相同的 bot 在一组固定种子上也不会恰好得到 50%。

## 测试

```sh
rl/tests/run_tests.sh          # 由 uv 提供 pytest、numpy、pycapnp、torch
```

`rl/tests/test_rl.py` 检查：特征提取器的一致性（以及该检查能发现单个 token 的改动）；运行时头文件在很小的随机权重上的行为（`dummy_weights.py`、`runtime_smoke.cpp`：输出已归一化、向量化计算与标量计算一致、记忆输入正确）；基于 `tools/tests/fixtures` 中样例回放的记录构建（布局、标签、F1 与 F2 记录相互吻合）；小规模训练后 int16 导出与 torch 模型一致；在随机网络（含反例对照）和已发布 bot 的两个网络上运行一致性测试。

## 目录结构

```
rl/
  featurize/    rl_features.hpp（== bots/abyss/rl_features.hpp）、io.hpp、common.hpp、featurize_{rl,f1,f2}.cpp、
                build.sh、check_features.py
  data/         replay_src.py、index_replays.py、merge_index.py、build_rl.py、build_f1.py、build_f2.py、aux_labels.py
  train/        pretrain.py、train_f1.py、export_bot.py、soup.py
  runtime/      rl_net.hpp、rl_prior.hpp、rl_f1.hpp、f2_memory.hpp、f2_comms.hpp、f2_run.hpp、rl_aux.hpp、rl_late.hpp
  integration/  make_arm.py
  selfplay/     selfplay.py、ei_round.sh
  eval/         probe_nets.cpp、aux_eval.py、q_calib.py、prior_flips.py、consistency/{make_vectors.py, ref_forward.py, check.cpp}
  examples/     make_example.sh
  tests/        test_rl.py、run_tests.sh、dummy_weights.py、runtime_smoke.cpp
```

配置只通过命令行参数和环境变量完成：`RL_ROOT`（shell 驱动脚本的工作目录）、`NCPUS`（并行任务数、torch 线程数）、`ABYSS_BOTS`（unswbc 工作区）、`ABYSS_TOOLS_PATH`（`abyss_tools` 所在位置）、`UNSWBC`（工具包可执行文件）、`RL_MPS=1`（在 Apple GPU 上训练）。

## 经验教训

以下是我们在构建和调优这些网络时测得的结果，来自内部基准上的配对对局（镜像对局以及一组固定的陪练对手）和留出的回放。

**搜索中的先验**

- *网络只做微调，决定由搜索做出。* 搜索把吃到一颗珍珠的价值定为约 1。对每一步施加权重 0.3 的 log 先验，会让测试对局中的龙回合数减半：龙去模仿移动习惯，而不是去吃珍珠。权重 1.0，或只在接近最优的几步之间施加强先验，都会输。对每一步施加 0.02-0.05 是安全区间；0.6 大约损失 6 个胜率百分点。
- *开局先验通过经济收益起作用。* 在配对对局中去掉它，到第 200 回合时总长度约少 10 节、吃到的珍珠约少 23 颗。它的价值主要来自第一步先验；当 bot 自己的规则已经在相同长度分裂时，分裂触发几乎没有额外贡献。
- *免费步是最明显的差距。* 被模仿的长度 >= 5 的龙大约一半的移动是多步移动，而我们的搜索几乎从不这样做。用网络的 P(多步) 为免费步把关（排除女王，并检查落点安全），是走法先验中唯一带来稳定收益的部分：在约 700 局配对对局上合计约 +5 个胜率百分点（2.3 sigma）。没有落点安全检查时，它会害死更多我方的龙。
- *CPU 很重要。* 朴素前向计算每回合约占 100 M 点中的 30 M，压缩了搜索；改为输出通道在最内层（可向量化）并跳过零输入后降到约 9 M，输出不变。每个进程只构建一个网络，只反量化一次。

**模仿学习**

- *准确率会饱和。* 无论怎样尝试，第一步准确率都停在 72-73%：更宽的网络（96 / 256 通道）、更多数据、记住龙见过的一切的非局部记忆（+0.3 个百分点）。7 x 7 视野携带的信息不足以预测得更准。对局中的收益来自先验的使用方式，而不是准确率。
- *信号在稀有动作里。* 分裂概率能很好地排序分裂回合（AUC 约 0.97），多步与无动作回合也可预测（AUC 0.94-0.97）。按类别抽样，存下保留率，并按其倒数加权。
- *地图习惯是陷阱，地图覆盖不是。* 网络能从局部的墙和珍珠床认出地图并重放每张图的习惯；这在一些地图上有帮助、在另一些地图上有害，地图变化后就失去价值。专门针对某类地图的网络在本类地图上更好，在其他地图上更差；一个在所有地图类别上合并训练的网络在每张留出地图上都优于之前的网络。面对未见过的地图，请使用通用网络。
- *不要给策略网络输入示范数据中从未出现过的信息。* 回放知道而龙不知道的全局事实（特权输入）对策略没有帮助。我方队伍的解码消息在其他队伍的回放中全为零，因此用这些回放训练的策略从未见过非零值；对局时它们会让策略偏移（分裂变少）。这类输入应放进用自己的对局训练的价值头中。

**从结果中学习**

- *短视野会惩罚成长。* 5 回合视野的动作价值（“很快会吃到”）让重排后的策略不再分裂：分裂的那一回合是原地不动的。改用龙的谱系价值（它自己的珍珠加上从它分裂出去的龙的珍珠）解决了这个问题。规则：短视野价值可以重排移动，但不能决定成长类决策（分裂、多步、无动作）。
- *专家迭代需要更强的专家。* 六轮以自对弈结果作为策略目标的实验全部为负：“专家”（网络加上安全层后面的价值重排）并不比网络本身强，所以蒸馏它就是蒸馏它的偏差，而它自己的状态又稀释了示范数据。按一致性筛选目标也会使安全层最常否决的类别代表性不足。自对弈数据只对价值头有用。
- *由网络主导的 bot 输给了搜索 bot。* 位于安全层之后的逐龙策略网络在所有变体中都远不如搜索 bot：逐龙模仿没有团队计划（谁成长、谁喂食、谁护卫）。网络作为搜索中的先验才物有所值。

**评估**

- *训练噪声很大。* 同一配方、不同种子的副本在 124 局配对对局中对同一个参照网络的得分从 39% 到 54% 不等；把已退火的网络再续训一个 epoch，无论用什么数据都会损失 10-20 局。评判新配方至少要用两个种子。模型汤（soup）和 logit 集成没有带来稳定收益。
- *初筛结果会缩水。* 八个 40 局初筛中最好的那个，在 160 局时回落到接近 50%。先确认，再相信。
- *使用配对种子和相同种子的基线*，并从引擎自己的结果中读取胜者：根据最终长度重新计算的胜者忽略了女王规则，在约 15% 的对局中与引擎不一致。
- *检查记录里到底有什么。* 缺少珍珠床信息的回放会悄无声息地让两个特征通道为空；磁盘写满曾经截断过记录分片（构建脚本现在会检查记录大小）。

## 术语表

- **女王（queen）**：龙 0 和龙 1；第 500 回合后更长的女王获胜（规则 1.2.3）。
- **king**：在标签名中指一方最长的龙。
- **免费步 / 多步移动**：多步的 `MOVE`；长度为 L 的龙前 ceil(L / 4) 步不消耗身体。
- **F1 / F2**：不带 / 带龙自身记忆输入的 16 类网络。
- **保留率（keep rate）**：构建脚本保留某类回合的比例；训练时按其倒数加权。
- **实验组（arm）**：只在开关或权重上不同的 bot 实验副本。
