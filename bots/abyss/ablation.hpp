#pragma once
// Module switches (ablation table) of the final build. Every behaviour module of the bot is guarded by a BC_* macro so
// that it can be switched off for an ablation run (e.g. -DBC_QGATE=0 where the default is wrapped in #ifndef). The
// defaults below are the configuration that was submitted; common.hpp holds the remaining switches (on by default unless
// stated there). The table is layered: the first block fixes the queen and economy modules, the BC_R4* / BC_R5* / BC_R7*
// / BC_T64* / BC_R6* blocks declare the move-prior net and its optional variants (all off by default), and the #undef
// overrides at the end set the values used by the final build. With a switch at 0 the guarded code is not compiled, so
// the binary is the same as a build without that module.
// Explicitly off: the early queen modules (king / split / safe / zone / feed / grow). BC_QUEEN_QKILL is turned on by the
// overrides at the end.
#define BC_QUEEN_KING 0
#define BC_QUEEN_SPLIT 0
#define BC_QUEEN_SAFE 0
#define BC_QUEEN_ZONE 0
#define BC_QUEEN_FEED 0
#define BC_QUEEN_QKILL 0
#define BC_QUEEN_GROW 0
#define BC_QUEEN_ROOMGUARD 1
#define BC_ECON_DIP 1
#define BC_ECON_BED 1
#ifndef BC_QUEEN_EXITFIX
#define BC_QUEEN_EXITFIX 1
#endif
#ifndef BC_QUEEN_NODEAD
#define BC_QUEEN_NODEAD 1
#endif
#ifndef BC_QGATE
#define BC_QGATE 1
#endif
#ifndef BC_DEADPORTAL
#define BC_DEADPORTAL 1
#endif
#ifndef BC_MAPNET
#define BC_MAPNET 1
#endif
#define BC_QUEEN_BREATHE 2  // the queen keeps 2 free sides; mode 2 adds the bubble: a teammate step into the 8 tiles around her head costs kBreatheW
#define BC_HARVEST 1  // an idle short non-queen harvests a closed 2-8 tile bed room behind a paired portal: eats inside, splits the rear out
#define BC_LONGEST_RESERVE 3  // from kReserveFrom the team's longest non-queen dragon keeps every segment and eats first; the two nearest teammates leave it the food
#define BC_UNIT_SLOT 1  // a non-queen never takes the last free unit slot (it is the queen's)
#define BC_FORAGE_PATH 1  // the remote-yield distance of a teammate heard out of sight is path steps over the known map, not torus Manhattan
#define BC_MAPNET_VALUE 1  // map network VALUE facts: a dragon that knows 2+ beds in a heat sector tells the team once
#define BC_MAPNET_EVIDENCE 2  // map network evidence grading: an unanswered portal probe becomes a TRAP only on evidence; 2 = judged after 12 rounds
#define BC_QUEEN_GROWFIRST 1  // before r200, while the queen is shorter than 5, she eats first (higher eat weight, QSK food preferences)
#define BC_QUEEN_KEEPAWAY 1  // among the landings QSK keeps, the queen takes the one farther by path from the nearest known enemy head
#define BC_QUEEN_BOXGUARD 1  // the queen's last free side is never taken by a teammate, not even as its trapped fallback (it dies alone instead)
#ifndef BC_FALLBACK95
#define BC_FALLBACK95 1    // hard 95M-point CPU fallback: past the line every module falls back to the cheapest legal move
#endif

// R4 move prior (r4net.hpp + r4_weights.hpp): a small policy network trained by imitation on aggregated public ladder
// replays of top-ranked teams; it gives the move prior of every dragon but the queen (the queen keeps the opening prior
// of rl_net.hpp). Declared off here with all its options; the overrides at the end of this file switch it on.
#ifndef BC_R4NET
#define BC_R4NET 0
#endif
#ifndef BC_R4_W
#define BC_R4_W 0.05        // weight of log p(first step | move) on the search's root values
#endif
#ifndef BC_R4_FROM
#define BC_R4_FROM 0
#endif
#ifndef BC_R4_UNTIL
#define BC_R4_UNTIL 500     // last round the prior is used (the opening prior of rl_net.hpp stops at p_.rlUntil = 200)
#endif
#ifndef BC_R4_CONF
#define BC_R4_CONF 0.0      // apply the prior only where the net's top legal move holds this share of the legal mass
#endif
#ifndef BC_R4_BAND
#define BC_R4_BAND 0.0      // > 0: only among moves within this band of the best search value
#endif
#ifndef BC_R4_SPLIT_ON
#define BC_R4_SPLIT_ON 0    // 1: the split trigger (rounds < rlUntil) comes from the net's split probability >= BC_R4_SPLITP; the opening net is not built for non-queens
#endif
#ifndef BC_R4_SPLITP
#define BC_R4_SPLITP 0.1
#endif
#ifndef BC_R4_CALM
#define BC_R4_CALM 0        // 1: the move prior only on turns with no enemy part in the dragon's view (the net does not even run in a fight)
#endif
#ifndef BC_R4_SPRINT
#define BC_R4_SPRINT 0      // bit 1: the search core's freeSprint (free steps along the shortest way to the nearest fresh pearl / due bed), bit 2: the free steps along the net's second-step head, wherever the net's P(multi-step | first step) >= BC_R4_SPRINTP, bit 4: for more kinds of step (eating, pocket exit, expand, bed wait, finish)
#endif
#ifndef BC_R4_SPRINTP
#define BC_R4_SPRINTP 0.5
#endif
#ifndef BC_R4_NOISE
#define BC_R4_NOISE 0       // 1: control setting - a pseudo-random move prior instead of the net (use with BC_R4_SPLIT_ON 0, BC_R4_SPRINT 0); off in the final build
#endif
#ifndef BC_R4_MAXSTEPS
#define BC_R4_MAXSTEPS 2    // the most steps of a net-gated free-step move (bit 2 of BC_R4_SPRINT); the free steps are ceil(L / 4)
#endif
#ifndef BC_R4_CULL
#define BC_R4_CULL 0        // 1: a non-queen of length <= BC_R4_CULLMAXLEN next to our queen sacrifices itself (feeds her) where the net's P(no action) >= BC_R4_CULLP, from round BC_R4_CULLFROM; off in the final build
#endif
#ifndef BC_R4_CULLP
#define BC_R4_CULLP 0.5
#endif
#ifndef BC_R4_CULLFROM
#define BC_R4_CULLFROM 300
#endif
#ifndef BC_R4_CULLMAXLEN
#define BC_R4_CULLMAXLEN 4
#endif
#ifndef BC_R4_CULLUNITS
#define BC_R4_CULLUNITS 8   // never below this many units
#endif
#ifndef BC_R4_CULLBACKLOG
#define BC_R4_CULLBACKLOG 2 // pearls in view within 3 of her head
#endif
#ifndef BC_R4_CULLNEAR
#define BC_R4_CULLNEAR 3    // the queen's head within this Chebyshev distance of ours
#endif
#ifndef BC_R4_QRING
#define BC_R4_QRING 2       // net-gated free steps: no cell of the path within this Chebyshev distance of our queen's head (when she is in view)
#endif
#ifndef BC_R4_SLACK
#define BC_R4_SLACK 0.003   // net-gated free steps: with an enemy known, QSK's threat map (<= ~10M points) runs only while the turn clock is at least this many seconds (3M points) short of the search deadline
#endif
// Optional free-step and split variants (off by default; the code is under #if, or a macro that expands to the plain token):
//   BC_R4_LAND 1          a non-queen's free-step landing is checked by the cheap free-reach rule (r4LandOk) instead of QSK's threat map and the clock guard (on in the final build)
//   BC_R4_SPRINT bit 8    (value 8 added: 6 -> 14) the travel kinds (to-feed, to-feed-heard, to-probe, cramped) may take the free steps too (off)
//   BC_R5_SWARM_MINLEN n  the shortest dragon the swarm (and, above 4, the plain split) halves; default 4
//   BC_R5_NETSPLIT 1      no swarm and no plain economic split: only the net's trigger (BC_R4_SPLITP, to round BC_R5_RLUNTIL), the rebuild / pocket guard / grower rules (off)
//   BC_R5_RLUNTIL n       the net's split trigger window in rounds (0: p_.rlUntil = 200)
#ifndef BC_R4_LAND
#define BC_R4_LAND 0
#endif
#ifndef BC_R5_SWARM_MINLEN
#define BC_R5_SWARM_MINLEN 4
#endif
#ifndef BC_R5_NETSPLIT
#define BC_R5_NETSPLIT 0
#endif
#ifndef BC_R5_RLUNTIL
#define BC_R5_RLUNTIL 0
#endif
#if BC_R5_NETSPLIT
#define BC_R5_SWARM_OFF false &&
#else
#define BC_R5_SWARM_OFF
#endif
// Free-step refusal classes (off / neutral by default): one switch per class of free-step refusal of r4Sprint, each a
// macro that expands to the default token, for non-queen dragons only (r4Sprint refuses the queen first):
//   BC_R5_LANDM n   r4LandOk (BC_R4_LAND 1): a landing must lie farther than a known enemy head's free reach + n (default 1; 0 admits a landing one cell nearer; refusal code 7)
//   BC_R5_ROOM n    r4Sprint: the landing must keep a room of L + eaten + n tiles (default 2; refusal code 8)
//   BC_R5_KINDS m   bit 1: r4Sprint also takes the free steps after a "slay" first step (the squad's step toward a long enemy; refusal code 3). The travel kinds (to-feed, to-probe, cramped) are the existing
//                   BC_R4_SPRINT bit 8 (value 14). Default 0.
#ifndef BC_R5_KINDS
#define BC_R5_KINDS 0
#endif
//   BC_R5_GATE 1    the free-step gate is stochastic: r4Sprint is tried with probability min(1, BC_R5_KAPPA x P) instead of P >= BC_R4_SPRINTP (P = the net's P(multi-step | the first step)); default 0
// Alternative readings of the move prior (off by default; everything is under #if). The prior adds BC_R4_W x log(p) to
// the search's root values; at the final build's W 0.02 it changes the step on ~7% of the prior turns and gives up 0.006
// of search value on average (a tie-breaker). Two other readings:
//   BC_R7_TOPB_ON 1, BC_R7_TOPB b   a fixed bonus b to the net's top legal first step (in addition to the log-prior): b 0.05 changes ~10% of the prior turns (mean value given up 0.015), b 0.1 ~14% (0.030, all <= 0.1)
//   BC_R7_KINDW_ON 1, BC_R7_KW0/1/2  the prior weight of a turn scales with the kind of the search's best step before the prior: class 0 positioning (forage, scout, disperse, detach, idle-bed, expand, leave-pocket, probe, eat,
//                                    harvest*, reverse), class 1 tactical (hunt, slay, finish, trade, sprint-trade, support, escort*, pursue-q, qkill, qguard*), class 2 travel / team / queen (the rest); 0 switches the prior off for that class
#ifndef BC_R7_TOPB_ON
#define BC_R7_TOPB_ON 0
#endif
#ifndef BC_R7_TOPB
#define BC_R7_TOPB 0.1
#endif
#ifndef BC_R7_KINDW_ON
#define BC_R7_KINDW_ON 0
#endif
#ifndef BC_R7_KW0
#define BC_R7_KW0 1.0
#endif
#ifndef BC_R7_KW1
#define BC_R7_KW1 1.0
#endif
#ifndef BC_R7_KW2
#define BC_R7_KW2 1.0
#endif
// Field mode (off by default, everything is under #if BC_T64_FIELD): a non-queen dragon in the FIELD of a fast bed plays
// the move prior at weight BC_T64_W instead of BC_R4_W (the prior adds W x log p to the search's root values; at the
// default 0.02 it is a tie-breaker). A bed is known-fast when the dragon's board has bed == 1 and 0 < gapMax <=
// BC_T64_GAP (learnGap sets it once hotObs countdowns were seen, none above hotGap; a fountain heard over sonar sets it
// too). The dragon is in the field when such a bed is within BC_T64_R steps along the walkable graph (walls count,
// portals too); BC_T64_FASTEST 1: only beds of the fastest class it knows count (gap <= BC_T64_TOL x the smallest known
// gap on its board), 0: any known-fast bed. BC_T64_EATB: in the field also a bonus on the root steps that eat.
#ifndef BC_T64_FIELD
#define BC_T64_FIELD 0
#endif
#ifndef BC_T64_W
#define BC_T64_W 0.3
#endif
#ifndef BC_T64_R
#define BC_T64_R 3
#endif
#ifndef BC_T64_GAP
#define BC_T64_GAP 20
#endif
#ifndef BC_T64_FASTEST
#define BC_T64_FASTEST 1
#endif
#ifndef BC_T64_TOL
#define BC_T64_TOL 1.5
#endif
#ifndef BC_T64_EATB
#define BC_T64_EATB 0.0
#endif
#ifndef BC_R5_GATE
#define BC_R5_GATE 0
#endif
#ifndef BC_R5_KAPPA
#define BC_R5_KAPPA 1.0
#endif
#ifndef BC_R5_LANDM
#define BC_R5_LANDM 1
#endif
#ifndef BC_R5_ROOM
#define BC_R5_ROOM 2
#endif
// Map-class nets (off; everything is under #if BC_R6). The headers this mode needs (a per-dragon map-class selector and
// two class-specific move-prior nets) are NOT shipped in this release, so BC_R6 must stay 0; the macros are kept only
// because removing them could change the build.
//   BC_R6 1               the selector runs and (with BC_ECON_TRACE) logs its class; the play is unchanged
//   BC_R6 2               the class A / B net replaces the default move-prior net of the dragon
//   BC_R6_FROM / EVERY / UNTIL / MINSEEN   judged from round FROM every EVERY rounds to round UNTIL, once the dragon has seen MINSEEN tiles
//   BC_R6_TAUSCALE x      the acceptance radius = the class's chi-square quantile x this; BC_R6_STICKY x: a class is kept until the squared distance exceeds the radius x this
#ifndef BC_R6
#define BC_R6 0
#endif
#ifndef BC_R6_FROM
#define BC_R6_FROM 20
#endif
#ifndef BC_R6_EVERY
#define BC_R6_EVERY 5
#endif
#ifndef BC_R6_UNTIL
#define BC_R6_UNTIL 140
#endif
#ifndef BC_R6_MINSEEN
#define BC_R6_MINSEEN 60
#endif
#ifndef BC_R6_TAUSCALE
#define BC_R6_TAUSCALE 1.0
#endif
#ifndef BC_R6_STICKY
#define BC_R6_STICKY 1.5
#endif
#ifndef BC_R6_W
#define BC_R6_W -1  // BC_R6 2: the root-prior weight while a class net plays (-1: BC_R4_W)
#endif
#ifndef BC_R6_MASK
#define BC_R6_MASK 3  // BC_R6 2: which class nets are used: bit 0 class A, bit 1 class B (1 = A only, 2 = B only, 3 = both)
#endif
#ifndef BC_R6_CONFIRM
#define BC_R6_CONFIRM 2  // a change of class (also from none to a class) needs this many judgements in a row with the same verdict
#endif
#ifndef BC_R6_BUILD_SLACK
#define BC_R6_BUILD_SLACK 0.015  // seconds of the turn clock a class net's first construction needs left (about 5M points of dequantising)
#endif
// Final-build overrides. The move-prior net on for non-queen dragons (split trigger from the net, free steps of bits 2
// and 4), plus the queen kill and queen hunt modules.
#undef BC_R4NET
#define BC_R4NET 1
#undef BC_R4_W
#define BC_R4_W 0.05
#undef BC_R4_UNTIL
#define BC_R4_UNTIL 500
#undef BC_R4_SPLIT_ON
#define BC_R4_SPLIT_ON 1
#undef BC_R4_SPLITP
#define BC_R4_SPLITP 0.4
#undef BC_R4_SPRINT
#define BC_R4_SPRINT 6
#undef BC_R4_SPRINTP
#define BC_R4_SPRINTP 0.5
#undef BC_QUEEN_QKILL
#define BC_QUEEN_QKILL 1
#undef BC_QUEEN_HUNT
#define BC_QUEEN_HUNT 1
// The cheaper landing rule for the net-gated free steps and the free-step threshold P >= 0.3.
#undef BC_R4_LAND
#define BC_R4_LAND 1
#undef BC_R4_SPRINTP
#define BC_R4_SPRINTP 0.3
// The move-prior net's root weight lowered to 0.02 (a tie-breaker; measured better than 0.05 on internal benchmarks).
#undef BC_R4_W
#define BC_R4_W 0.02
// Opening expansion (BC_OPEN_EXPAND) on, gate 28 = bits 4 | 8 | 16 (see common.hpp): maps with <= 2 starting dragons and
// <= 700 tiles, dragons born later on maps <= 700 tiles, any map <= 700 tiles.
#undef BC_OPEN_EXPAND
#define BC_OPEN_EXPAND 1
#undef BC_OPEN_EXPAND_GATE
#define BC_OPEN_EXPAND_GATE 28
