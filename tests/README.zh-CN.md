# tests

[English](README.md)

这里是 [`../bots/abyss`](../bots/abyss) 中发布版 bot 的原生 C++ 单元测试。每个测试都直接 include bot 自己的头文件，因此编译的正是 `unswbc` 打包进 WASM 的那份代码。测试在小的合成棋盘（一般为 20 x 20）上单独构造每个模块，检查它的决策。不需要工具包、沙箱或对局，有一个支持 C++20 的编译器就够了。

Python 工具有自己的测试，在 [`../tools/tests`](../tools/tests)（pytest，见 [tools/README.zh-CN.md](../tools/README.zh-CN.md#测试)）。RL 流水线的测试在 [`../rl/tests`](../rl/tests)。

## 运行

```sh
tests/native/run.sh                  # 运行 tests/native/manifest.txt 中的全部构建
tests/native/run.sh mapnet queen     # 只运行包含这些词之一的行
tests/native/run.sh -v               # 同时打印每个测试的完整输出
CXX=g++ tests/native/run.sh          # 换一个编译器
```

测试用 `#define private public` 访问私有成员，而 libstdc++ 自己的头文件不接受这种写法。编译器使用 libstdc++ 时（g++，也就是大多数 Linux 系统上默认的 `c++`），`run.sh` 会自动加上 `-include bits/stdc++.h`，让所有标准头文件在这个宏之前读入。使用 libc++（Apple clang）时不加任何参数。

`run.sh` 把 manifest 的每一行编译为 `$CXX -std=c++20 -O2 $CXXFLAGS <flags> -I bots/abyss tests/native/<file>`，然后在一个临时目录中运行生成的程序。程序退出码为 0 且至少执行了一项检查，该行才算通过。任何一行失败，脚本都以 1 退出。每个测试都要编译整个 bot，所以在笔记本上完整跑一遍约需两分钟。

| 变量 | 含义 | 默认值 |
| --- | --- | --- |
| `CXX` | C++20 编译器 | `c++` |
| `CXXFLAGS` | 每次构建额外加的参数 | 无（使用 libstdc++ 时另加 `-include bits/stdc++.h`） |
| `BOT_DIR` | 被测的 bot（例如一份改过的副本） | `bots/abyss` |
| `BUILD_DIR` | 可执行文件和日志的位置（`-k` 保留默认的临时目录） | 临时目录 |
| `ABYSS_MAPS` | 存放官方 `.map` 文件的目录，供 `region_test.cpp` 的真实地图用例使用 | 不设置时跳过该用例 |
| `TEST_TIMEOUT` | 每个测试程序的超时秒数 | `300` |

官方地图随 `unswbc` 工具包一起发布（`unswbc maps DIR` 会把它们复制到 `DIR`）。没有官方地图时，`region_test.cpp` 只跑合成棋盘部分（36 项检查，而不是 85 项）。

在仓库根目录手动运行单个测试：

```sh
c++ -std=c++20 -O2 -DBC_FALLBACK95=1 -DBC_V11_TRACE=1 -I bots/abyss tests/native/fallback95_test.cpp -o fallback95_test
./fallback95_test        # 25 / 25 checks passed
```

使用 g++ / libstdc++ 时，在上面的命令中加上 `-include bits/stdc++.h`。

## 测试内容

bot 的行为模块由编译期开关（`BC_*`，见 [docs/policy/switches.md](../docs/policy/switches.md)（英文））控制。manifest 分两组。

- **最终版本中的模块**：按提交时的设置测试。这些行没有 `-D` 参数，或者只有一个仅供测试用的 trace 参数。
- **未启用的模块**：最终版本里编译时被关掉，测试时用 `-D` 打开。这些代码仍然随 bot 一起发布，因为删掉它们可能改变按指令计量的搜索，从而改变提交的二进制。这些测试说明了被关闭的代码做什么，也保证它仍能编译。

有些开关在 `ablation.hpp` 中被无条件设置，或在文件末尾的最终覆盖段被重新定义，例如 `BC_QUEEN_BREATHE 2`、`BC_LONGEST_RESERVE 3`、`BC_MAPNET_EVIDENCE 2`、`BC_OPEN_EXPAND 1` 和 `BC_OPEN_EXPAND_GATE 28`。`-D` 参数改不了这些开关：最终生效的是 `ablation.hpp` 里的值（编译器会给出宏重定义警告；`ablation.hpp` 先 `#undef` 再定义的宏除外）。因此它们的测试都按最终取值运行；为其他模式写的分支，只有在改过 `ablation.hpp` 的 bot 副本中才会被编译。

| 测试 | 模块（开关） | 最终版本 | 检查内容 |
| --- | --- | --- | --- |
| `comms_test.cpp` | 声呐协议（`comms.hpp`） | 开 | 事实经编码、解码后保持不变；只在发送回合及下一回合被接受；对方队伍的密钥和翻转一位的消息都会被拒绝 |
| `fallback95_test.cpp` | 95 M CPU 兜底（`BC_FALLBACK95`） | 开 | 红线及其计时起点、兜底走法（绝不输出空动作）、Policy 的检查点、输出前丢弃声呐、trace 标记（`BC_V11_TRACE`，仅测试用） |
| `portal_gate_test.cpp` | 传送门闸（`BC_QGATE`、`BC_DEADPORTAL`） | 开 | 女王和其他龙把哪些传送门当作墙（未见过、单格、走廊、死路和过小的封闭落点） |
| `mapnet_test.cpp` | 地图网络（`BC_MAPNET`） | 开 | GATE / REGION / TRAPSET 编解码、闸门状态机及其超时、镜像、由 CLEARED 落点配对传送门、DEAD / TRAP 闸门当墙、女王的食物房间许可、转发 |
| `mapnet_evidence_test.cpp` | 证据分级（`BC_MAPNET_EVIDENCE` 2） | 开 | 推断出的 TRAP 需要证据：探路者 12 回合的沉默、TRAP 60 回合的有效期、探路者 id、食物房间的转发 |
| `mapnet_value_test.cpp` | VALUE 事实（`BC_MAPNET_VALUE`） | 开 | VALUE 事实、每个热度扇区一条记录、扫描、转发、`planValue` 的目标 |
| `queen_breathe_test.cpp` | 女王的呼吸空间（`BC_QUEEN_BREATHE` 2） | 开 | QSK 去掉过窄的落点、连续被困 2 回合后的脱困、队友在她头部周围的禁入区 |
| `queen_boxguard_test.cpp` | 出口守护（`BC_QUEEN_BOXGUARD` 1） | 开 | 谁都不占女王最后一个空闲边，即使自己被困也不把它当退路（加 `BC_ECON_TRACE`：牺牲标记） |
| `queen_growfirst_test.cpp` | 女王优先成长（`BC_QUEEN_GROWFIRST`） | 开 | 第 200 回合前短女王优先进食：适用对象与时机、她的进食权重、QSK 的食物偏好 |
| `queen_keepaway_test.cpp` | 远离敌人（`BC_QUEEN_KEEPAWAY`） | 开 | 在 QSK 保留的落点中，选按路径离最近已知敌方龙头更远的那个 |
| `longest_reserve_test.cpp` | 最长者保留（`BC_LONGEST_RESERVE` 3） | 开 | 谁是保留者（回合、女王、事实的新鲜度、平局）；保留者不分裂 |
| `reserve_feed_test.cpp` | 保留者的喂食者（`BC_LONGEST_RESERVE` 3） | 开 | 最近的两个队友把食物让给保留者；没有提前的喂食阶段 |
| `harvest_test.cpp` | 房间收割（`BC_HARVEST`） | 开 | 哪些封闭房间符合条件、谁进去、进入的一步、在里面进食以及把尾部分裂出去（另以 `-DBC_EXPAND_OPENING=1` 构建一次：少兵开局模式下的收割规则） |
| `unit_slot_test.cpp` | 单位名额、觅食路径（`BC_UNIT_SLOT`、`BC_FORAGE_PATH`） | 开 | 最后一个单位名额留给女王；队友的远程让食距离按已知地图上的路径计算 |
| `open_expand_test.cpp` | 开局扩张（`BC_OPEN_EXPAND`，闸值 28） | 开 | 目标食物区、家里的和已被占的床、倒计时、护卫规则、未见过的地图中心、地图闸 |
| `bedraid_test.cpp` | 开局觅食探路（`BC_BEDRAID`） | 关 | 不加参数：不探路（最终行为）；加参数：饥饿的小龙去探未配对的传送门 |
| `fast_bed_field_test.cpp` | 快床区域（`BC_T64_FIELD`） | 关 | 不加参数：开关为 0；加参数：沿可走图的区域判定、间隔与容差规则 |
| `region_test.cpp` | 已知地图的房间（`BC_MAPNET_ROOMS`） | 关 | 合成棋盘上的房间、拆分与合并、触及格预算和扫描；设置 `ABYSS_MAPS` 时在 8 张官方地图上比较增量更新与完全重建。`--dump MAP` 打印一张地图的房间 |
| `expand_test.cpp` | 空闲扩张（`BC_EXPAND`） | 关 | 空闲的龙去争夺空着的食物核心区 |
| `expand_opening_test.cpp` | 少兵开局模式（`BC_EXPAND_OPENING`） | 关 | 进入与退出该模式、女王放宽的闸、探路、母体的分芽 |
| `queen_growup_test.cpp` | 女王成长（`BC_QUEEN_GROWUP`、`BC_GROWUP_FEED`） | 关 | 被困回合数的锁存、她在 SELF 事实里的请求、谁给她让食、喂食者 |
| `queen_yield_test.cpp` | 给女王让路（`BC_QUEEN_YIELD`） | 关 | 被围住的女王旁边的队友先让开 |
| `qsk_pathheat_test.cpp` | QSK 路径热度（`BC_QSK_PATHHEAT`） | 关 | QSK 的“附近有敌人”按路径判断；传送门口作为威胁源 |
| `queen_feed350_test.cpp` | 后期喂女王（`BC_QUEEN_FEED350` 1-4） | 关 | 第 350 回合起，最近的两个非女王把珍珠让给较短的女王；模式 2-4 加上她自己一侧的规则 |
| `trap_pocket_test.cpp` | 陷阱口袋（`BC_TRAPPOCKET`） | 关 | 门通向死房间的死路格，对长度 2 及以上的龙是墙 |
| `sprint_target_test.cpp` | 争夺冲刺（`BC_SPRINT_TARGET`） | 关 | 免费步只朝有人争夺的新鲜珍珠走 |
| `open_risk_test.cpp` | 开局冒险（`BC_OPEN_RISK`，位 1/2/4/8） | 关 | 第 100 回合前长度 1-2 的非女王不再让路和等待，可以占队友的出口，停在最好的床边 |
| `bed_keep_test.cpp` | 守床（`BC_BED_KEEP`、`BC_TIER2`） | 关 | 只要快床所在的簇还有空位，非女王就守着它；第二梯队的床 |

## 添加测试

照着现有文件的写法：

- include 对应模块的头文件；涉及搜索的，include `policy.hpp`。
- 测试在 include 前后用 `#define private public` 访问私有成员。这对这些只有头文件的模块可行，且仅用于测试。用 g++ 手动编译时需要加 `-include bits/stdc++.h`（`run.sh` 会自动添加）。
- 用文件里的 `CHECK` 宏计数，最后输出 `printf("%d / %d checks passed\n", ...)`。
- 失败时返回非零。
- 某个开关的测试用 `#if BC_...` 包起来，这样开关关闭时文件仍能编译。
- 每组参数在 manifest 里写一行。

测试不得修改 bot。对 `bots/abyss` 代码的任何改动（参与编译的代码行、构建中包含的头文件或编译期宏）都可能让 WASM 与提交版本不同（见 [bots/abyss/README.zh-CN.md](../bots/abyss/README.zh-CN.md)）。
