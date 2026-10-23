# Compile-time switches of the final build

Every optional module of `bots/abyss` sits behind a preprocessor switch. Defaults are in `common.hpp` (and a few other
headers); `ablation.hpp`, included first, sets the final build's values. The values below are the **effective** ones,
read from the preprocessor itself (`clang++ -std=c++20 -E -dM main.cpp` in the bot folder), not from the source
comments. Pointers are `file:line` in `bots/abyss`: "defined at" is the `#define` that takes effect, "main code" the
place where the module does its work.

Status legend:

| status | meaning |
|---|---|
| runs | compiled in and acting in games |
| runs (n) | as above, with the mode or bit set n |
| partly | part of the module acts, part is switched off (see the row) |
| inert | compiled and executed, but in this build its result cannot change a decision (it still costs instructions, which moves the search's stopping point) |
| off | compiled out by `#if`, or behind a compile-time or tuned run-time value that never lets it act (the optimiser removes most such code) |
| shadowed | compiled, but its call site is replaced by another module |
| knob | a numeric parameter of a module; the module's own row gives its status |
| derived | defined from other switches |
| label | no code depends on it; it names part of the tuning tools' search space |
| unused | defined, and nothing reads it |

Run-time switches that are not macros (tuned `Params` values that are 0 in every context, such as `wFight`, `wScav`,
`kingOn`, `roomOn`, `scoutUntil`) are listed in [search-core.md](search-core.md#parameters-map-group-and-phase).
`BC_DEBUG` (debug `LOG` output) is undefined in the release build.

To check the table against a build, run in the bot folder:

```sh
clang++ -std=c++20 -E -dM main.cpp | grep -E '^#define (BC|V10K)_' | sort
```

The output also contains `BC_BASE_MAX`, `BC_DIM_MAX`, `BC_SCALE_MAX` and `BC_STRING_MAX`. Those are the POSIX limits of
the `bc` calculator from the system's `limits.h`, not switches of the bot.

## Search core and older modules

Defaults in `common.hpp`; none of them is overridden by `ablation.hpp`.

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_TUNED` | `1` | runs | common.hpp:1171 | common.hpp:1190 | set when `tuned_params.hpp` is present: the tuned `Params` per (map group, phase) |
| `BC_TURN_BUDGET` | `0.075` | knob | common.hpp:1212 | main.cpp:60 | search deadline in seconds of the virtual clock (0.075 = 75 M points) |
| `BC_PHASED` | `1` | label | common.hpp:97 |  | names the per-(map group, phase) parameter space of the tuning tools; `paramsFor(group, phase)` is always used |
| `BC_COVER` | `1` | label | common.hpp:84 |  | names the coverage knobs in the tuning space (team yield, remote yield, team size, rival fields) |
| `BC_SWARM` | `1` | label | common.hpp:87 |  | names the swarm knobs in the tuning space (`swarmUntil`, `smallRisk`, `lateNoSplit`); the swarm itself always runs |
| `BC_FINAL` | `1` | label | common.hpp:100 |  | names the final-push knobs in the tuning space (`finalFrom` 500: never) |
| `BC_FULL` | `1` | partly | common.hpp:90 | policy.hpp:1075 | the final push is off (`finalFrom` 500); its late reverse protection runs (dragons of 10+ from round 350) |
| `BC_MIRROR` | `1` | runs | common.hpp:25 | world.hpp:1224 | map symmetry inference, mirrored beds / gaps / kelp, the mirror-fountain pull |
| `BC_FAR_EXPLORE` | `1` | runs | common.hpp:28 | policy.hpp:2983 | long-range pull to the nearest never-seen tile of an unexplored sector |
| `BC_EAT_NO_DISPERSE` | `1` | runs | common.hpp:31 | policy.hpp:3974 | the dispersal cost does not tax a step that eats |
| `BC_TRAPPED_PORTAL` | `1` | runs | common.hpp:34 | policy.hpp:892 | a doomed dragon prefers an unpaired portal to certain death |
| `BC_HEAT` | `1` | runs | common.hpp:37 | world.hpp:153 | 5 x 4 heat of our and enemy heads; enemy-held sectors (no feeding, no idle bed), the kernel's fog, HEAT facts; the heat-spread pull is off (`wHeatSpread` 0) |
| `BC_SCAV` | `1` | partly | common.hpp:40 | world.hpp:864 | drop detection runs and feeds the danger layer; the scavenger planner is off (`wScav` 0) and no DROPS facts are sent |
| `BC_ENGAGE` | `1` | runs | common.hpp:43 | policy.hpp:3241 | head-on trades by the fitted trade-value model (attack, hold, retreat) |
| `BC_DEADEND` | `1` | partly | common.hpp:46 | board.hpp:78 | dead-end tiles are computed (lane dips, NODEAD, the queen's gate); the dead-end veto in the search is off (`deadEndOn` 0) |
| `BC_KING` | `1` | partly | common.hpp:49 | policy.hpp:610 | the longest-enemy record runs; this feeding block is off (`kingOn` 0) |
| `BC_KINGGUARD` | `0` | off | common.hpp:52 |  | the longest dragon plays guard from `guardFrom` (superseded by the early king) |
| `BC_SLAY` | `1` | partly | common.hpp:55 | policy.hpp:3269 | the longest-enemy record and EKING facts run; the slay planner is shadowed by the regicide planner (`BC_QUEEN_HUNT`) |
| `BC_V9_EKING` | `1` | runs | common.hpp:59 | policy.hpp:293 | the early king from round 200 ([endgame.md](endgame.md)) |
| `BC_V9_DENSITY` | `1` | inert | common.hpp:62 | policy.hpp:4121 | density cap on voluntary splits; `kDensity` 1 makes the cap the whole map |
| `BC_V9_TAIL` | `1` | off | common.hpp:65 | policy.hpp:3497 | count on a reverse split before the tail is tracked; `tailOn` 0 |
| `BC_V9_ESCAPE` | `0` | off | common.hpp:68 |  | a starving dragon probes unpaired portals whatever the unit count |
| `BC_V9_SCOUT` | `1` | off | common.hpp:71 | policy.hpp:3318 | opening scouts per unexplored sector; `scoutUntil` 0 |
| `BC_V9_KFEED` | `0` | off | common.hpp:74 |  | feeding only the king, throttled by the drops around its head |
| `BC_V9_FINISH` | `1` | runs | common.hpp:78 | policy.hpp:3359 | late hunts: from round 300 with 40+ units, a free dragon of length 8 or less heads for the nearest enemy head seen or heard within 6 rounds |
| `BC_V9_KING` | `0` | off | common.hpp:81 |  | the late-king module with feeding stream and saturation rules |
| `BC_RL_PRIOR` | `1` | runs | common.hpp:93 | policy.hpp:366 | the RL net: opening prior and split trigger for the queen's process before round 200 ([net-priors.md](net-priors.md)) |
| `BC_FIGHT` | `1` | off | common.hpp:103 | policy.hpp:838 | compiles `fight.hpp`; the local fight search never runs (`wFight` 0, `fightTradeMin` -10). The same `#if` also includes `qsk.hpp`, so setting it to 0 breaks the build |
| `BC_QUIET` | `1` | runs | common.hpp:107 | main.cpp:160 | no sonar before `sonarFrom` 300 except event facts |
| `BC_PORTAL2` | `1` | runs | common.hpp:110 | policy.hpp:4265 | a closed, poor region is left through its unpaired portals (hungry probes) |
| `BC_ROOMCAP` | `1` | off | common.hpp:113 | policy.hpp:4469 | room capacity for closed rooms; `roomOn` 0 (the pocket guard runs on its own switch, `pocketOn`) |

## Queen

The final build's queen layer is the safety kernel plus friend discipline ([queen-safety.md](queen-safety.md)).

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_QUEEN` | `1` | runs | common.hpp:116 | policy.hpp:291 | the queen rule: ids 0 / 1; no trades, no feeding, trapped portal first, queen identity and tracking |
| `BC_QUEEN_QSK` | `1` | runs | common.hpp:136 | policy.hpp:942 | the Queen Safety Kernel picks her landing by tiers |
| `BC_QUEEN_EXITFIX` | `1` | runs | ablation.hpp:22 | qsk.hpp:713 | tier 5 ranks landings by margins under calibrated cut-chain lengths |
| `BC_QUEEN_NODEAD` | `1` | runs | ablation.hpp:25 | qsk.hpp:361 | no landing on a dead-end tile while she stands on an open one |
| `BC_QUEEN_BREATHE` | `2` | runs (2) | ablation.hpp:36 | qsk.hpp:397 | she keeps 2 free sides; mode 2: teammates pay 60 for any step into the 8 tiles around her head |
| `BC_QUEEN_ROOMGUARD` | `1` | runs | ablation.hpp:18 | policy.hpp:3757 | a teammate's first step within Chebyshev 2 of her head that leaves her fewer than max(L, 2) + 2 reachable tiles costs 30 |
| `BC_QUEEN_BOXGUARD` | `1` | runs (bit 1) | ablation.hpp:45 | policy.hpp:109 | bit 1: never take her last free side, even trapped; bit 2 (tier-3 landings next to a teammate's head) off |
| `BC_QUEEN_KEEPAWAY` | `1` | runs | ablation.hpp:44 | qsk.hpp:475 | inside her best tier, prefer the landing farther by path from the nearest enemy head |
| `BC_QUEEN_GROWFIRST` | `1` | runs | ablation.hpp:43 | policy.hpp:102 | before round 200 and while shorter than 5: eat weight x2 and food-first landings |
| `BC_GROWFIRST_UNTIL` | `200` | knob | common.hpp:1116 |  | GROWFIRST's last round + 1 (200) |
| `BC_GROWFIRST_LEN` | `5` | knob | common.hpp:1119 |  | GROWFIRST's length limit (5) |
| `BC_QFEED_EAT` | `(BC_QUEEN_FEED350 >= 2 \|\| BC_QUEEN_GROWFIRST)` | runs | common.hpp:411 | qsk.hpp:652 | derived (= GROWFIRST): inside the tier, a landing that eats first |
| `BC_QFEED_PULL` | `(BC_QUEEN_FEED350 >= 3 \|\| BC_QUEEN_GROWFIRST)` | runs | common.hpp:413 | qsk.hpp:659 | derived (= GROWFIRST): then the landing nearest food within 6 steps |
| `BC_QFEED_SRC` | `(BC_QUEEN_FEED350 >= 4 \|\| BC_QUEEN_GROWFIRST)` | runs | common.hpp:415 | qsk.hpp:516 | derived (= GROWFIRST): food out of her view counts too |
| `BC_QUEEN_QKILL` | `1` | runs | ablation.hpp:273 | policy.hpp:1425 | QKILL and QGUARD for every dragon with id 2 or more ([endgame.md](endgame.md)) |
| `BC_QUEEN_HUNT` | `1` | runs | ablation.hpp:275 | policy.hpp:2173 | enemy-queen tracking and tombs, regicide hunters, free-step pursuit |
| `BC_HUNT_PURSUE_MINLEN` | `5` | knob | common.hpp:1122 |  | the shortest pursuer (5) |
| `BC_REGICIDE_AGE` | `10` | knob | common.hpp:1125 |  | regicide uses an enemy-queen fact while this fresh (10 rounds) |
| `BC_REGICIDE_PER` | `8` | knob | common.hpp:1128 |  | regicide hunters = clamp(units / this, 1, 3) (8) |
| `BC_QGATE` | `1` | runs | ablation.hpp:28 | world.hpp:366 | the queen never probes; unsafe paired portals are walls for her |
| `BC_DEADPORTAL` | `1` | runs | ablation.hpp:31 | world.hpp:405 | for every dragon, a portal into a known 1-tile room is a wall |
| `BC_QUEEN_KING` | `0` | off | ablation.hpp:11 |  | the queen is the king from round 0 |
| `BC_QUEEN_SPLIT` | `0` | off | ablation.hpp:12 |  | no economic split for the queen but an opening bud |
| `BC_QUEEN_SAFE` | `0` | off | ablation.hpp:13 |  | an older queen safety model inside the search (replaced by the kernel) |
| `BC_QUEEN_ZONE` | `0` | off | ablation.hpp:14 |  | the others keep clear of her head and leave her food from round 0 |
| `BC_QUEEN_FEED` | `0` | off | ablation.hpp:15 |  | all feeding and the LONGEST facts go to the queen |
| `BC_QUEEN_FLEE` | `1` | shadowed | common.hpp:485 | policy.hpp:1033 | a 2-3 step flee; its call site needs the kernel off and `BC_QUEEN_SAFE` on |
| `BC_QUEEN_GROW` | `0` | off | ablation.hpp:17 |  | growth arm: teammates leave her pearls, she eats x2.5 / x1.5, no split below 10 |
| `BC_QUEEN_GROWUP` | `0` | off | common.hpp:330 |  | grow her after a boxed opening (trigger at round 100) |
| `BC_GROWUP_FEED` | `0` | off | common.hpp:337 |  | with GROWUP: short teammates feed her by sacrifice |
| `BC_QUEEN_TINY` | `0` | off | common.hpp:481 |  | she stays 2 long and buds the rest |
| `BC_QUEEN_FEED350` | `0` | off | common.hpp:402 |  | feeding the queen from round 350 (yield, eat-first, food pull) |
| `BC_QUEEN_HOME` | `0` | off | common.hpp:432 |  | inside a tier, prefer our side's interior |
| `BC_QUEEN_ESCORT` | `0` | off | common.hpp:436 |  | the nearest 1-2 teammates shield her from the nearest enemy head |
| `BC_QUEEN_MODES` | `0` | off | common.hpp:444 |  | LOCK (their queen dead) / DESPERATE (ours dead) team modes |
| `BC_QUEEN_LOCKALL` | `1` | off | common.hpp:447 |  | LOCK mode's no-trade scope; needs `BC_QUEEN_MODES` |
| `BC_QUEEN_KINAVOID` | `0` | off | common.hpp:459 |  | our dragons never step next to our queen's head |
| `BC_QUEEN_SLACK` | `0` | off | common.hpp:455 |  | a hard free-reach slack filter on her landings |
| `BC_QUEEN_YIELD` | `0` | off | common.hpp:367 |  | teammates next to a boxed queen step away first |
| `BC_QSK_PATHHEAT` | `0` | off | common.hpp:349 |  | the kernel's "enemies about" by path steps instead of Chebyshev distance |

## Economy and expansion

([economy-and-expansion.md](economy-and-expansion.md))

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_ECON_B0` | `1` | runs | common.hpp:462 | main.cpp:85 | the queen's multi-step moves come from the kernel only |
| `BC_ECON_DIP` | `1` | runs | ablation.hpp:19 | policy.hpp:2035 | lane dips: a short dragon farms a dead-end lane and leaves by a reverse split |
| `BC_ECON_BED` | `1` | runs | ablation.hpp:20 | policy.hpp:3546 | the bed spawn clock before round 300 |
| `BC_ECON_CROWN` | `0` | unused | common.hpp:478 |  | defined, but no code reads it in this build |
| `BC_ECON_TRACE` | `0` | off | common.hpp:466 |  | trace tags in debug log lines |
| `BC_HARVEST` | `1` | runs (1) | ablation.hpp:37 | policy.hpp:1848 | room harvest behind paired portals (mode 1: the late split) |
| `BC_LONGEST_RESERVE` | `3` | runs (3) | ablation.hpp:38 | world.hpp:262 | from round 300 the longest non-queen keeps every segment and eats first; two feeders |
| `BC_UNIT_SLOT` | `1` | runs | ablation.hpp:39 | policy.hpp:222 | no dragon but the queen splits at 63+ units |
| `BC_FORAGE_PATH` | `1` | inert | ablation.hpp:40 | policy.hpp:3038 | path distance for heard teammates; its result is multiplied by `remoteYield` 1.0 |
| `BC_OPEN_EXPAND` | `1` | runs | ablation.hpp:287 | policy.hpp:2891 | opening expansion toward free bed areas until round 120 |
| `BC_OPEN_EXPAND_W` | `0.4` | knob | common.hpp:1007 | policy.hpp:3616 | the expansion pull per step (0.4) |
| `BC_OPEN_EXPAND_GATE` | `28` | runs (28) | ablation.hpp:289 | policy.hpp:2858 | bits 4, 8 and 16: in effect every map of at most 700 tiles (W x H) |
| `BC_FREESPRINT` | `0` | off | common.hpp:451 |  | free steps toward the nearest food for every step (superseded by the R4 free steps) |
| `BC_SPRINT_TARGET` | `0` | off | common.hpp:396 |  | free steps only toward contested fresh pearls |
| `BC_BEDRAID` | `0` | off | common.hpp:241 |  | opening food probes through unpaired portals |
| `BC_EXPAND` | `0` | off | common.hpp:248 |  | idle dragons contest bed cores none of ours holds |
| `BC_EXPAND_OPENING` | `0` | off | common.hpp:254 |  | a few-unit opening mode (queen mother, mirror targets, anti-hunt); its sub-switches `BC_EO_*` are undefined |
| `BC_BED_KEEP` | `0` | off | common.hpp:285 |  | keep a fast bed (hold it, arrive on time); its gap knob `BC_KEEP_GAP` is undefined |
| `BC_KEEP_FREE` | `true` | runs | policy.hpp:3407 |  | derived from `BC_BED_KEEP` 0: the far pulls are always allowed |
| `BC_TIER2` | `0` | off | common.hpp:300 |  | with BED_KEEP: keep a second-tier bed when the fast ones are full |
| `BC_OPEN_RISK` | `0` | off | common.hpp:307 |  | opening risk appetite: no yielding, no waiting, fast-bed parking |
| `BC_TRAPPOCKET` | `0` | off | common.hpp:341 |  | a trap pocket (one open side, portals into dead rooms) is a wall |

## Map network

([mapnet.md](mapnet.md)). The `BC_MN_*` macros without a value of their own are derived from the `BC_MAPNET_*` switches.

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_MAPNET` | `1` | runs | ablation.hpp:34 | world.hpp:595 | the shared map network: gate table, facts, danger layer, hooks |
| `BC_MAPNET_FACTS` | `1` | runs | common.hpp:155 | world.hpp:690 | GATE / REGION facts, probe once per team, inferred traps |
| `BC_MAPNET_REGION` | `1` | runs | common.hpp:158 | policy.hpp:4088 | the queen's room rules: no split in a small closed room, bed rooms allowed through her gate |
| `BC_MAPNET_EXPLORE` | `1` | runs | common.hpp:161 | policy.hpp:3932 | the danger layer in step costs and exploration |
| `BC_MAPNET_TRAPSET` | `1` | runs | common.hpp:164 | world.hpp:483 | the TRAPSET mask of trapped portal ids |
| `BC_MAPNET_EVIDENCE` | `2` | runs (2) | ablation.hpp:42 | world.hpp:511 | evidence grading of inferred traps; level 2 waits 12 rounds |
| `BC_MAPNET_VALUE` | `1` | runs | ablation.hpp:41 | policy.hpp:2801 | VALUE areas: shared bed areas and the pull toward unheld ones |
| `BC_MAPNET_ROOMS` | `0` | off | common.hpp:196 |  | the incremental room graph (no decision reads it) |
| `BC_MN_TRAP_INFER` | `1` | runs | common.hpp:167 | region.hpp:470 | an unanswered PROBING may become a TRAP (else it only lapses) |
| `BC_MN_TRAPSET_SONAR` | `1` | runs | common.hpp:172 | main.cpp:178 | the trap set forces sonar in the quiet opening |
| `BC_MN_DANGER_W` | `0.6` | knob | common.hpp:176 |  | cost of a step onto a tile of danger 1 (0.6) |
| `BC_MN_EXPLORE_TH` | `0.3` | knob | common.hpp:179 |  | danger above which an unseen tile has no exploration value (0.3) |
| `BC_MN_VALUE_W` | `0.15` | knob | common.hpp:1063 |  | the VALUE pull's weight (0.15) |
| `BC_MN_FACTS` | `(BC_MAPNET && BC_MAPNET_FACTS)` | derived | common.hpp:182 |  | = `BC_MAPNET && BC_MAPNET_FACTS` |
| `BC_MN_REGION` | `(BC_MAPNET && BC_MAPNET_REGION)` | derived | common.hpp:193 |  | = `BC_MAPNET && BC_MAPNET_REGION` |
| `BC_MN_EXPLORE` | `(BC_MAPNET && BC_MAPNET_EXPLORE)` | derived | common.hpp:194 |  | = `BC_MAPNET && BC_MAPNET_EXPLORE` |
| `BC_MN_TRAPSET` | `(BC_MN_FACTS && BC_MAPNET_TRAPSET)` | derived | common.hpp:192 |  | = `BC_MN_FACTS && BC_MAPNET_TRAPSET` |
| `BC_MN_EVIDENCE` | `(BC_MN_FACTS && BC_MAPNET_EVIDENCE)` | derived | common.hpp:191 |  | = `BC_MN_FACTS && BC_MAPNET_EVIDENCE` |
| `BC_MN_VALUE` | `(BC_MN_FACTS && BC_MAPNET_VALUE)` | derived | common.hpp:207 |  | = `BC_MN_FACTS && BC_MAPNET_VALUE` |
| `BC_MN_ROOMS` | `(BC_MAPNET && BC_MAPNET_ROOMS)` | derived | common.hpp:199 |  | = `BC_MAPNET && BC_MAPNET_ROOMS` (0) |

## Network priors

([net-priors.md](net-priors.md))

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_R4NET` | `1` | runs | ablation.hpp:259 | policy.hpp:419 | the R4 net for every non-queen dragon |
| `BC_R4_W` | `0.02` | knob | ablation.hpp:283 | policy.hpp:813 | weight of the R4 root prior (0.02) |
| `BC_R4_WEFF` | `BC_R4_W` | derived | policy.hpp:23 |  | = `BC_R4_W` (a class net would change it; `BC_R6` is 0) |
| `BC_R4_WEFF_NOW` | `BC_R4_WEFF` | derived | policy.hpp:28 |  | = `BC_R4_WEFF` (`BC_T64_FIELD` is 0) |
| `BC_R4_FROM` | `0` | knob | ablation.hpp:60 |  | first round of the R4 prior and free steps (0) |
| `BC_R4_UNTIL` | `500` | knob | ablation.hpp:263 |  | last round + 1 of the R4 prior and free steps (500: the whole game) |
| `BC_R4_SPLIT_ON` | `1` | runs | ablation.hpp:265 | policy.hpp:426 | the R4 net gives the split trigger of non-queen dragons before round 200 (redundant with the swarm) |
| `BC_R4_SPLITP` | `0.4` | knob | ablation.hpp:267 |  | split trigger threshold (0.4) |
| `BC_R4_SPRINT` | `6` | runs (6) | ablation.hpp:269 | policy.hpp:2415 | bit 2: free steps along the net's second-step head; bit 4: also after eating / pocket exit / expansion / idle-bed / finish steps; bits 1 and 8 off |
| `BC_R4_SPRINTP` | `0.3` | knob | ablation.hpp:280 |  | free-step gate: P(multi-step given the first step) >= 0.3 |
| `BC_R4_MAXSTEPS` | `2` | knob | ablation.hpp:90 |  | at most 2 steps in a free-step move |
| `BC_R4_QRING` | `2` | knob | ablation.hpp:114 |  | no free-step cell within Chebyshev 2 of our queen's head |
| `BC_R4_LAND` | `1` | runs | ablation.hpp:278 | policy.hpp:2393 | the cheap landing rule (free reach + 1 of every known enemy head) |
| `BC_R5_LANDM` | `1` | knob | ablation.hpp:211 |  | margin of the landing rule (1) |
| `BC_R5_ROOM` | `2` | knob | ablation.hpp:214 |  | room the landing must keep beyond L + pearls eaten (2) |
| `BC_R5_SWARM_MINLEN` | `4` | knob | ablation.hpp:129 |  | the swarm's minimum length (4, the default) |
| `BC_R5_SWARM_OFF` | (empty) | derived | ablation.hpp:140 |  | empty (`BC_R5_NETSPLIT` 0): the swarm is not disabled |
| `BC_R5_GATEOK` | `((p) >= BC_R4_SPRINTP)` | derived | policy.hpp:33 |  | the free-step gate is `P >= BC_R4_SPRINTP` (`BC_R5_GATE` 0) |
| `BC_R4_SLACK` | `0.003` | off | ablation.hpp:117 |  | clock guard of the old threat-map landing check (compiled out with `BC_R4_LAND` 1) |
| `BC_R4_CONF` | `0.0` | off | ablation.hpp:66 |  | apply the prior only where the net is confident (0: always) |
| `BC_R4_BAND` | `0.0` | off | ablation.hpp:69 |  | apply the prior only among near-best moves (0: all moves) |
| `BC_R4_CALM` | `0` | off | ablation.hpp:78 |  | apply the prior only with no enemy in view |
| `BC_R4_NOISE` | `0` | off | ablation.hpp:87 |  | a pseudo-random prior instead of the net (control arm) |
| `BC_R4_CULL` | `0` | off | ablation.hpp:93 |  | a short dragon next to our queen sacrifices itself where the net predicts no action |
| `BC_R4_CULLP` | `0.5` | off | ablation.hpp:96 |  | `BC_R4_CULL`'s threshold |
| `BC_R4_CULLFROM` | `300` | off | ablation.hpp:99 |  | `BC_R4_CULL`'s first round |
| `BC_R4_CULLMAXLEN` | `4` | off | ablation.hpp:102 |  | `BC_R4_CULL`'s length limit |
| `BC_R4_CULLUNITS` | `8` | off | ablation.hpp:105 |  | `BC_R4_CULL`'s unit floor |
| `BC_R4_CULLBACKLOG` | `2` | off | ablation.hpp:108 |  | `BC_R4_CULL`'s drop backlog limit |
| `BC_R4_CULLNEAR` | `3` | off | ablation.hpp:111 |  | `BC_R4_CULL`'s distance to our queen |
| `BC_R5_NETSPLIT` | `0` | off | ablation.hpp:132 |  | the net's trigger as the only economic split rule |
| `BC_R5_RLUNTIL` | `0` | off | ablation.hpp:135 |  | a different window for the net's split trigger (0: `rlUntil` 200) |
| `BC_R5_KINDS` | `0` | off | ablation.hpp:149 |  | free steps also after a squad's slay step |
| `BC_R5_GATE` | `0` | off | ablation.hpp:205 |  | a stochastic free-step gate |
| `BC_R5_KAPPA` | `1.0` | off | ablation.hpp:208 |  | `BC_R5_GATE`'s scale |
| `BC_R6` | `0` | off | ablation.hpp:224 |  | per-map-class nets; its headers are not shipped, so it cannot be turned on in this release |
| `BC_R6_FROM` | `20` | off | ablation.hpp:227 |  | `BC_R6` selector timing |
| `BC_R6_EVERY` | `5` | off | ablation.hpp:230 |  | `BC_R6` selector timing |
| `BC_R6_UNTIL` | `140` | off | ablation.hpp:233 |  | `BC_R6` selector timing |
| `BC_R6_MINSEEN` | `60` | off | ablation.hpp:236 |  | `BC_R6` selector: tiles seen before judging |
| `BC_R6_TAUSCALE` | `1.0` | off | ablation.hpp:239 |  | `BC_R6` selector acceptance radius |
| `BC_R6_STICKY` | `1.5` | off | ablation.hpp:242 |  | `BC_R6` selector hysteresis |
| `BC_R6_W` | `-1` | off | ablation.hpp:245 |  | `BC_R6` prior weight |
| `BC_R6_MASK` | `3` | off | ablation.hpp:248 |  | `BC_R6` class mask |
| `BC_R6_CONFIRM` | `2` | off | ablation.hpp:251 |  | `BC_R6` class confirmation count |
| `BC_R6_BUILD_SLACK` | `0.015` | off | ablation.hpp:254 |  | `BC_R6` clock slack for building a class net |
| `BC_R7_TOPB_ON` | `0` | off | ablation.hpp:159 |  | a fixed bonus for the net's top move |
| `BC_R7_TOPB` | `0.1` | off | ablation.hpp:162 |  | that bonus |
| `BC_R7_KINDW_ON` | `0` | off | ablation.hpp:165 |  | prior weight by the kind of the search's best step |
| `BC_R7_KW0` | `1.0` | off | ablation.hpp:168 |  | weight for positioning steps |
| `BC_R7_KW1` | `1.0` | off | ablation.hpp:171 |  | weight for tactical steps |
| `BC_R7_KW2` | `1.0` | off | ablation.hpp:174 |  | weight for travel / team / queen steps |
| `BC_T64_FIELD` | `0` | off | ablation.hpp:184 |  | a larger prior weight near fast beds |
| `BC_T64_W` | `0.3` | off | ablation.hpp:187 |  | `BC_T64_FIELD`'s weight |
| `BC_T64_R` | `3` | off | ablation.hpp:190 |  | `BC_T64_FIELD`'s radius |
| `BC_T64_GAP` | `20` | off | ablation.hpp:193 |  | `BC_T64_FIELD`'s fast-bed gap |
| `BC_T64_FASTEST` | `1` | off | ablation.hpp:196 |  | `BC_T64_FIELD`: fastest class only |
| `BC_T64_TOL` | `1.5` | off | ablation.hpp:199 |  | `BC_T64_FIELD`'s tolerance |
| `BC_T64_EATB` | `0.0` | off | ablation.hpp:202 |  | `BC_T64_FIELD`'s eat bonus |

## CPU safety and trace builds

([cpu-safety.md](cpu-safety.md))

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `BC_FALLBACK95` | `1` | runs | ablation.hpp:47 | common.hpp:1234 | the 95 M-point hard line and its check points |
| `BC_T13_BURN` | `0` | off | common.hpp:1231 |  | test builds only: burn the clock past the line at one check point a round |
| `BC_V11_TRACE` | `0` | off | common.hpp:1228 |  | trace builds: fallback markers and room-graph statistics in the log |

## King library (`v10k.hpp`, `king/`)

Compiled, never runs: every call site requires `round >= V10K_R0`, and `V10K_R0` is 501 in a 500-round game. The other
`V10K_*` values are parameters of that library.

| switch | value | status | defined at | main code | what it does |
|---|---|---|---|---|---|
| `V10K_ON` | `1` | off | v10k.hpp:30 | main.cpp:54 | the adapter's master switch (1), gated by `V10K_R0` |
| `V10K_R0` | `501` | knob | v10k.hpp:37 |  | first round the library may run: 501, i.e. never |

Other library parameters (all inert): `V10K_ACTIVATE` `1`, `V10K_AGE_TO_CLAIM` `20`, `V10K_ARM` `1`,
`V10K_ARMY_CONVERGE_NOSPLIT` `1`, `V10K_CPU_GUARD` `0.090`, `V10K_D5P_DIST` `3`, `V10K_D5P_DIST_SMALL` `V10K_D5P_DIST`,
`V10K_D5P_SMALL_TILES` `2000`, `V10K_EXEC_AGE` `3`, `V10K_EXEC_THROTTLE` `1`, `V10K_FEED` `1`, `V10K_FEED_FORAGE` `0.4`,
`V10K_FEED_FROM` `300`, `V10K_GROW_UHOLD` `0`, `V10K_HANDOVER` `1`, `V10K_KING_DANGER` `3.0`, `V10K_KING_REACH` `5`,
`V10K_KING_RULES` `1`, `V10K_KING_VETO` `0`, `V10K_LATE_ROUND` `260`, `V10K_LIB_RAYS` `3`, `V10K_MINLEN` `6`,
`V10K_MINLEN_LATE` `4`, `V10K_MIN_TILES` `1100`, `V10K_PATIENCE` `20`, `V10K_PLAUSIBLE` `1`, `V10K_QUARANTINE` `10`,
`V10K_REBUILD_KEEP` `1`, `V10K_RECENT` `10`, `V10K_REFRESH` `3`, `V10K_RESERVE` `0.002`, `V10K_SEED` `-1`,
`V10K_SELF_KING` `1`, `V10K_SONAR` `1`, `V10K_STUB_RELAY` `5`, `V10K_TRACE` `0`, `V10K_UE_HEARD` `0`,
`V10K_V10FEED_FALLBACK` `1`, `V10K_YIELD` `1`.

## Changing a switch

Any change of a value changes the WASM, and with it the games: the search stops on an instruction-metered clock, so even
a module that never triggers moves the point where the search ends. To try a variant:

1. copy `bots/abyss` to a new, uniquely named folder (the sandbox caches the WASM by folder name and `.cpp` contents, so
   an edited header in a reused folder can run a stale build);
2. change the value in the copy's `ablation.hpp` (a `-D` flag in a native test build works only for a switch whose
   definition is wrapped in `#ifndef`; for the others the compiler warns about the redefinition and the value in the
   source wins);
3. judge it against a renamed copy of the unchanged bot on the same paired seeds, by difference, with enough games (see
   [../methodology.md](../methodology.md)), and re-run the CPU census ([cpu-safety.md](cpu-safety.md)).

A switch that should be inert in some configuration is verified by an identical WASM hash or by event-identical replays
on fixed seeds, never by win rates.
