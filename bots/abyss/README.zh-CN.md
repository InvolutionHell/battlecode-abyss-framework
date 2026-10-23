# abyss：最终版 bot

[English](README.md)

本目录是我们在 UNSW Battlecode 2026 "Abyss" 中提交的最终版 bot，按提交时的原样公开。改动包括：重写了注释，删去了不参与编译的历史记录文件，重写了两个权重文件的第一行文件头。所有会到达编译器的代码 token、宏、`#if` 块和字符串都与提交版完全相同。此外还有一处改动：一个处于关闭状态的 `#if V10K_KING_VETO` 分组里的 `#error` 消息（原文提到一份内部设计笔记）；预处理器会跳过这个分组，编译器根本看不到它。因此用 `unswbc` 1.2.9 加 `--sandbox` 编译本目录，得到的 WASM 与提交版逐位一致（见[确定性与精确构建](#确定性与精确构建)）。

按这份代码写成的设计说明见 [docs/policy](../../docs/policy/README.zh-CN.md)。

## 构建与运行

这是一个标准的 `unswbc` C++ 工程：`bot.toml` 中 `language = "c++"`，入口为 `main.cpp`，其余模块都是头文件。工程不含 `helper.hpp`，也不依赖外部库。在一个 `unswbc` 工作区（含 `maps/` 的目录，见 [bots/README.zh-CN.md](../README.zh-CN.md)）中执行：

```sh
cp -R path/to/repo/bots/abyss ./abyss   # 若直接把仓库自带的 bots/ 当作工作区，跳过这一行
unswbc run maps/arena.map abyss starter                       # 本机原生构建，回放写入 replays/*.replay
unswbc run maps/arena.map abyss starter --sandbox --no-replay # 评测机沙箱：WASM，CPU 以点数计量
```

`unswbc run --sandbox` 会把编译出的 WASM 缓存为 `${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots/<bot>-<hash>.wasm`。缓存键只覆盖 `.c/.cc/.cpp` 文件，修改 `.hpp` 不会触发重新编译。再次运行前请先删除缓存：

```sh
find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss-*.wasm' -delete
```

如需脱离工具链调试，可以原生编译（单一编译单元）：

```sh
c++ -std=c++20 -O2 -o abyss_native main.cpp
```

做实验时可以用 `-D` 覆盖编译期开关，例如 `-DBC_QGATE=0`。凡是默认值包在 `#ifndef` 里的开关都可以这样覆盖。`ablation.hpp` 无条件设定的开关，以及它在文件末尾重新定义的开关，只能直接修改文件。任何改动都会得到与公开版不同的二进制。

## 架构概要

每条龙是一个独立进程，彼此不共享内存。分裂出的子龙一开始世界模型为空，要靠自己的 7x7 视野和队友的声呐重新建立认知。`main.cpp` 中每回合依次执行：

1. **观察**（`world.hpp`、`board.hpp`）：只根据视野更新世界模型。内容包括墙与传送门、珍珠床及其刷新间隔、地图对称性（镜像得到的床与海藻）、自身身体、其他龙、敌方目击信息，以及双方女王（单位 id 0 和 1）。
2. **收听**（`comms.hpp`、`region.hpp`）：解码带校验标签的 64 位声呐事实并更新转发表。事实包括位置、最长龙、敌人、热度，以及地图网络的传送门状态、封闭房间和陷阱集合，这些事实也会输入地图网络的危险层。（协议中定义了表示死亡掉落物的 DROPS 事实，但本构建从不发送它，因为拾荒规划器是关闭的。）
3. **参数**（`tuned_params.hpp`、`common.hpp`）：根据龙判断自己所在的地图组（在线判断，从不使用地图名）和对局阶段，选取调好的 `Params`。
4. **决策**（`policy.hpp`）：
   - 先做各项计划：战术、局部小队（`team.hpp`）、目标、开局扩张、储备龙、收割、传送门探测、围攻小组、喂食和弑后（regicide）。
   - 然后对自身走法序列做随时可停的前瞻搜索，逐层加深，直到用完 75M 点预算。
   - 两个通过模仿学习得到的小网络为根节点走法和分裂触发提供先验：回合 200 前的女王进程用 `rl_net.hpp`，其余龙用 `r4net.hpp`。
5. **女王安全**（`qsk.hpp`）：我方女王的落点由女王安全内核（QSK）按硬性分层选出，搜索值只在同一层内排序。队友在她周围遵守友军纪律（房间守护、呼吸空间、最后出口）。
6. **守卫与输出**（`main.cpp`、`io.hpp`）：做最后的合法性检查，包括女王多步检查、单位名额、合法分裂和付得起的冲刺。动作和最多四条声呐射线在一次写出中发出；300 回合前除事件类事实外保持静默。最后运行 95M 兜底检查。

国王残局库（`v10k.hpp`、`king/`）和局部战斗搜索（`fight.hpp`）会被编译进来，但在本构建中从不改变任何决策（见[未启用代码](#关闭或未启用)）。

## 文件一览

| 文件 | 内容 |
|---|---|
| `bot.toml` | `unswbc` 工程文件（C++；包含目录下全部源文件） |
| `main.cpp` | 入口：每回合主循环、参数选择、最后的合法性守卫、输出和 95M 检查 |
| `common.hpp` | 编译期开关及默认值、`Params`（全部可调数值）、回合预算和 `fb95` |
| `ablation.hpp` | 最终构建的开关设置，以及定义提交配置的覆盖项 |
| `tuned_params.hpp` | 自动生成：按（地图组, 对局阶段）调好的 `Params`，由 `applyTuned` 应用 |
| `io.hpp` | 标准输入行读取、回合解析，以及每回合一次的缓冲写出 |
| `board.hpp` | 环面几何、格子边界（海藻、传送门）、死胡同检测 |
| `nav.hpp` | 环面上考虑时间的 BFS 和泛洪（自身身体从尾部开始让出） |
| `world.hpp` | 单个龙进程的世界模型：地图知识、珍珠床、对称性、目击、声呐事实、女王 |
| `comms.hpp` | 声呐协议：事实类型、位布局、认证我方帧的标签、转发规划 |
| `region.hpp` | 地图网络状态（`BC_MAPNET`）：传送门状态（探测 / 已确认 / 陷阱 / 死路）和危险度；其中的增量房间图（`BC_MAPNET_ROOMS`）处于关闭状态 |
| `team.hpp` | 局部小队：互相可见的龙之间的成员关系、队长和目标分配 |
| `policy.hpp` | 决策策略：角色、计划、随时可停的前瞻搜索、评分、分裂、探测、喂食、先验 |
| `qsk.hpp` | 女王安全内核：可达范围模型、落点分层 T1-T5、安全裕量（其中的成长分支 `BC_QUEEN_GROW` 处于关闭状态） |
| `fight.hpp` | 局部战斗搜索（已编译，但最终参数中权重为 0，因此不起作用） |
| `rl_features.hpp` | 训练特征提取与对局推理共用的观测特征 |
| `rl_net.hpp` | 开局先验网络（策略 / 价值），在 bot 内用 int16 权重推理 |
| `rl_weights.hpp` | 开局先验网络的生成权重 |
| `r4net.hpp` | 非女王龙的走法先验网络（走法、多步和分裂输出头） |
| `r4_weights.hpp` | 走法先验网络的生成权重 |
| `v10k.hpp` | 搜索核心与国王残局库之间的适配层（未启用：`V10K_R0 = 501`） |
| `king/core.hpp` | 国王库的入口头文件及每回合调用顺序 |
| `king/proto.hpp` | 国王库第 1 部分：声呐帧、校验和与黑板 |
| `king/crown.hpp` | 国王库第 2 部分：谁当国王（加冕声明、继承人）和模式状态机 |
| `king/sigma.hpp` | 国王库第 3 部分：领先 / 落后估计 |
| `king/hunger.hpp` | 国王库第 4 部分：残局中国王的成长目标 |
| `king/feed.hpp` | 国王库第 5 部分：喂食角色和有上限的献祭 |

## 最终构建的开关

下面是提交版构建中宏实际取值的摘要，用 `c++ -std=c++20 -E -dM main.cpp` 读出（输出中的 `BC_BASE_MAX`、`BC_DIM_MAX`、`BC_SCALE_MAX` 和 `BC_STRING_MAX` 是 `<limits.h>` 中的 POSIX 限制，不是 bot 开关）。默认值位于 `common.hpp` 和 `ablation.hpp`，`ablation.hpp` 末尾的覆盖项决定最终取值。完整清单（每个宏的状态和 `文件:行号` 指针）见 [docs/policy/switches.md](../../docs/policy/switches.md)（英文）。开关设为 1 并不等于模块一定在起作用：有些模块被调好的运行期参数关掉，有些被别的模块遮蔽，有些只是给一组调参旋钮起的标签。这些开关单独列在下面。

### 开启且起作用

| 开关 | 含义 |
|---|---|
| 搜索核心（`BC_TUNED`） | 带手写价值项和危险项的随时可停前瞻搜索；每个参数按（地图组, 阶段）调优 |
| 蜂群（没有独立开关） | 在 `swarmUntil` 之前，每条长度 4 以上、不是国王的龙在安全时对半分裂 |
| `BC_MIRROR` | 地图对称：镜像得到的床、喷泉和海藻无需亲见即可知晓；对只从镜像得知的喷泉加大牵引（`wMirrorHot` 1.23 倍） |
| `BC_FAR_EXPLORE` | 远程牵引到最近的未探索边界格 |
| `BC_HEAT` | 双方在 5 x 4 扇区网格上的存在热度图：敌方占据的扇区不喂食、不去闲置的床，发送 HEAT 事实，并作为内核的"迷雾"输入。其中"散开"的牵引是关闭的（`wHeatSpread` 为 0） |
| `BC_ENGAGE` | 依据在天梯回放上拟合的对撞价值模型决定进攻 / 坚守 / 撤退 |
| `BC_V9_EKING` | 早期国王：从调好的回合（200）起，我方最长的龙（长度至少 8）保留全部身段，其他龙把它附近的食物让给它 |
| `BC_V9_FINISH` | 从第 300 回合起、我方有 40 条以上的龙时，每条长度 <= 8 的空闲龙奔向 6 回合内看到或听到的最近敌方龙头 |
| `BC_TRAPPED_PORTAL`、`BC_EAT_NO_DISPERSE` | 必死的龙先试未配对的传送门；吃食的一步不计分散惩罚 |
| `BC_RL_PRIOR` | 开局先验网络：仅女王进程、回合 < 200，根节点权重 `wPrior` 0.05，分裂阈值 `rlSplitP` 0.4 |
| `BC_QUIET` | 安静开局：300 回合前不发例行声呐（事件类事实照常发送） |
| `BC_PORTAL2` | 传送门战术：封闭且贫瘠的区域经由未配对传送门离开（饥饿探测）。"每个口袋一个守卫"不属于这个开关，它由 `Params::pocketOn`（默认 1，未参与调参）控制 |
| `BC_QUEEN` | 女王规则：我方女王（id 0 或 1）决定第 500 回合的胜负；她从不对撞，也从不献祭自己；没有任何规则专门喂她（只有当她恰好是全队最长的龙时才会被喂食）；保护围绕她展开 |
| `BC_QUEEN_QSK` | 女王安全内核：按 T1-T5 硬性分层选她的落点 |
| `BC_QUEEN_QKILL` | 非女王龙在够得着时击杀敌方女王（QKILL），并防住稍后出手的袭击者（QGUARD） |
| `BC_QUEEN_HUNT` | 追踪并共享敌方女王位置、自发形成的猎手（弑后）、利用免费步追击 |
| `BC_QUEEN_ROOMGUARD` | 友军纪律：队友走到女王 Chebyshev 距离 2 以内、且使她可达的格子少于 max(L, 2) + 2 时，这一步的代价加 30 |
| `BC_QUEEN_EXITFIX` | QSK 的 T5 层按回放校准的敌方长度计算安全裕量，据此给免费落点排序 |
| `BC_QUEEN_NODEAD` | 只要还有其他落点，女王就不落在死胡同格 |
| `BC_QUEEN_BREATHE` = 2 | 女王保持 2 个空闲边；队友踏入她头部周围 8 格要付出代价 |
| `BC_QUEEN_GROWFIRST` | 200 回合前、长度不足 5 时，女王优先吃食 |
| `BC_QUEEN_KEEPAWAY` | 在 QSK 保留的落点中，女王选沿路径离已知敌方龙头更远的那个 |
| `BC_QUEEN_BOXGUARD` | 队友绝不占用女王最后一个空闲边，即使自己被困、这是它唯一的退路也不行 |
| `BC_UNIT_SLOT` | 非女王龙从不占用最后一个单位名额（留给女王做分裂保命） |
| `BC_ECON_B0` | 经济守卫：女王的多步移动只能来自 QSK |
| `BC_ECON_DIP` | 死胡同 / 喷泉支路：短龙钻进去吃食，再用反向分裂离开 |
| `BC_ECON_BED` | 刷新时钟：等待价值、倒计时补全（镜像床共享同一倒计时）、解锁、不堵床、空闲时去床 |
| `BC_HARVEST` | 收割传送门后的小喷泉房间：在里面吃食，再把后段分裂出传送门 |
| `BC_LONGEST_RESERVE` = 3 | 从第 300 回合起，全队最长的非女王龙保留全部身段并优先吃食（eatBonus x2），最近的两名队友把食物让给它 |
| `BC_QGATE` | 女王从不探测传送门；对她而言，不安全或未知的传送门落点视同墙 |
| `BC_DEADPORTAL` | 对所有龙：已知落点是 1 格房间的传送门视同墙 |
| `BC_MAPNET`（FACTS、REGION、EXPLORE、TRAPSET；`EVIDENCE` = 2，`VALUE` = 1） | 共享地图网络：传送门与房间事实、带证据分级的陷阱推断、危险层、床区 VALUE 事实 |
| `BC_R4NET` | 非女王龙的走法先验网络：权重 0.02，全程使用（`UNTIL` 500），第 200 回合前分裂阈值 P 0.4（`SPLIT_ON`，与蜂群规则重复），由网络控制的免费步（`SPRINT` 6、`SPRINTP` 0.3、`MAXSTEPS` 2），配合低成本落点规则（`LAND` 1） |
| `BC_OPEN_EXPAND`（W 0.4，`GATE` 28） | 非女王龙在第 120 回合之前向空闲床区扩张。门控（位 4、8、16）放行三种情况：起始龙 <= 2 条且格数 <= 700 的地图；格数 <= 700 地图上后出生的龙；任意格数 <= 700 的地图；位 16 已包含位 4 和 8，实际效果就是所有 W x H <= 700 的地图 |
| `BC_FALLBACK95` | 95M 点硬性 CPU 兜底（见下文） |

残局喂食采用搜索核心的单国王规则。它从第 350 回合起启用（`Params::oneKingFrom`），但只在喂食窗口内起作用，即第 400-489 回合（所有调参上下文中 `feedRound` 都是 400，`feedStop` 都是 490）：看得见国王（LONGEST 事实指出的那条龙）、且国王不短于自己的非女王队友，会走到国王旁边，死在它的头部旁边。女王从不喂别人，也没有规则专门喂她（`BC_QUEEN_FEED` 为 0）：只有当她恰好是 LONGEST 事实指出的那条龙时才会被喂食。

### 设为 1，但只部分起作用、不起作用、被遮蔽或只是标签

| 开关 | 状态 | 实际运行的部分，以及其余部分不运行的原因 |
|---|---|---|
| `BC_FULL` | 部分 | 最终推进关闭（`finalFrom` 500）；它的后期反向分裂保护在运行（第 350 回合起长度 10 以上的龙） |
| `BC_KING` | 部分 | 最长敌龙记录在运行；它的喂食块关闭（`kingOn` 为 0） |
| `BC_SLAY` | 部分 | 最长敌龙记录和 EKING 事实在运行；slay 规划器被弑后规划器（`BC_QUEEN_HUNT`）遮蔽 |
| `BC_SCAV` | 部分 | 检测死亡掉落物并输入危险层；拾荒规划器关闭（`wScav` 为 0），因此从不发送 DROPS 事实 |
| `BC_DEADEND` | 部分 | 计算死胡同格（供支路收割、NODEAD、女王的传送门闸使用）；搜索里的死胡同否决关闭（`deadEndOn` 为 0） |
| `BC_V9_DENSITY` | 不起作用 | 主动分裂的密度上限；`kDensity` 为 1，上限等于整张地图 |
| `BC_FORAGE_PATH` | 不起作用 | 对听到的队友改用路径距离，但结果只乘以 `remoteYield` 1.0 |
| `BC_QUEEN_FLEE` | 被遮蔽 | 2-3 步逃跑；它唯一的调用点要求内核关闭且 `BC_QUEEN_SAFE` 开启 |
| `BC_V9_TAIL` | 关闭 | 尾部尚未被追踪到时也把反向分裂计入考虑；`tailOn` 为 0 |
| `BC_V9_SCOUT` | 关闭 | 按未探索扇区派出开局侦察；`scoutUntil` 为 0 |
| `BC_ROOMCAP` | 关闭 | 封闭房间的容量限制；所有调参上下文中 `roomOn` 都是 0 |
| `BC_QUEEN_LOCKALL` | 关闭 | LOCK 模式的不对撞范围；依赖 `BC_QUEEN_MODES`，而它为 0 |
| `BC_COVER`、`BC_SWARM`、`BC_PHASED`、`BC_FINAL` | 标签 | 没有代码读取它们；它们只是调参工具搜索空间里几组旋钮的名字（蜂群本身始终运行） |

### 关闭或未启用

| 开关 | 含义，以及不运行的原因 |
|---|---|
| `BC_QUEEN_KING`、`_SPLIT`、`_SAFE`、`_ZONE`、`_FEED`、`_GROW` | 早期女王模块，在 ablation.hpp 中显式设为 0，已被 QSK 和单国王规则取代 |
| 国王残局库（`v10k.hpp`、`king/`） | 已编译，但 `V10K_R0` = 501 超过最后一回合，每回合只执行几次常数时间的判断 |
| FightSearch（`BC_FIGHT`、`fight.hpp`） | 已编译，但所有调参上下文中 `wFight` 都是 0，从不改变决策 |
| `BC_MAPNET_ROOMS` | 增量重算已知地图的房间；没有任何决策读取它 |
| `BC_EXPAND_OPENING` | 针对起始兵少、食物在门后的地图的开局"冒险模式" |
| `BC_BED_KEEP` | 守住一张快速刷新的床，等待它刷新 |
| `BC_QUEEN_GROWUP`（及 `BC_GROWUP_FEED`） | 在容易被围的地图上让女王长大，由队友喂她 |
| `BC_V9_KING` | 后期国王喂食模块（已被单国王规则取代） |
| `BC_OPEN_RISK` | 开局小龙争抢食物，不相互礼让 |
| `BC_R6` | 按龙判断地图类别的分类网络。孤立代码：其头文件未随本仓库发布，必须保持为 0 |
| `BC_EXPAND` | 空闲的龙去争夺没有队友占据的床区核心 |
| `BC_QUEEN_FEED350` | 从第 350 回合起队友喂食较短的女王 |
| `BC_ECON_TRACE` | 在 LOG 行中输出经济追踪标签（仅追踪构建） |
| `BC_T64_FIELD` | 在快速床附近加大走法先验的权重 |
| `BC_TIER2` | 快速床已满时守住次一级的床 |
| `BC_TRAPPOCKET` | 把陷阱口袋（唯一的出路是陷阱的路口）当作墙 |
| `BC_QSK_PATHHEAT` | QSK 按路径距离判断"附近有敌人" |
| `BC_QUEEN_YIELD` | 局促的女王身边的队友先让开 |
| `BC_R7_KINDW_ON`（及 `BC_R7_TOPB_ON`） | 走法先验的其他加权方式 |
| `BC_R4_NOISE` | 对照设置：用伪随机先验代替网络 |
| `BC_SPRINT_TARGET` | 朝有争夺的新鲜珍珠走免费步 |
| `BC_R5_GATE`（及其余处于中性默认值的 `BC_R5_*` 变体） | 随机化的免费步门控 |
| `BC_BEDRAID` | 开局时饥饿的短龙穿过未知传送门去找食物 |
| 其他女王变体：`BC_QUEEN_TINY`、`_HOME`、`_ESCORT`、`_MODES`、`_KINAVOID`、`_SLACK` | 只有 2 节长、其余全部分出去的女王；偏好己方腹地；护卫；团队模式；不贴近女王头部的规则；可达余量过滤 |
| `BC_V9_ESCAPE`、`BC_V9_KFEED`、`BC_KINGGUARD` | 不论单位数都去探测未配对传送门的逃离；只喂国王；让最长的龙担任守卫 |
| `BC_FREESPRINT`、`BC_R4_CULL`、`BC_R4_CALM` | 朝最近食物走免费步；在我方女王旁边献祭；只在视野内没有敌人时使用先验 |
| `BC_ECON_CROWN` | 有定义，但没有代码读取它 |

开关为 0 的代码要么被编译掉，要么退化为一次常量判断。保留这些代码是有意为之：回合时钟按指令计量，即使删掉从不运行的代码，也可能改变内联和代码布局，从而改变随时可停搜索在预算内能达到的深度，进而改变走法。

## 网络权重

`rl_weights.hpp` 是开局先验，回合 200 前由女王进程使用；`r4_weights.hpp` 是非女王龙的走法先验。两者都是生成的 C++ 头文件，每行存 int16 数值加一个 float 缩放系数，每个进程在首次使用时反量化一次。

> 数据声明（Data statement）：trained by imitation on aggregated public ladder replays of top-ranked teams; contains
> no per-team model; the training data is not included.
>
> 译文：基于排名靠前队伍的公开天梯回放的聚合数据，通过模仿学习训练；不包含任何针对单个队伍的模型；训练数据不随本仓库发布。

## CPU 预算与 95M 兜底

评测机按点数计量每回合的消耗（虚拟时钟约 1 ns 记 1 点），单回合超过 100M 点的龙会被杀死。

- 搜索预算为 75M 点（`BC_TURN_BUDGET` 0.075 秒）。
- QSK 的上限是搜索截止时间 + 10M，并被限制在硬线以下。
- 开启 `BC_FALLBACK95` 后，任何检查点一旦发现本回合计量时钟超过 95M，就立即兜底：保留已经决定的动作；若在策略检查点（尚未搜索）触发，则走最便宜的合法一步（走向一个空闲邻格，否则直行），绝不输出空动作。声呐射线会被丢弃，使唯一一次写出（约 2.5M 点）仍在 100M 以内。
- 策略检查点复用其精简判断（lean test）已有的时钟读取，QSK 的上限被限制在硬线以下；后面两个检查点（发射声呐射线之前，main.cpp:191；写出之前，main.cpp:253）只在所有依赖时钟的工作完成之后才重新读取时钟。在搜索内部新增时钟读取会改变搜索预算，从而改变走法。第四个检查点保护国王库的射线，它在本构建中从不运行。
- 新生子龙如果初始化就用掉了第一回合的大部分时间，就只走一步精简回合，不做搜索。
- 测试构建可以强制触发兜底（`BC_T13_BURN 1`）并记录日志（`BC_V11_TRACE 1`），两者在最终构建中均为 0。

最坏情况 CPU 普查工具及其测试用 bot 位于 `tools/abyss_tools/run/` 和 [`bots/fixtures/`](../fixtures)。

## 确定性与精确构建

提交版由 `unswbc` 1.2.9 加 `--sandbox` 编译（clang 编译到 `wasm32`，`-O2 -msimd128`），sha256 为：

```
0f39d15f10a503b5d5602144660b8c3d3e62cae53ed1fa94e792c88e395e3b0e
```

在装有 `unswbc` 1.2.9 的工作区中校验：

```sh
cp -R path/to/repo/bots/abyss ./abyss_check
find "${XDG_CACHE_HOME:-$HOME/.cache}/unswbc/wasmbots" -name 'abyss_check-*' -delete
unswbc run maps/arena.map abyss_check starter --sandbox --no-replay
shasum -a 256 "${XDG_CACHE_HOME:-$HOME/.cache}"/unswbc/wasmbots/abyss_check-*.wasm
```

bot 不读取任何随机源，需要随机选择的地方都用对局状态（回合、id、龙头）的哈希代替。搜索是随时可停的，深度取决于时钟。沙箱中的时钟是按指令计量的虚拟时钟，所以用固定 `unswbc run --seed` 的沙箱对局可以完全复现。我们做过验证：在 `default.map` 上以 `--seed 7` 对战 `starter`，两次沙箱运行写出的回放逐字节相同。原生运行时时钟是真实时间，搜索深度和走法可能每次不同。换用其他工具链版本时，如果其 clang 或 CPU 计量方式不同，对局结果也可能不同。
