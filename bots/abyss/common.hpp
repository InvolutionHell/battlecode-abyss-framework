#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Optional build hooks for parameter tuning:
//   eval_budget.hpp: a smaller BC_TURN_BUDGET so tuning games run faster (absent in the final build).
//   tuned_params.hpp: applyTuned(Params&, group, phase), the tuned knobs per (map group, phase) (present).
#if __has_include("eval_budget.hpp")
#include "eval_budget.hpp"
#endif
// Component switches. ablation.hpp (included here) holds the final build's settings and overrides the defaults below;
// a component whose switch is 0 is compiled out or stays inactive.
#if __has_include("ablation.hpp")
#include "ablation.hpp"
#endif
#ifndef BC_MIRROR
#define BC_MIRROR 1            // map symmetry: mirrored beds / fountains / kelp, raid pull (World::setSymmetry)
#endif
#ifndef BC_FAR_EXPLORE
#define BC_FAR_EXPLORE 1       // long-range pull to the nearest unexplored frontier tile (wExploreFar)
#endif
#ifndef BC_EAT_NO_DISPERSE
#define BC_EAT_NO_DISPERSE 1   // the dispersal cost does not tax a step that eats
#endif
#ifndef BC_TRAPPED_PORTAL
#define BC_TRAPPED_PORTAL 1    // a doomed dragon steps into an unpaired portal before certain death
#endif
#ifndef BC_HEAT
#define BC_HEAT 1              // presence heat map (World::friendHeat / enemyHeat), spread + feeding guard
#endif
#ifndef BC_SCAV
#define BC_SCAV 1              // "scavenger": the nearest free dragons go for the pearls deaths drop
#endif
#ifndef BC_ENGAGE
#define BC_ENGAGE 1            // attack / hold / retreat from a trade-value model fitted on ladder replays
#endif
#ifndef BC_DEADEND
#define BC_DEADEND 1           // no short dragon into a tree-like dead end (Board::deadEnd)
#endif
#ifndef BC_KING
#define BC_KING 1              // end-game feeding goes to one king, medium dragons too while it is short
#endif
#ifndef BC_KINGGUARD
#define BC_KINGGUARD 0         // dragon plans, guard: our longest dragon stops splitting and keeps clear of enemy reach
#endif
#ifndef BC_SLAY
#define BC_SLAY 1              // dragon plans, slay: the nearest short dragons hunt the enemy's longest (World::enemyKing)
#endif
// Strategy module group (BC_V9_*). Each can be switched off in ablation.hpp.
#ifndef BC_V9_EKING
#define BC_V9_EKING 1          // early king: from ekFrom our longest (the same rule everywhere, never pre-selected) plays the guard
#endif
#ifndef BC_V9_DENSITY
#define BC_V9_DENSITY 1        // density cap: no voluntary split once we have map tiles / kDensity dragons
#endif
#ifndef BC_V9_TAIL
#define BC_V9_TAIL 1           // a dragon whose tail is not tracked yet may count on a reverse split (starters, fresh children)
#endif
#ifndef BC_V9_ESCAPE
#define BC_V9_ESCAPE 0         // a starving dragon probes unpaired portals whatever the unit count (confined start areas)
#endif
#ifndef BC_V9_SCOUT
#define BC_V9_SCOUT 1          // opening scouts: until scoutUntil each free dragon heads for its own unexplored sector
#endif
#ifndef BC_V9_KFEED
#define BC_V9_KFEED 0          // the basic feeding goes to the king only (LONGEST facts), throttled by the drops
                               // around its head
#endif
#ifndef BC_V9_FINISH
#define BC_V9_FINISH 1         // module F: in a big late army each free short dragon heads for the nearest enemy head it knows
#endif
#ifndef BC_V9_KING
#define BC_V9_KING 0           // module K: late king; feeders leave by their way to the king, sacrifice when its drops are eaten
#endif
#ifndef BC_COVER
#define BC_COVER 1             // the coverage knobs (team yield, remote yield, team size, rival fields) are tuned
#endif
#ifndef BC_SWARM
#define BC_SWARM 1             // the swarm / trade-less / small-risk / no-late-split knobs are tuned
#endif
#ifndef BC_FULL
#define BC_FULL 1              // full policy set: every component compiled in, each switched by a tuned knob
#endif
#ifndef BC_RL_PRIOR
#define BC_RL_PRIOR 1          // the opening prior network (rl_net.hpp) as a move / split prior, on when wPrior > 0
                               // or rlSplitP <= 1
#endif
#ifndef BC_PHASED
#define BC_PHASED 1            // every tuned knob per (map group, game phase): 12 contexts, ~1,900 dims
#endif
#ifndef BC_FINAL
#define BC_FINAL 1             // final push knobs in the search space (finalFrom 500: off)
#endif
#ifndef BC_FIGHT
#define BC_FIGHT 1             // the local fight search (fight.hpp), on when wFight > 0 (0 in every tuned context);
                               // no even swaps when fightTradeMin > -10
#endif
#ifndef BC_QUIET
#define BC_QUIET 1             // quiet opening: no sonar before sonarFrom
#endif
#ifndef BC_PORTAL2
#define BC_PORTAL2 1           // a closed, poor region is left through its unpaired portals (probes, any unit count)
#endif
#ifndef BC_ROOMCAP
#define BC_ROOMCAP 1           // room capacity: a closed room holds a few of ours, drawn alike by all; the rest leave by portal
#endif
#ifndef BC_QUEEN
#define BC_QUEEN 1             // queen rule (rules 1.2.3): the queen (unit id 0 or 1, one per team) decides round
                               // 500; she is the king from round 0, never trades, splits only in the opening
                               // exception; feeding targets her; new sprint reach
#endif
#ifndef BC_QUEEN_KING
#define BC_QUEEN_KING 1        // the queen is the king from r0 (kingMe_), nobody else (0: the older king rules)
#endif
#ifndef BC_QUEEN_SPLIT
#define BC_QUEEN_SPLIT 1       // no economic split for the queen but the opening bud (0: she splits like any dragon,
                               // front kept)
#endif
#ifndef BC_QUEEN_SAFE
#define BC_QUEEN_SAFE 1        // her safety model - rules-1.2.3 reach, tail tips, safe room, pocket, dead-end veto,
                               // no reverse plans
#endif
#ifndef BC_QUEEN_ZONE
#define BC_QUEEN_ZONE 1        // the others keep clear of her head and leave her food from r0 (0: the early king
                               // from ekFrom)
#endif
#ifndef BC_QUEEN_QSK
#define BC_QUEEN_QSK 1         // the Queen Safety Kernel (qsk.hpp) picks her landing; replaces the flee
#endif
#ifndef BC_QUEEN_QKILL
#define BC_QUEEN_QKILL 1       // non-queens take the enemy queen in reach (QKILL) and guard ours from a later striker
                               // (QGUARD)
#endif
#ifndef BC_QUEEN_GROW
#define BC_QUEEN_GROW 0        // growth arm: teammates leave the pearls near a short queen (< kGrowTo) to her, she
                               // eats at x2.5 (< 5) / x1.5 (< 9), and makes no economic split below kGrowSplitMin
#endif
#ifndef BC_QUEEN_ROOMGUARD
#define BC_QUEEN_ROOMGUARD 0   // friend discipline: a teammate's step within 2 of our queen that leaves her room < L
                               // + 2 costs kRoomGuardW
#endif
#ifndef BC_MAPNET
#define BC_MAPNET 0            // the shared map network (region.hpp): portal gate facts, room facts, a danger layer
                               // and their hooks
#endif
#ifndef BC_MAPNET_FACTS
#define BC_MAPNET_FACTS 1      // sub-switch: GATE / REGION facts (probe once per team, inferred traps)
#endif
#ifndef BC_MAPNET_REGION
#define BC_MAPNET_REGION 1     // sub-switch: the queen's room rules (no split in a small room, bed-room allowance)
#endif
#ifndef BC_MAPNET_EXPLORE
#define BC_MAPNET_EXPLORE 1    // sub-switch: the danger layer in path values and exploration
#endif
#ifndef BC_MAPNET_TRAPSET
#define BC_MAPNET_TRAPSET 1    // sub-switch: the TRAPSET mask (needs FACTS)
#endif
#ifndef BC_MN_TRAP_INFER
#define BC_MN_TRAP_INFER 1     // an unanswered PROBING becomes a TRAP (0: it just lapses). In a test most inferred
                               // TRAP gates on a portal map were food-room doors (false); the seen DEAD ones were all
                               // real traps
#endif
#ifndef BC_MN_TRAPSET_SONAR
#define BC_MN_TRAPSET_SONAR 1  // the trap set forces the sonar on in the quiet opening (news: every turn, else every
                               // 3rd)
#endif
#ifndef BC_MN_DANGER_W
#define BC_MN_DANGER_W 0.6     // knob: lambda, the cost of a step onto a tile of danger 1 (swept 0.3 / 0.6 / 1.0)
#endif
#ifndef BC_MN_EXPLORE_TH
#define BC_MN_EXPLORE_TH 0.3   // knob: danger above which an unseen tile has no exploration value (swept 0.2 / 0.3 /
                               // 0.5)
#endif
#define BC_MN_FACTS (BC_MAPNET && BC_MAPNET_FACTS)
#ifndef BC_MAPNET_EVIDENCE
#define BC_MAPNET_EVIDENCE 0   // fact evidence grading: a PROBING carries its prober's id; an unanswered one becomes
                               // a TRAP only when the prober was not heard or seen after it and the landing is no
                               // known bed room (else it lapses); an inferred TRAP is forgotten after 60 rounds
                               // (150); CLEARED gates landing in a bed room are relayed first (alternate turns). 2:
                               // the PROBING waits 12 rounds (6) before its TRAP is judged (the prober's facts have
                               // time to arrive)
#endif
#define BC_MN_EVIDENCE (BC_MN_FACTS && BC_MAPNET_EVIDENCE)
#define BC_MN_TRAPSET (BC_MN_FACTS && BC_MAPNET_TRAPSET)
#define BC_MN_REGION (BC_MAPNET && BC_MAPNET_REGION)
#define BC_MN_EXPLORE (BC_MAPNET && BC_MAPNET_EXPLORE)
#ifndef BC_MAPNET_ROOMS
#define BC_MAPNET_ROOMS 0      // sub-switch: region.hpp's rooms of the known map, recomputed in observe when the
                               // known tiles or the edges change; no decision reads them, so 0
#endif
#define BC_MN_ROOMS (BC_MAPNET && BC_MAPNET_ROOMS)
#ifndef BC_MAPNET_VALUE
#define BC_MAPNET_VALUE 0      // the value half of the map network - a dragon that knows 2+ beds in a heat sector
                               // tells the team once (a REGION fact flagged VALUE: an anchor bed, beds, held by us,
                               // round), every dragon keeps the sectors it knows and relays them (the gate relay
                               // slot, alternate turns), and a non-queen is pulled toward the best known bed area
                               // none of ours holds
#endif
#define BC_MN_VALUE (BC_MN_FACTS && BC_MAPNET_VALUE)
#ifndef BC_QGATE
#define BC_QGATE 0             // the queen never probes a portal, and a paired portal whose landing is unknown, has <
                               // 2 open edges or is a dead end is a wall in her adjacency (World::gatePortals; a
                               // queen was seen dying in a 1-tile room behind a portal)
#endif
#ifndef BC_DEADPORTAL
#define BC_DEADPORTAL 0        // for every dragon, a paired portal whose known landing has no open edge (a 1-tile
                               // room) is a wall
#endif
#ifndef BC_QUEEN_NODEAD
#define BC_QUEEN_NODEAD 0      // the queen's landings never enter a dead-end tile (Board::deadEnd) from an open one
                               // while another landing exists: a dead end is certain death for a 2-4 long queen (in
                               // testing nearly every such dip died within 10 rounds)
#endif
#ifndef BC_QUEEN_BREATHE
#define BC_QUEEN_BREATHE 0     // (replay reviews: many queen deaths came on her own move, boxed in, often with a
                               // teammate among the blockers): she keeps 2 free sides - QSK drops her landings with
                               // fewer while a safe one has 2, boxed 2 turns running she takes the most free sides; a
                               // teammate's step that leaves her < 2 free sides costs kBreatheW; 2: the bubble -
                               // every teammate step into the 8 tiles around her head costs kBreatheW; 3: the bubble,
                               // and her landing's free sides leave out the tiles next to a teammate's head (it may
                               // step there after her)
#endif
#ifndef BC_LONGEST_RESERVE
#define BC_LONGEST_RESERVE 0   // (replay reviews: many losses were decided by the longest dragon while our total
                               // length was larger; every dragon of 4+ splits in half until r400): from kReserveFrom
                               // the team's longest keeps every segment, eats first; 2: and the feeding phase
                               // (Params::feedRound: small dragons feed the longer one, no splits) starts at
                               // kReserveFrom; 3: mode 1, and the two teammates nearest to the reserve's head (no
                               // queen) leave it the pearls within kFeedYield of its head; no feeding phase (mode 2
                               // starved the army)
#endif
#ifndef BC_BEDRAID
#define BC_BEDRAID 0           // (on maps with portal-locked starts our 2 starting dragons never probe - under
                               // kSurvivalUnits, the start room open, BC_V9_ESCAPE off - and starve while strong
                               // teams cross in the first rounds): in the opening (before kRaidUntil) a non-queen
                               // dragon that has not eaten for kRaidStarve rounds probes unpaired portals whatever
                               // the units
#endif
#ifndef BC_EXPAND
#define BC_EXPAND 0            // (on some maps the spawns fall nearer to the enemy, and our dragons farmed the home
                               // room and never took the bed cores): an idle non-queen dragon outside a bed core
                               // heads for the nearest known bed (seen or mirrored, any refill gap, enemy-held too)
                               // that none of ours holds, else for the nearest unseen tile
#endif
#ifndef BC_EXPAND_OPENING
#define BC_EXPAND_OPENING 0    // (few starting units with the food behind doors: most losses on such maps were wipes
                               // after no eating by r60): a per-process mode for few-unit starts with no food in
                               // reach - risk appetite up until the army and the food are there
                               // (Policy::planOpenMode); the three parts below are its sub-switches, on with it
#endif
#if BC_EXPAND_OPENING
#ifndef BC_EO_QUEEN_MOTHER
#define BC_EO_QUEEN_MOTHER 1   // QUEEN_MOTHER: in the mode the queen's portal gate relaxes to a seen / mirrored
                               // landing with >= 2 open edges, she eats first (QSK's growth arm, eat x kEoQueenEat)
                               // and buds a 2-long explorer as soon as she is 4
#endif
#ifndef BC_EO_MIRROR_TARGET
#define BC_EO_MIRROR_TARGET 1  // MIRROR_TARGET: a non-queen in the mode heads for the reachable band with the most
                               // mirrored beds; none, or no symmetry yet: it probes the nearest unknown portal
                               // (BC_BEDRAID's path; CLAIM / PROBING / CLEARED as ever)
#endif
#ifndef BC_EO_ANTIHUNT
#define BC_EO_ANTIHUNT 1       // ANTIHUNT: in the mode with <= kEoHuntUnits units, a non-queen with an enemy of
                               // kEoHuntLen+ within kEoHuntR moves away from it (its BFS field), never into a dead
                               // end
#endif
#endif
#ifndef BC_OPEN_EXPAND
#define BC_OPEN_EXPAND 0       // opening expansion (replay study: on some maps the opening's spawns fall nearer to
                               // the enemy, mostly in a central bed block, while most of our heads held the start
                               // area): until kOpenExpandUntil a non-queen (not the escort: with 3+ of us known, the
                               // non-queen nearest our queen) heads for the known bed area with the most beds none of
                               // ours holds and away from the enemy's (its heads, the mirror of this process's first
                               // head); none known: for the map's centre while it is unseen (Policy::planOpenExpand)
#endif
#ifndef BC_BED_KEEP
#define BC_BED_KEEP 0          // (replay study: with both sides at a bed early at equal length, strong teams grew
                               // much faster through the opening, and on fountain maps our pearls per 100
                               // dragon-turns were about 0.7 of theirs): a non-queen with a fast bed (the largest
                               // countdown seen on it or its mirror, or its learned gap, at most kKeepGap) within
                               // kKeepSteps steps, before kKeepUntil and not feeding, keeps it while the bed's
                               // cluster (its fast beds within kKeepR) has room for it among the teammates in view
                               // nearer to it; bits: 1 hold - pulled by kWKeep to the bed, targets beyond kKeepR of
                               // it worth nothing, the far pulls (exploring, frontier, value room, heat,
                               // BC_OPEN_EXPAND / BC_EXPAND / BC_EXPAND_OPENING expansion, harvest, idle bed, scout)
                               // off; 2 on time - the kept bed pulls only when due, its countdown (seen, or its
                               // mirror's) at most its distance + kKeepSlack or a pearl on it; until then the dragon
                               // eats within kKeepR of it. (A spawn split by arrival was considered and not built:
                               // the nearer-teammate rule already orders by arrival.)
#endif
#ifndef BC_TIER2
#define BC_TIER2 0             // (replay study: outside the field we ate fewer pearls per 100 dragon-turns than
                               // strong teams, far fewer on maps where many slow beds surround a few fountains): with
                               // BC_BED_KEEP, when no bed of the first tier (BC_KEEP_GAP) has room for us within
                               // kKeepSteps (the field full or empty), the nearest known second-tier bed (gap at most
                               // kTier2Gap) within kTier2Steps with room is kept instead of foraging
#endif
#ifndef BC_OPEN_RISK
#define BC_OPEN_RISK 0         // (replay study: strong teams ate far more pearls by r100 on small crowded maps; they
                               // take the crowd's collisions for speed): before kOpenRiskUntil a non-queen of at most
                               // kOpenRiskMaxLen contests food like they do, bits: 1 no yielding to teammates (the
                               // nearer / heard teammate's pearl, the team's assignment, a fountain a teammate is
                               // on); 2 no waiting (the spawn clock's bed-wait value and idle-to-bed off: a bed
                               // counts from its spawn); 4 no teammate exit veto (a teammate's last way out is ours
                               // to take); 8 the fast beds (in replays early pearls on beds with a gap <= 100 spawn
                               // several times nearer to them, slow beds even; the countdown is visible, mirror beds
                               // share it): park at the known bed (seen or mirrored) of the best rate / distance
                               // whose largest countdown seen (its own or its mirror's) is at most kOpenRiskFastGap,
                               // no teammate in view nearer
#endif
#ifndef BC_OPEN_EXPAND_GATE
#define BC_OPEN_EXPAND_GATE 0  // the gate of BC_OPEN_EXPAND (ungated, the expansion lost on open maps): the opening
                               // expansion only in games that pass one of these (decided once a process, sticky),
                               // bits: 1 at its first turn from r10, at most kGateMaxBeds known beds within
                               // kGateBedSteps steps; 4 our starting dragons (UNIT_COUNT at r0) at most
                               // kGateMaxStarts and the map at most kGateMaxTiles tiles; 8 with bit 4, a dragon born
                               // later (it cannot know the start) on a map of at most kGateMaxTiles tiles; 16 a map
                               // of at most kGateMaxTiles tiles whatever the start. The final build uses 28 = 4 | 8 |
                               // 16
#endif
#ifndef BC_QUEEN_GROWUP
#define BC_QUEEN_GROWUP 0      // (replay study: strong teams' queens end the game far longer than ours - the swarm
                               // halved her at 4 until r400): 1: if by kGrowCheck >= kGrowShare of her turns had <= 1
                               // free side, from then on she makes no economic split and, until kGrowUntil and
                               // kGrowUpTo long, eats first (the growth arm) while teammates who heard her leave her
                               // food to her; 2: the same on every map (no trigger)
#endif
#ifndef BC_GROWUP_FEED
#define BC_GROWUP_FEED 0       // with BC_QUEEN_GROWUP: while she asks, a short teammate next to her dies there to
                               // feed her (its pearls land by her)
#endif
#ifndef BC_TRAPPOCKET
#define BC_TRAPPOCKET 0        // (from a trace): a trap pocket - a known tile whose one open side is its way in and
                               // whose portal sides all lead into known 1-tile rooms (or DEAD / TRAP gates) - is a
                               // wall for a dragon of 2+: it cannot turn round there, and its only step on is the
                               // trap (a dragon blocked at the junction by one of ours backed in, then died in the
                               // room). As a wall it also leaves the space checks, so the corridor to that junction
                               // reads tight while the junction's way on is blocked
#endif
#ifndef BC_QSK_PATHHEAT
#define BC_QSK_PATHHEAT 0      // QSK's "enemies about" by path - an enemy head seen or heard in the last 10 rounds
                               // within 8 steps on the known map (portals included), or her sector enemy-held - and
                               // portal mouths are reach-2 sources only then (or a paired mouth whose far side is
                               // enemy-held); fog likewise (in a repro, heads 6-7 away by Chebyshev but 13-20 by path
                               // kept the mouth zones on her)
#endif
#ifndef BC_UNIT_SLOT
#define BC_UNIT_SLOT 0         // a non-queen never splits at units >= the unit limit - 1, so the queen keeps a slot
                               // for her SPLIT-2 hold (every split site: the reverse, the swarm / economic split, the
                               // dip's reverse, BC_HARVEST's splits)
#endif
#ifndef BC_FORAGE_PATH
#define BC_FORAGE_PATH 0       // the distance from a teammate heard out of sight to a target (the remote-yield rule)
                               // is path steps over the known map (paired portals in, kForagePathCap at most;
                               // farther: no yield), not torus Manhattan - a teammate behind a wall no longer takes
                               // the pearls we would reach first
#endif
#ifndef BC_QUEEN_YIELD
#define BC_QUEEN_YIELD 0       // (local, no comms): a teammate whose head is within Chebyshev 1 of our queen's head
                               // while she has <= 2 free sides pays kYieldW for a landing within Chebyshev 1 of her
                               // and half of it at 2 (on top of BREATHE's bubble, which only prices the step in): it
                               // steps away first (its own dangers still count)
#endif
#ifndef BC_QUEEN_BOXGUARD
#define BC_QUEEN_BOXGUARD 0    // (from boxed-death timelines in replays: most were a teammate head stepping into her
                               // last exit - each time the blocker itself was trapped: its "shared-exit" fallback,
                               // the only legal move left), bits: 1 our queen's last free side (she in view with
                               // exactly one) is never taken, not even as that fallback: a trapped teammate dies
                               // alone instead (Policy::buildBase already bars it otherwise); 2 her one-exit landings
                               // whose exit has a teammate head next to it are dropped from QSK's tier 3 while
                               // another tier-3 landing is left (tiers 1-2 need 2 exits already)
#endif
#ifndef BC_QUEEN_KEEPAWAY
#define BC_QUEEN_KEEPAWAY 0    // (replay study: on some maps our queen sat about half as far from the nearest enemy
                               // head as strong teams' queens): among the landings QSK keeps (her best tier, slack,
                               // strict margin - the margin is capped, past it distance went unpriced), the one
                               // farther by path from the nearest known enemy head (in view, or heard within 5
                               // rounds) first
#endif
#ifndef BC_QUEEN_GROWFIRST
#define BC_QUEEN_GROWFIRST 0   // (replay study: on some maps a 2-3 long queen is traded one for one by short
                               // dragons): before r200 (kGrowFirstUntil), while our queen is shorter than 5
                               // (kGrowFirstLen), her eat weight x kGrowFirstEat and the QSK food preferences
                               // (BC_QFEED_EAT / PULL / SRC) - within the kept landings, one that eats first, then
                               // the one nearest food within 6 steps (in view or out of it)
#endif
#ifndef BC_SPRINT_TARGET
#define BC_SPRINT_TARGET 0     // (replay study: strong teams move several steps in 3.5-7% of their turns, we almost
                               // never), bits: 1 the free steps (freeSprint's walk and landing checks) toward a
                               // contested fresh pearl only - in view this round, an enemy head no more than
                               // kGrabSlack steps farther from it than we are - not a run to any food
#endif
#ifndef BC_QUEEN_FEED350
#define BC_QUEEN_FEED350 0     // from r350, while our queen is alive and shorter than 8, the two non-queens nearest
                               // to her (within 6 steps) leave her the pearls within 3 of her head (the reserve's
                               // yield, aimed at her; her place and length from sight, else from her fresh fact); no
                               // escort. 2: also her side - from r350 at L < 8 her eat weight x 2, and inside QSK's
                               // best tier a 1-step landing that eats comes first (never a worse tier); 3: then the
                               // landing nearest a pearl within 6 steps (a pull to the nearest safe food); 4: the
                               // pull's food also out of her view - teammates' HOT facts (<= 10 rounds), beds due
                               // unseen, due mirrored beds
#endif
#define BC_QFEED_EAT (BC_QUEEN_FEED350 >= 2 || BC_QUEEN_GROWFIRST)   // QSK: a landing that eats first (BC_QUEEN_FEED350 2
                                                                     // / BC_QUEEN_GROWFIRST)
#define BC_QFEED_PULL (BC_QUEEN_FEED350 >= 3 || BC_QUEEN_GROWFIRST)  // ... then the one nearest food (FEED350 3 /
                                                                     // GROWFIRST)
#define BC_QFEED_SRC (BC_QUEEN_FEED350 >= 4 || BC_QUEEN_GROWFIRST)   // ... food out of her view too (FEED350 4 /
                                                                     // GROWFIRST)
#ifndef BC_HARVEST
#define BC_HARVEST 0           // (from replay study: strong teams feed on small fountain rooms by entering,
                               // eating and splitting the rear out through the portal - the child free, the parent
                               // left): a closed room of 2-8 known tiles behind a paired portal with a bed refilling
                               // within kHarvestGap is harvested by an idle non-queen of 2-3 in the expand mode or
                               // under half the unit limit; inside it eats while it can and splits L - 2 - the child,
                               // born outside, taking the eaten mass - when the next step eats nothing (2: also in
                               // chunks, keeping a tail outside)
#endif
#ifndef BC_QUEEN_EXITFIX
#define BC_QUEEN_EXITFIX 0     // QSK tier T5 (no landing safe under the kernel's model) ranks the free landings by
                               // margins under lengths calibrated on ladder replays (a chain cut by her window is
                               // rarely >= 6 long). 1: classes safe under the mid / low lengths first; 2: always
#endif
#ifndef BC_QUEEN_HOME
#define BC_QUEEN_HOME 0        // inside a QSK tier the queen prefers our side's interior (far from enemy heads, near
                               // ours)
#endif
#ifndef BC_QUEEN_ESCORT
#define BC_QUEEN_ESCORT 0      // the 1-2 nearest non-queens within 5 put themselves between her and the nearest enemy
                               // head
#endif
#ifndef BC_QUEEN_HUNT
#define BC_QUEEN_HUNT 0        // enemy-queen (EQ) tracking / sharing (ENEMY facts with id < 2, tombs), emergent
                               // hunters, free-step pursuit
#endif
#ifndef BC_QUEEN_MODES
#define BC_QUEEN_MODES 0       // LOCK (their queen witnessed dead, ours alive) / DESPERATE (ours witnessed dead)
#endif
#ifndef BC_QUEEN_LOCKALL
#define BC_QUEEN_LOCKALL 1     // LOCK: no trades for everybody (0: only the queen and her escorts; the others trade
                               // as usual)
#endif
#ifndef BC_FREESPRINT
#define BC_FREESPRINT 0        // the search's step toward food continues along the shortest way on the free steps
                               // (ceil(L/4))
#endif
#ifndef BC_QUEEN_SLACK
#define BC_QUEEN_SLACK 0       // QSK: post-move free-reach slack >= 3 as a hard filter when possible, larger
                               // preferred
#endif
#ifndef BC_QUEEN_KINAVOID
#define BC_QUEEN_KINAVOID 0    // our dragons never step next to our queen's head (her own fresh children took her)
#endif
#ifndef BC_ECON_B0
#define BC_ECON_B0 1           // economy guards: the queen never free-sprints (it left QSK at qskDone); main.cpp's
                               // queen multi-step assertion
#endif
#ifndef BC_ECON_TRACE
#define BC_ECON_TRACE 0        // economy trace tags in the LOG line (dip-in / dip-rev / dip-fail / bed-cd / noblock /
                               // idle-bed / crown ...)
#endif
#ifndef BC_ECON_DIP
#define BC_ECON_DIP 0          // dead-end / fountain harvest (a short dragon dips into a lane of food and leaves by a
                               // reverse split)
#endif
#ifndef BC_ECON_BED
#define BC_ECON_BED 0          // spawn clock (wait discount, countdown completion incl. the mirror's, unlock value,
                               // NOBLOCK, idle-to-bed)
#endif
#ifndef BC_ECON_CROWN
#define BC_ECON_CROWN 0        // a king emerges from unit saturation (r >= 250)
#endif
#ifndef BC_QUEEN_TINY
#define BC_QUEEN_TINY 0        // tiny queen (needs SPLIT): she stays 2 long and buds the rest until lateNoSplit, then
                               // is fed
#endif
#ifndef BC_QUEEN_FLEE
#define BC_QUEEN_FLEE 1        // with an enemy head inside her danger circle the queen may take 2 steps (free from 5
                               // long)
#endif
#ifndef BC_QUEEN_FEED
#define BC_QUEEN_FEED 1        // feeding and the LONGEST facts go to the queen (0: survival only)
#endif

namespace bc {

// Direction order matches the engine: North is y-1.
enum Dir : int { N = 0, E = 1, S = 2, W = 3 };
inline constexpr int DX[4] = {0, 1, 0, -1};
inline constexpr int DY[4] = {-1, 0, 1, 0};
inline constexpr char DCH[4] = {'N', 'E', 'S', 'W'};
inline constexpr int INF = 1 << 29;

inline int dirFromChar(char c) {
    switch (c) {
    case 'N': return N;
    case 'E': return E;
    case 'S': return S;
    case 'W': return W;
    }
    return -1;
}

// Every tunable number lives here so self-play tuning can sweep them later.
struct Params {
    // pearl value
    double gamma = 0.80;              // discount per move of distance
    double eatBonus = 1.0;            // eating right now
    double exploreValue = 0.03;       // per never-seen tile (unknown maps)
    // A never-seen tile may already hold a pearl: bed share of the tiles seen x min(1, round / mean refill gap) x
    // unseenScale (World::unseenPearlProb). Without it, on a map where every tile is a slow bed, we ate far fewer
    // natural pearls in rounds 0-50 than the enemy.
    double unseenScale = 0.7;         // share of those spawns not yet eaten by anyone
    int unseenMinTiles = 30;          // tiles seen before the estimate is trusted
    // Explicit exploration (explore the whole map early, coordinated over sonar): a never-seen tile in a sector the
    // team has not explored is worth this much more, fading to 0 by exploreUntil. Without it we had far fewer tiles
    // in view at round 200 than strong opponents.
    double wExploreSector = 0.1;
    int exploreUntil = 200;
    // The per-tile values above fade with gamma, so a dragon in seen land had no pull at all; the nearest never-seen
    // tile of an unexplored sector also pulls from afar (gammaFar), fading like the sector bonus.
    double wExploreFar = 0.1434;
    // Map symmetry (World::setSymmetry): a fountain known only as the mirror image of one we watched (mostly the
    // enemy's side) pulls this much of a watched one.
    double wMirrorHot = 1.2343;
    // Heat map (BC_HEAT): a free swarm dragon is pulled (gammaFar) toward the nearest sector no teammate and no enemy
    // is known to hold.
    double wHeatSpread = 0.0;
    // Scavenger (BC_SCAV): each drop cluster pulls its ceil(count/2) nearest free dragons, wScav per pearl (4 at
    // most) discounted 0.85 a step, unless an enemy head is scavSlack steps closer; a teammate head within 2 of an
    // enemy head gets its nearest free teammate standing by 2 tiles off.
    double wScav = 0.0;               // 0 = off unless a tuned context sets it (3.0 when first introduced)
    int scavRange = 15;
    int scavSlack = 2;
    // Engage (BC_ENGAGE: attack, hold or retreat by data). The value of a head-on trade for the side that moves in,
    // fitted on ~18k trades from ladder replays (our games and games between top-ranked teams): it gives its length
    // and takes the victim's, and its side eats a share p of the drops ceil(la/2) + ceil(lv/2), with logit p = engB0
    // + engB3 * (heads within 3 of the site, its side minus theirs) + engB5 * (the same within 5) + engBL * (steps
    // its side's nearest other head is nearer to the site than theirs, capped at 4). Predicted value bands match what
    // happened: <= -3: -5.2 (3% ahead), 1..3: +1.6 (81%), >= 3: +6.4 (97%).
    double engB0 = 0.273, engB3 = 0.038, engB5 = 0.098, engBL = 0.278;
    double engageAttack = 1.0;        // attack (step or sprint onto a head) when the trade is worth this much to us
    // retreat: danger per unit of an enemy's value of trading us where we stop. It does not know whether the enemy
    // will move in, and fleeing costs food: at 1.0 the untuned line starved (far fewer natural pearls by round 100 on
    // a pearl-poor map), so it starts at an earlier tuned wDanger and the tuner finds the scale.
    double wEngage = 2.4914;
    // King (BC_KING): in the feeding window every dragon feeds the longest teammate heard of (LONGEST facts), medium
    // ones too while it is not kingMargin longer than the longest enemy seen.
    int kingMargin = 5;
    // Dragon plans: count both sides' longest dragons ourselves, slay theirs, guard ours. In a reviewed loss we
    // out-ate the opponent and had nearly twice its total length but lost the round-500 count: our longest
    // reverse-split itself in a crowd of our own, and their king grew in the other half with almost none of our heads
    // near it. Guard (BC_KINGGUARD): from guardFrom our longest dragon is the king: no voluntary split, it needs at
    // most kingSpace reachable tiles (a crowd of our own is not a trap), reverses only when (nearly) boxed in, and
    // danger near enemy heads (and inside their sprint reach) costs it kingDanger times more.
    int guardFrom = 150;
    int kingSpace = 12;
    double kingDanger = 3.0;
    // Slay (BC_SLAY): from slayFrom, once the longest enemy known (World::enemyKing) is slayMinLen long, its
    // slayHunters nearest dragons of slayMaxLen or shorter hunt it: pulled from afar (gammaFar, torus distance
    // beyond the BFS horizon), and in view it is their squad target (the attack / support machinery).
    int slayFrom = 200;
    int slayMinLen = 10;
    int slayMaxLen = 8;
    int slayHunters = 3;
    double wSlay = 3.0;
    int slayPer = 8;                  // slay with a big army: hunters = max(slayHunters, units / slayPer)
    // Module F (BC_V9_FINISH): from fFrom, with fUnits of ours alive, a free dragon of fMaxLen or less is pulled
    // (wFinish, gammaFar) to the nearest enemy head seen or heard of in the last fAge rounds: on a big open map an
    // army of 64 dragons lost the round-500 count to a handful of long ones; eliminating them wins outright. Early
    // king (BC_V9_EKING): a dragon grows at most one a round, and growers bud down to ~12-16 until round 400, so the
    // round-500 king came from feeding alone; from ekFrom our longest (LONGEST facts, at least ekMinLen) keeps every
    // segment and plays the guard (kingMe_), the king's unit slots and right of way included. Density cap
    // (BC_V9_DENSITY): small maps crowd (on a 512-tile map 40+ dragons a side, and most of our deaths in a test game
    // were small dragons boxed into dead-end pockets), so voluntary splits stop at max(kDensMin, W x H / kDensity)
    // dragons.
    int kDensity = 1;                 // 1 = off in the base parameters (the cap is the whole map); 20 when first
                                      // introduced
    int kDensMin = 12;
    int ekFrom = 100;
    int ekMinLen = 8;
    // The king grows only by eating, and its teammates took its pearls (a pearl a friend reaches first is worth 0):
    // from ekFrom the others leave pearls within ekYield of the king's head in view (0: off), and the king values
    // pearls with ekGamma (0: the usual gamma; a tuned gamma of 0.7 sits at the bottom of its range).
    int ekYield = 0;
    double ekGamma = 0.0;
    double ekZone = 1.5;              // others' cost of ending within 2 of the early king's head, x (3 - distance) / 3
    int tailStarters = 1;
    // Early splits: until esUntil a dragon splits even when its best move eats (without it, ladder opponents split
    // more often in rounds 0-50 and ate more natural pearls; in self-play every elimination game went to the side
    // ahead in units at round 50). 0: off.
    int esUntil = 300;
    // Swarm (BC_SWARM). swarmUntil: before it every dragon of 4 or more splits in half, growers too, while eating
    // too, in a crowd too; smallRisk: danger weight of dragons of smallLen or less; lateNoSplit: no split at all from
    // this round.
    int swarmUntil = 300;
    double smallRisk = 0.5;
    int smallLen = 3;
    int lateNoSplit = 400;
    // Escape (BC_V9_ESCAPE): the opening's pearls mostly lie uneaten behind portals nobody has seen both ends of
    // (on several portal maps dozens of pearls still lay there at round 25). On one map both sides' 4 units sat 100
    // rounds in a closed start area: with 4 units or fewer the survival rule forbids probing, and wProbe is 0. A
    // dragon that has eaten nothing for escStarve rounds probes the nearest unpaired portal within escRange steps
    // (units >= 2), pulled by wEscape. Scouts (BC_V9_SCOUT): on one map our starters roamed the top half for 40
    // rounds and ate 5 pearls while over a hundred lay in the bottom half by round 50 (gamma 0.7 sees ~5 steps,
    // unseen land is worth ~0, and every dragon's far pull went to the same frontier). Until scoutUntil, a free
    // dragon takes the nearest unexplored sector (EXPLORED facts) no known teammate is nearer to, and walks to its
    // centre, pulled by wScout (linear, 60 steps to zero).
    int scoutUntil = 0;               // 0 = off in the base parameters (80 when first introduced)
    double wScout = 2.0;
    int scoutHungry = 0;              // scout only after this many rounds without eating (0: any free dragon)
    int escStarve = 12;
    int escRange = 40;
    double wEscape = 3.0;             // linear, 40 steps to zero
    // Exploration through unpaired portals (BC_PORTAL2). Strong teams walk out of a closed start area through a
    // portal nobody has seen the other end of within the first ~10 rounds and eat over a hundred pearls by round 150,
    // while a bot that never crosses early eats a few dozen at most (and on portal maps 50-120 pearls lie uneaten
    // from round 200 on). A region is my tiles reachable on the known map (other dragons ignored); it is closed when
    // no unseen tile is reachable, poor when its beds (a watched bed at p2GapScale / its largest countdown, an
    // unwatched one at p2BedRate) feed our dragons in it less than p2Poor pearls a round each, or when I have not
    // eaten for p2Starve rounds. Then a dragon of p2MaxLen or less (growers too) probes the region's nearest unpaired
    // portal end, whatever the unit count (the survival rule is about trades), with 2+ units; one prober per portal
    // as before; pulled by p2Pull (linear, 40 steps to zero), and the entry's dead end is no trap for it (the
    // escape fixes). Early probes: top teams go through an unpaired portal as soon as they see it (first crossing
    // around round 8-11 in replays). Before round p2Early a dragon of p2MaxLen or less with an unpaired portal end
    // within p2Range steps probes it (one prober per portal, 2+ units), whatever its region.
    int p2Early = 0;                  // 0 = off in the base parameters (120 measured worse in self-play)
    // ... and only after p2EarlyStarve rounds without eating (on a map whose portals lead from small rooms by our
    // start into the enemy's, early probes from its rich centre went to the enemy)
    int p2EarlyStarve = 6;
    int p2Range = 10;
    double p2Poor = 0.0;              // off in the base parameters (0.08 when first introduced)
    int p2Starve = 60;                // nearly off in the base parameters (20 when first introduced)
    int p2MaxLen = 4;
    int p2Until = 500;
    double p2BedRate = 0.01;
    double p2GapScale = 1.5;
    double p2Pull = 3.0;
    int escUntil = 500;               // escape only before this round (all game long it caused mass probing)
    int fFrom = 300;
    int fUnits = 40;
    int fMaxLen = 8;
    int fAge = 6;
    double wFinish = 2.0;
    int slayAttack = 99;              // slay with a big army: hunters ranked below this go in (originally 1, the others supported)
    // Room capacity (BC_ROOMCAP; in a reviewed game 4-7 of ours sat in each 25-tile closed room for 200 rounds and
    // most of our deaths there were by their own moves; children born in a room only know the portal's inner end, so
    // the pocket rule never let them out). A closed region (open edges only, every tile seen, at most pocketMax
    // tiles) holds at most max(1, tiles / roomTiles) of our dragons. Which stay is a draw every one of them makes
    // alike (a hash of the ids and round / 50) from the teammates it sees or heard of in the room; the others leave
    // through a portal, an unpaired one too (stepping in like a probe); nobody splits in a full room (the survival
    // rule aside), and crossing a portal into a full closed room costs wRoomFull.
    int roomTiles = 16;
    double wRoomFull = 3.0;
    // Module K, late king (BC_V9_KING). Round-500 games go to the longest dragon; self-play showed the timing of
    // consolidation decides them (feeding from 420 instead of 300 won clearly on two open maps) and that mazes need
    // the feeders to leave early (on a maze map they never arrived). The king is whoever is longest (LONGEST facts),
    // never pre-selected. A dragon clearly shorter than the king leaves to feed it once round + kWalk x its way to
    // the king >= 500 - kWindow (the way: BFS on the known map), sacrifices next to its head only while at most
    // kBacklog pearls lie within 3 of that head (the king eats about one pearl in two moves: feeders arriving
    // together left drops uneaten at round 500) and no enemy head is within 4. The king stops voluntary splits
    // kNoSplit rounds before the window and plays the guard (kingMe_); other dragons keep splitting until they leave.
    // A leaving feeder is pulled by kFeedPull (linear in the way, 80 steps to zero) and values pearls at kFeedForage.
    int kWindow = 70;
    int oneKingFrom = 350;            // one-king rule (from a study of top-ranked teams' replays): from this round
                                      // one king, everyone else feeds it; 0 off. Not overridden by any tuned context,
                                      // so the final build feeds one king from round 350
    double kWalk = 1.3;
    int kBacklog = 2;
    int kNoSplit = 150;
    double kFeedPull = 12.0;
    double kFeedForage = 0.4;
    // The stream: from kStreamFrom, the kQueue nearest teammates within kStreamRange steps of the king (and at most
    // kStreamMaxLen long) feed it whenever its drops are eaten, so the king grows all the way (it gains at most one a
    // round) while the army keeps splitting (in a test game on a big open map the window alone left 922 total length
    // and a 38-long king).
    int kStreamFrom = 330;
    int kQueue = 2;
    int kStreamRange = 12;
    int kStreamMaxLen = 10;
    // Who is king: the longest teammate heard of in the last kLongestAge rounds (LONGEST facts go out first every
    // turn from kStreamFrom - 80, and the king sends its SELF fact all four ways); a dragon shorter than kKingMin
    // never takes itself for the king (in a test game a third of our dragon-turns thought they were it).
    int kLongestAge = 40;
    // Saturation: with kCapUnits of ours alive (the limit is 64) from round kCapFrom, the stream runs with kCapQueue
    // feeders and the king keeps every segment (in tests on a big open map: 64 alive and 700-950 total length, king
    // 42-54; feeding everything from round 300 instead reached 62-78).
    int kCapUnits = 56;
    int kCapFrom = 120;
    int kCapQueue = 3;
    int kCapMaxLen = 20;              // saturated: dragons up to this long feed the king in view
    int kReserve = 2;                 // unit slots only the king may fill (its reverse split)
    double kZone = 1.5;               // cost for others of ending within 2 of the guarding king's head (3 - distance) / 3
    int kKingMin = 8;
    double seenDecay = 0.97;          // belief a pearl we saw is still there, per round
    double spawnDecay = 0.985;        // belief an unseen spawned pearl is still there, per round
    double enemyCloserFactor = 0.5;   // pearl an enemy head reaches first
    int horizon = 48;                 // BFS depth considered
    int maxRivalFields = 12;
    // Fight search (BC_FIGHT, fight.hpp): runs when wFight > 0 and the nearest enemy head is within fightRadius; its
    // value (lengths) is weighted by wFight; fightPlies plies; enemy nodes blend its best reply and the mean of its
    // replies by fightParanoia; head-on trades need the enemy to be fightTradeMin longer (defence excepted; -10:
    // off). A first version (wFight 1, fightTradeMin 1) lost clearly in self-play: both start off here, and wFight is
    // 0 in every tuned context. Opening prior (BC_RL_PRIOR, rl_net.hpp): the opening network (imitation of top-ranked
    // teams' turns in rounds 0-200) before round rlUntil. wPrior: weight of log p(relative direction) added to each
    // root move's search value (0: off); rlSplitP: split when the network's split probability reaches it (above 1:
    // off); rlBand > 0: the move prior only among moves within rlBand of the best. (Split prior 0.3 + move prior 0.05
    // measured slightly positive in self-play.)
    int rlUntil = 200;
    double wPrior = 0.0;
    double rlSplitP = 1.1;
    double rlBand = 0.0;
    int fightRadius = 3;
    double wFight = 0.0;
    int fightPlies = 3;
    double fightParanoia = 0.5;
    int fightTradeMin = -10;          // distance fields computed for the nearest visible heads
    int maxDepth = 10;                // lookahead cap for the anytime search
    double leanMargin = 0.045;        // < this much turn clock left at decide(): one-step turn only

    // safety
    int spaceMargin = 3;              // reachable tiles needed: spaceFactor * length + margin
    double spaceFactor = 2.0;         // eating keeps the tail still, so plain length is optimistic
    double wSpace = 0.6;              // mild preference for open space
    double wDanger = 1.5;             // per own segment, tile next to an enemy head
    double wPocket = 1.5;             // entering a tile with a single way onward
    double wReverse = 1.5;            // entering a dead end we can only leave by a reverse split
    double wBlind = 2.0;              // portal into unseen tiles where dragons were seen lately
    double wBlindQuiet = 0.3;         // portal into unseen tiles with no recent sightings
    int blindMemory = 6;              // rounds a sighting near a portal exit stays relevant
    double wPortalLoiter = 1.0;       // ending a move on a tile beside a portal

    // phases
    int earlyEnd = 120;               // hunting squads switch on here ("mid game")
    int midEnd = 400;                 // late game: growers stop splitting, play safest

    // roles: growers (the starting dragons, or anyone long) farm and stay safe;
    // the swarm (split children) escorts, hunts and scavenges.
    int promoteLen = 10;              // any dragon this long plays as a grower
    int growerKeepBase = 4;           // grower keeps at least base + round/keepEvery segments
    int growerKeepEvery = 30;
    int growerChild = 2;              // growers bud off children this long
    double growerDanger = 3.0;        // extra danger multiplier for growers
    int swarmSplitLen = 4;            // swarm dragons split in half at this length
    int splitEnemyDist = 2;
    int splitRoom = 8;               // both halves need this much reachable room to split

    // Online map knowledge (no map names): a bed is hot once hotObs countdowns were seen on it and none exceeded
    // hotGap; map group 1 needs hotBedsForGroup hot beds, group 0 a seen refill rate below poorRate pearls per round
    // per tile (after groupMinTiles tiles). Sonar coordination (comms.hpp): every turn up to four authenticated facts
    // go out, relayed hop by hop; heard enemy heads out of sight are avoided, heard teammates closer to a pearl get
    // it, fountains are shared, and small dragons walk to the longest teammate they heard of in the feeding window.
    int sonarOn = 1;
    // Quiet opening (BC_QUIET): in ladder replays strong teams send few sonar pings per dragon-turn in rounds 0-200
    // and many more late in the game, while an always-talking bot pings ~4 per dragon-turn throughout. No sonar goes
    // out before sonarFrom.
    int sonarFrom = 300;
    // Situational sonar: sonar helps in some situations and hurts in others (in replays strong teams ping more with
    // an enemy head within 3 than with none before round 300, and their longest pings most; a sweep of sonarFrom gave
    // mixed results by map; a sizeable share of our deaths by enemies had a teammate see the killer first). Before
    // sonarFrom a dragon sends only position facts (SELF, ENEMY, LONGEST, PORTAL) and only when an enemy head is
    // within sitEnemyR (0: never), when it is the longest teammate it knows of, every sitKingEvery rounds (0: never),
    // or the turn after it crossed an unpaired portal (sitPortal 1).
    int sitEnemyR = 0;
    int sitKingEvery = 0;
    int sitPortal = 0;
    // Full policy set (BC_FULL): run-time switches of components that had only a compile-time one (tuned, 0/1)
    int kingOn = 1;                   // one king (BC_KING): feed the longest teammate heard of
    // Final push (BC_FULL; from a ladder replay review: round-500 games decided the weak maps and the enemy won them
    // in the last 100 rounds - its longest grew by 10-16 while its units halved, while ours added a few or shrank:
    // our feeding went to local kings and our long dragons were reverse-split in crowds). From finalFrom every dragon
    // of finalMaxLen or less but the king (the longest teammate heard of, LONGEST facts) walks to the king (linear
    // pull finalPull, 80 steps to zero, the way on the known map beyond the BFS horizon) and dies next to its head
    // while at most finalBacklog pearls lie within 3 of it and no enemy head is within 2; dragons of finalRevLen or
    // more reverse only before certain death from finalFrom on. finalFrom 500: off.
    int finalFrom = 500;
    int finalMaxLen = 40;
    int finalBacklog = 2;
    double finalPull = 12.0;
    int finalRevLen = 1000;
    // (A later review: a few late eliminations with units falling to 0 in rounds 350-500, and the longest shrank in
    // the last 100 rounds of about half of the round-500 losses.) finalKeepUnits: a feeder dies next to the king only
    // while we have more than this many units (0: no floor); lateRevFrom: from this round dragons of finalRevLen or
    // more reverse only before certain death, final push or not (500: off).
    int finalKeepUnits = 0;
    int lateRevFrom = 500;
    int deadEndOn = 0;                // no short dragon into a tree-like dead end (BC_DEADEND)
    int tailOn = 0;                   // count on a reverse split before the tail is tracked (BC_V9_TAIL; measured
                                      // better without it)
    int roomOn = 0;                   // room capacity (BC_ROOMCAP; it lost when every probe died after crossing)
    int remoteMaxAge = 8;             // rounds a heard dragon position stays useful
    int remoteRadius = 4;             // heard enemy heads matter this close (Chebyshev)
    double wRemoteEnemy = 0.5;        // per own segment at the heard enemy's tile, fading with distance
    double remoteYield = 1.0;         // off: the local team's assignment decides instead
    int remoteSlack = 2;              // "clearly": this many steps closer (torus Manhattan)
    int feedRemote = 1;               // feeding pull toward a heard longest teammate out of sight
    int longestMaxAge = 20;
    // Local / global teams (team.hpp)
    int teamOn = 1;
    int assignOn = 0;                 // pearl assignment off (it kept teams clustered and lost in self-play)

    // Tactics library (policy.hpp computeTactics). Replays: self-deaths rise with the number of own heads within 3
    // (at 8+: 54 per 1000 turns vs 4 alone); top teams spend 2% of turns in such crowds, an early version of ours 8%.
    //   disperse: a crowd field around each own head in view costs every planned step; the
    //             more own heads around, the more; shorter dragons give way, the VIP barely.
    //   no split: no voluntary split with crowdNoSplit own heads around.
    //   protect:  the longest teammate in view (>= vipMinLen) plays safe like a grower, and the
    //             others escort it / intercept threats (the grower machinery), at any round.
    int tacticsOn = 1;
    int crowdR = 3;                   // Chebyshev radius of "around"
    int crowdStart = 3;               // own heads around before dispersal starts
    double wDisperse = 1.5;           // per crowd-field unit per planned step, per crowd level (1.5 measured best in
                                      // a self-play sweep of 0.35-2.5)
    int crowdNoSplit = 5;
    int vipMinLen = 1000;             // VIP protection off (it gathers escorts into a crowd and lost in self-play)
    double vipDisperse = 0.3;         // the VIP moves less: the others make room
    int teamRadius = 5;               // heard teammates this close (Chebyshev) join my local team
    int teamMax = 5;                  // members beyond this leave to explore
    // team weights: a nudge, not the whole decision (larger weights lost in self-play)
    double wTeamTarget = 0.5;         // pull toward the pearl the team assigned to me
    double teamYield = 0.6;           // value kept for a pearl the team assigned to someone else
    double wSpread = 0.2;             // detached members: pull away from the team's leader
    double wCrowd = 0.1;              // other local teams' areas (heard TEAM facts), per nearness
    int crowdRadius = 6;
    // Portal tactics (from a reviewed loss: a 204-tile area reachable only through portals held 4-14 enemy dragons
    // from round 200 and never one of ours, because a portal counts as a wall until both of its ends have been seen).
    //   probe:  the nearest short dragon (<= probeMaxLen) steps into an unpaired portal end to find
    //           out where it leads (then both ends are known and everyone can route through it)
    //   pocket: in a small region closed except for portals, the shortest own dragon stays, eats and
    //           splits; every other own dragon there heads for a portal exit and does not split
    int probeOn = 1;
    int probeMaxLen = 4;
    int probeRange = 10;              // unpaired portal ends up to this many steps away
    double wProbe = 0.5;              // (1.5 / range 20: about half of all turns walked to probes on a portal-heavy
                                      // map)
    int pocketOn = 1;
    int pocketMax = 150;              // at most this many tiles, all seen, to count as a pocket
    double wLeave = 1.5;
    int hotObs = 3;
    int hotGap = 10;
    int hotBedsForGroup = 2;
    int groupMinTiles = 150;
    double poorRate = 0.004;

    // Head-on trades (from ladder replays): whoever moves in eats most of the ceil(L/2) pearls the two dead dragons
    // drop, and top teams sprint (MOVE of 2-3 steps) into heads they can reach. So never wait next to an enemy head
    // for it to decide, and start trades ourselves, sprinting too, where we have the local numbers.
    int localRadius = 3;              // heads this close (Chebyshev) to a trade site count as local
    int reachCap = 3;                 // enemy sprint reach considered: min(visible length - 1, cap)
    double wExposed = 1.2;            // per own segment, ending within an enemy head's reach
    double exposedFavour = 0.35;      // multiplier when we have the local numbers there
    int sprintMax = 3;                // our own sprint trades: at most this many steps
    // Voluntary trades (single step or sprint) need a teammate there to eat the drops. Without this, on pearl-poor
    // maps we traded with fewer own heads around and the enemy ate most of the drops (on rich maps we won them
    // clearly).
    int tradeSupport = 1;             // own heads (besides me) within supportRadius of the site
    int supportRadius = 3;            // fixed: the tuner shrank localRadius to 2, where 0 vs 0 passed
    double sprintTradeScore = 60.0;   // base score of a sprint trade (single-step trades are 50)
    int sprintTrades = 1;             // 0: no sprint trades (ablation)
    // 0: danger next to enemy heads unless we would trade; no sprint reach
    // 1: danger inside any enemy's sprint reach, reduced where we have the local numbers
    // 2: like 0 next to heads, plus sprint-reach danger only where enemies outnumber us
    // Mode 0 is the default since an ablation (sandbox, 11 maps x 2 sides): sprints + mode 0 won 14-8, mode 1 without
    // sprints 5-17, mode 1 with sprints 8-14.
    int exposureMode = 0;

    // End-game feeding: the longest-dragon tiebreak decides round-500 games, and the top teams sacrifice small
    // dragons into one big one. Nobody is pre-assigned: a dragon that sees a teammate at least feedMargin longer
    // walks to its head and dies there, dropping ceil(L/2) pearls where it will eat them.
    int feedRound = 400;
    int feedStop = 490;               // later sacrifices cannot be eaten in time
    int feedMargin = 4;
    int feedMaxLen = 6;               // only small dragons feed: medium ones anchor their own area
                                      // (feeding cascades up the chain threw long dragons away)
    int feedDist = 1;                 // die when this close to a tile next to the big head
    double wFeed = 3.0;

    // Hot beds: fast-respawning beds (fountains) are a steady income, valued as a long-range pull proportional to
    // their yield rate (pearls per round).
    double hotRate = 0.1;             // mean respawn gap <= 10 rounds
    double wHot = 2.0;                // the fountain-map profile's value; only hot beds feel it anyway
    double gammaFar = 0.93;
    int hotYield = 4;                 // yield a hot bed only to a teammate this close to it

    // economy / attrition
    int growerMinUnits = 40;          // growers exist only once the army is this big...
    int growRound = 300;              // ...or from this round
    int lateSplitUnits = 20;          // after growRound the swarm splits only below this many units
    int tradeMinUnits = 4;            // below this many units we do not trade heads
    int tradeSlack = 1;               // trade if own length <= enemy visible length + slack
    int survivalUnits = 3;            // at or below: extra caution
    double survivalDanger = 3.0;

    // squads
    int squadSize = 3;                // per enemy target: 1 attacker + (squadSize-1) supporters
    int escortCount = 3;              // swarm dragons that stay around a visible grower
    int escortRadius = 5;             // enemy head this close to a grower is a threat
    int escortRing = 3;               // escorts hold this distance from the grower
    int supportRing = 3;              // supporters hold this distance from the target, ready to eat
    int huntMinEnemyLen = 6;          // enemies shorter than this are not worth a swarm dragon
    double wHunt = 1.5;
    double wEscort = 0.8;
    double wSupport = 0.6;

    // trading heads
    double tradeRatio = 2.0;          // outside squads: trade only into enemies >= ratio * own length
    double wTrade = 1.0;
    double tradeMin = 3.0;            // net length gain needed to go for it
};

inline Params const kParams{};

// Fixed survival policy. Constants, not Params, so tuning cannot remove it: the tuner once pushed tradeMinUnits down
// to 2, and in a ranked loss the bot started head-on trades with 3, 2 and 2 units left, then its last dragon (length
// 3, too short to split, no teammate in sight for 20+ rounds) died and the team was eliminated.
//   few units (<= kSurvivalUnits): no voluntary trades or hunts; reproduce
//   isolated (no teammate in view for kIsolatedRounds, <= kIsolatedUnits units): reproduce
//   reproduce = split off a 2-long child as soon as length allows (4), whatever the grower /
//   late-game rules say, until kRebuildStop or the feeding phase (feedRound), whichever is
//   first; pearls worth more while too short
inline constexpr int kSurvivalUnits = 4;
inline constexpr int kIsolatedRounds = 15;
inline constexpr int kIsolatedUnits = 8;
inline constexpr int kRebuildStop = 485;
inline constexpr double kRebuildPearlBoost = 1.5;
// Queen constants (BC_QUEEN, the rules-1.2.3 queen rule)
inline constexpr int kQueenBudUnits = 3;     // the queen buds only while the team has this many dragons or fewer...
inline constexpr int kQueenBudLen = 6;       // ...and she is this long: a 2-3 child off her tail, she stays the front
inline constexpr int kQueenGone = 60;        // rounds without a fact of the queen before feeders stop counting on her
inline constexpr int kQueenReach = 6;        // the queen keeps out of enemy sprints up to this many steps (paid ones too)
inline constexpr double kQueenPaidW = 0.5;   // ... the paid tier (beyond the free steps and reachCap) at this weight
inline constexpr int kQueenSafeMin = 12;     // the queen wants this many tiles (or her space need) she reaches before any enemy head
inline constexpr double kQueenSafeW = 24.0;  // ... the shortfall costs up to this much on her first step
inline constexpr double kQueenPocket = 4.0;  // ... a step with one way on (corridor, pocket) costs this
inline constexpr int kBedUntil = 300;         // BC_ECON_BED: the spawn clock acts before this round
inline constexpr double kBedWait = 0.9;        // ... a turn of waiting for a spawn costs this factor (gamma is 0.7)
inline constexpr int kBedMaxGap = 150;         // ... beds with a larger refill gap are not worth waiting for
inline constexpr double kWUnlock = 1.0;        // ... eating a pearl that sits on a bed: + this x min(0.5, 1 / gap)
inline constexpr double kWNoBlock = 1.5;       // ... a step onto a bed spawning next round without eating: - this
inline constexpr double kWIdleBed = 0.5;       // ... the pull of an idle dragon's bed
inline constexpr int kIdleRounds = 30;         // ... idle = no meal for this many rounds
inline constexpr double kDipQ = 0.5;           // BC_ECON_DIP: the share of a reverse-split stub's drops we eat back
                                               // (a calibration constant)
inline constexpr int kDipMinFood = 2;          // ... the lane must hold at least max(this, 4 - L) pearls
inline constexpr int kDipReachSlack = 2;       // ... an enemy must need more turns than the dip's depth + this to reach the mouth
inline constexpr int kDipMouthR = 3;           // ... no other head within this Chebyshev distance of the mouth
inline constexpr int kDipMinUnits = 8;         // ... no dips with fewer units than this
inline constexpr double kKinW = 60.0;         // kin avoidance: a step next to our queen's head costs this
#if BC_MAPNET
inline constexpr double kMapnetDangerW = BC_MN_DANGER_W;  // BC_MAPNET: a step onto a tile of danger 1 (a death or
                                                          // trap seen, fading over 200 rounds) costs this, about two
                                                          // steps of value at gamma 0.8
#endif
inline constexpr double kEscortW = 4.0;       // escort (BC_QUEEN_ESCORT): the pull to the shield tile (wSlay is 3)
inline constexpr double kRoomGuardW = 30.0;   // room guard: the cost of a step that leaves our queen less room than L + 2
#if BC_BEDRAID
inline constexpr int kRaidUntil = 120;        // BC_BEDRAID: the opening's food probes only before this round (the
                                              // escape all game long caused mass probing)
inline constexpr int kRaidStarve = 8;         // ... after this many rounds without eating
#endif
#if BC_EXPAND
inline constexpr int kExpandIdle = 12;        // BC_EXPAND: idle = no meal for this many rounds
inline constexpr int kExpandMaxLen = 8;       // ... dragons up to this long go
inline constexpr int kExpandHeld = 3;         // ... a bed with one of our heads within this Chebyshev distance is held; a dragon with a
                                              //     known bed within this many steps is in a core and stays
inline constexpr double kWExpand = 1.0;       // ... the pull (the idle bed's is 0.5)
#endif
#if BC_BED_KEEP
inline constexpr int kKeepSteps = 6;    // BC_BED_KEEP: a fast bed this many steps away at most
#ifndef BC_KEEP_GAP
#define BC_KEEP_GAP 10
#endif
inline constexpr int kKeepGap = BC_KEEP_GAP;  // ... a fast bed: the largest countdown seen (its own or its mirror's) or its learned gap at most this
inline constexpr int kKeepR = 3;        // ... the hold radius (Chebyshev) round the kept bed; its cluster: the fast beds within it
inline constexpr int kKeepUntil = 300;  // ... before this round (the spawn clock's kBedUntil)
inline constexpr double kWKeep = 1.0;   // ... the pull to the kept bed (as BC_EXPAND's pull)
inline constexpr int kKeepSlack = 3;    // ... bit 2: due when its countdown is at most its distance + this
#if BC_TIER2
inline constexpr int kTier2Gap = 12;    // BC_TIER2: a second-tier bed - the largest countdown seen (own or mirror's)
                                        // or its learned gap at most this
inline constexpr int kTier2Steps = 10;  // ... this many steps away at most
#endif
#endif
#if BC_OPEN_RISK
inline constexpr int kOpenRiskUntil = 100;       // BC_OPEN_RISK: the opening's end
inline constexpr int kOpenRiskMaxLen = 2;        // ... dragons this long at most
inline constexpr int kOpenRiskFastGap = 40;      // ... bit 8: a fast bed - the largest countdown seen on it or its mirror at most this
inline constexpr double kWOpenRiskFast = 1.0;    // ... its pull (as BC_EXPAND's; the idle bed's is 0.5)
#endif
#if BC_OPEN_EXPAND
inline constexpr int kOpenExpandUntil = 120;  // BC_OPEN_EXPAND: the opening's last round
inline constexpr int kOpenExpandHeld = 4;     // ... a bed within this (Chebyshev) of another of our heads is ours already; of an enemy head
                                              //     (seen, or heard <= kOpenExpandAge rounds ago) or of the enemy's start, theirs
inline constexpr int kOpenExpandAge = 10;
inline constexpr int kOpenExpandHome = 5;     // ... a bed within this of our first head is home (the start tower), of its mirror the enemy's
inline constexpr int kOpenExpandBand = 3;     // ... an area: the known free beds within this of a bed
inline constexpr int kOpenExpandSoon = 100;   // ... a bed counts 1 when its countdown (seen, or its mirror's) is at most this, 1/2 unknown, 0 longer
inline constexpr int kOpenExpandCap = 12;     // ... its beds, counted to this many
#ifndef BC_OPEN_EXPAND_W
#define BC_OPEN_EXPAND_W 0.4
#endif
#if BC_OPEN_EXPAND_GATE
inline constexpr int kGateBedRound = 10;      // ... bit 1: decided at the first turn from this round
inline constexpr int kGateBedSteps = 10;      // ... the known beds within this many steps of the head
inline constexpr int kGateMaxBeds = 3;        // ... at most this many: on
inline constexpr int kGateMaxStarts = 2;      // ... bit 4: at most this many starting dragons
inline constexpr int kGateMaxTiles = 700;     // ... and at most this many tiles (the small maps this targets have
                                              // 288-600 tiles; an open 25 x 35 map stays out)
#endif
inline constexpr double kWOpenExpand = BC_OPEN_EXPAND_W;  // ... the pull to the area's bed: this much a step nearer,
                                                          // linear to 0 at max(40, W + H) steps (on one map the way
                                                          // out of the start area to the centre is 41 steps; a
                                                          // stronger 3.0 / 40 pull a step lost to the start area's
                                                          // beds and the 1-round beds next to it)
#endif
#if BC_EXPAND_OPENING
inline constexpr int kEoMaxUnits = 2;         // BC_EXPAND_OPENING: the mode's start - a team of at most this many
                                              // units ...
inline constexpr int kEoBedCheck = 10;        // ... with no known bed in reach from this round
inline constexpr int kEoEatCheck = 12;        // ... or nothing eaten from this round
inline constexpr int kEoLenCheck = 40;        // ... or, from this round (any unit count), at most half the longest enemy known
inline constexpr int kEoExitUnits = 6;        // ... its end: this many units
inline constexpr int kEoUntil = 150;          // ... or this round
inline constexpr int kEoExitEats = 3;         // ... or this many pearls eaten with kEoExitBeds known beds in reach
inline constexpr int kEoExitBeds = 2;
inline constexpr double kEoQueenEat = 2.0;    // ... QUEEN_MOTHER: her eat weight
inline constexpr int kEoBandR = 3;            // ... MIRROR_TARGET: a band = the mirrored beds within this Chebyshev distance
inline constexpr double kWEoMirror = 1.5;     // ... its pull (the idle bed's 0.5)
inline constexpr int kEoHuntUnits = 3;        // ... ANTIHUNT: at most this many units
inline constexpr int kEoHuntLen = 5;          // ... an enemy this long (visible segments)
inline constexpr int kEoHuntR = 4;            // ... within this Chebyshev distance
inline constexpr double kWEoHunt = 3.0;       // ... a step of its BFS distance is worth this
inline constexpr double kEoDeadPen = 20.0;    // ... a dead-end tile costs this
#endif
#if BC_QUEEN_GROWUP
inline constexpr int kGrowCheck = 100;        // BC_QUEEN_GROWUP: her turns before this round decide
inline constexpr double kGrowShare = 0.10;    // ... mode 1: this share of them with <= 1 free side
inline constexpr int kGrowUntil = 200;        // ... she eats first and is left her food until this round
inline constexpr int kGrowUpTo = 10;          // ... or until this long
inline constexpr int kGrowAskVal = 128;       // ... her SELF fact's val (her length, < kGrowUpTo while she asks) carries this bit then
inline constexpr int kGrowHeardAge = 10;      // ... teammates act on her ask this many rounds
#if BC_GROWUP_FEED
inline constexpr int kGrowFeedMaxLen = 6;     // ... BC_GROWUP_FEED: teammates up to this long feed her
inline constexpr int kGrowFeedKeep = 8;       // ... while the team has more units than this
#endif
#endif
#if BC_MN_VALUE
inline constexpr int kValueEvery = 4;         // BC_MAPNET_VALUE: the scan of the heat sectors' known beds, every this
                                              // many rounds
inline constexpr int kValueMinBeds = 2;       // ... a sector worth telling: this many known beds or more
inline constexpr int kValueAge = 100;         // ... relayed while this fresh
inline constexpr int kValueHeldAge = 30;      // ... its held-by-us bit counts while this fresh
inline constexpr int kValueHeldR = 3;         // ... held locally: one of our heads within this (Chebyshev) of its anchor or in its sector
inline constexpr size_t kValueMaxRooms = 64;  // ... the table's size
#ifndef BC_MN_VALUE_W
#define BC_MN_VALUE_W 0.15     // kWValue overridable by a define (for parameter sweeps); 0.15 = the default
#endif
inline constexpr double kWValue = BC_MN_VALUE_W;  // ... the pull to the best area, x min(beds, 6) / 6: a calibration
                                                  // knob, as wExploreFar
#endif
#if BC_FORAGE_PATH
inline constexpr int kForagePathCap = 12;     // BC_FORAGE_PATH: the heard teammate's path steps to a target, at most
                                              // this many
#endif
#if BC_HARVEST
inline constexpr int kHarvestRoomMax = 8;     // BC_HARVEST: a harvest room has at most this many tiles (and at least
                                              // 2: a 1-tile room is a trap)
inline constexpr int kHarvestGap = 3;         // ... and a bed refilling within this many rounds (the largest countdown seen, or mirrored)
inline constexpr int kHarvestIdle = 12;       // ... for a dragon with no meal for this many rounds (as BC_EXPAND's
                                              // idle): the map network's own foraging (the 2 x 2 rooms, +2 segments
                                              // and +1 dragon an entry) keeps its dragons
inline constexpr double kWHarvest = 3.0;      // ... the pull to its entry, linear to 0 at 40 steps (as the escape's wEscape)
#endif
#if BC_MN_EVIDENCE
inline constexpr int kEvidenceRoom = 33;      // BC_MN_EVIDENCE: a landing's room (World::pocket) up to this many
                                              // tiles for its known beds
inline constexpr int kEvidenceEvery = 4;      // ... the bed-room CLEARED gates refreshed every this many rounds
#endif
#if BC_QUEEN_KEEPAWAY
inline constexpr int kKeepAwayCap = 12;       // BC_QUEEN_KEEPAWAY: path steps from the nearest enemy head counted up
                                              // to this
inline constexpr int kKeepAwayAge = 5;        // ... a heard enemy head counts while this fresh
inline constexpr double kKeepAwayW = 1e8;     // ... per step: above the search values and her home bucket (1e7 a
                                              // bucket, adjacent landings <= 2 apart)
#endif
#if BC_SPRINT_TARGET & 1
inline constexpr int kGrabSlack = 2;          // BC_SPRINT_TARGET bit 1: a pearl is contested when an enemy head is at
                                              // most this many steps farther from it
#endif
#if BC_QUEEN_FEED350
inline constexpr int kQFeedFrom = 350;        // BC_QUEEN_FEED350: from this round
inline constexpr int kQFeedLen = 8;           // ... while she is shorter than this
inline constexpr int kQFeedNear = 6;          // ... feeders within this many steps of her head (the two nearest)
inline constexpr int kQFeedYield = 3;         // ... leave her the pearls within this (Chebyshev) of her head
inline constexpr int kQFeedAge = 10;          // ... her fact counts while this fresh
#if BC_QUEEN_FEED350 >= 2
inline constexpr double kQFeedEat = 2.0;      // ... (ii) her own eat weight is multiplied by this
#endif
#endif
#if BC_QFEED_SRC
inline constexpr int kQFeedHotAge = 10;       // BC_QFEED_SRC: a teammate's HOT fact counts as food while this fresh
#endif
#if BC_QFEED_PULL
inline constexpr int kQFeedPull = 6;          // BC_QFEED_PULL: inside QSK's kept landings, the one nearest a pearl
                                              // within this many steps
#endif
#if BC_QUEEN_GROWFIRST
#ifndef BC_GROWFIRST_UNTIL
#define BC_GROWFIRST_UNTIL 200   // BC_QUEEN_GROWFIRST's round limit (a define, so sweeps can override it)
#endif
#ifndef BC_GROWFIRST_LEN
#define BC_GROWFIRST_LEN 5        // BC_QUEEN_GROWFIRST's queen length limit
#endif
#ifndef BC_HUNT_PURSUE_MINLEN
#define BC_HUNT_PURSUE_MINLEN 5   // queenPursue's shortest pursuer
#endif
#ifndef BC_REGICIDE_AGE
#define BC_REGICIDE_AGE 10        // planRegicide uses an enemy-queen fact while this fresh
#endif
#ifndef BC_REGICIDE_PER
#define BC_REGICIDE_PER 8         // planRegicide's hunters = clamp(units / this, 1, 3)
#endif
inline constexpr int kGrowFirstUntil = BC_GROWFIRST_UNTIL;   // BC_QUEEN_GROWFIRST: before this round
inline constexpr int kGrowFirstLen = BC_GROWFIRST_LEN;       // ... while she is shorter than this
inline constexpr double kGrowFirstEat = 2.0;  // ... her eat weight is multiplied by this
#endif
#if BC_QUEEN_BREATHE
inline constexpr double kBreatheW = 60.0;     // BC_QUEEN_BREATHE: a teammate's step onto a side of our queen that
                                              // leaves her < 2 free sides (as kin avoidance)
#endif
#if BC_QUEEN_YIELD
inline constexpr double kYieldW = 60.0;       // BC_QUEEN_YIELD: a yielding teammate's landing within Chebyshev 1 of
                                              // her head costs this, at 2 half of it
#endif
#if BC_LONGEST_RESERVE
inline constexpr int kReserveFrom = 300;      // BC_LONGEST_RESERVE: the reserve from this round
inline constexpr int kReserveAge = 20;        // ... the LONGEST facts count while this fresh
inline constexpr int kReserveMinLen = 8;      // ... with none fresh, a dragon this long counts itself the longest
inline constexpr double kReserveEat = 2.0;    // ... its eatBonus is multiplied by this
inline constexpr int kFeedYield = 3;          // BC_LONGEST_RESERVE 3: the two nearest teammates leave the pearls
                                              // within this (Chebyshev) of its head
inline constexpr int kFeedNear = 10;          // ... feeders within this many steps of it
// BC_LONGEST_RESERVE 2: the base feeding phase (feedRound, 400 in every tuned group) from kReserveFrom; true when it
// changed p
inline bool reserveEarlyFeed(Params& p) {
    if (BC_LONGEST_RESERVE != 2 || p.feedRound <= kReserveFrom) return false;
    p.feedRound = kReserveFrom;
    return true;
}
#endif
inline constexpr int kGrowTo = 9;             // growth arm: the queen counts as short below this many visible segments
inline constexpr int kGrowYield = 5;          // ... teammates leave the pearls within this Chebyshev distance of her head
inline constexpr int kGrowSplitMin = 10;      // ... she makes no economic split below this length (half of 10 keeps 2 free steps)
inline constexpr double kQskBudget = 0.010;    // QSK may run until the search deadline + this (the hard cap is 0.100)
inline constexpr int kQueenFleeSteps = 3;       // the queen's flee takes up to this many free steps
inline constexpr double kQueenFleeMargin = 2.0;  // a 2-step flee must beat every single step's first-step score by this
inline constexpr double kQueenPaidCost = 3.0;    // ... a paid step (3-4 long queen) costs this (a pearl's eat bonus)
inline constexpr int kQueenAssumeLen = 5;    // the queen takes every enemy head as at least this long (its reach: 5 steps)
inline constexpr double kQueenZone = 3.0;    // the others' step cost next to the queen's head (cheb 1: 2/3 of it, cheb 2: 1/3)

}  // namespace bc
#if __has_include("tuned_params.hpp")
#include "tuned_params.hpp"
#define BC_TUNED 1
#endif
namespace bc {

// Params for a map group (0 pearl-poor, 1 fountain map, 2 the rest), judged online by World::mapGroup from what the
// dragon has seen: no map names are used.
inline Params paramsForGroup(int group) {
    Params p;
#ifdef BC_TUNED
    applyTuned(p, group);
#else
    (void)group;
#endif
    return p;
}

// Phases (BC_PHASED): the game's phase for the per-(group, phase) knobs: rounds 0-99 opening, 100-299 middle game,
// 300-399 consolidation, 400-499 end game.
inline int phaseOf(int round) { return round < 100 ? 0 : round < 300 ? 1 : round < 400 ? 2 : 3; }
inline Params paramsFor(int group, int phase) {
    Params p;
#ifdef BC_TUNED
    applyTuned(p, group, phase);
#else
    (void)group;
    (void)phase;
#endif
    return p;
}

// Seconds since process start. In the judge's sandbox the clock is virtual and
// advances 1 ns per CPU point spent (unswbc sandbox.py: now = spent + slept_ns),
// so the 100M-point turn budget is 0.1 s on this clock.
inline double clockNow() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Seconds of that clock the search may spend per turn: 0.075 = 75M of the 100M
// points. Test builds may override it to keep native local games short.
#ifndef BC_TURN_BUDGET
#define BC_TURN_BUDGET 0.075
#endif

// BC_FALLBACK95: the 95M hard line. With BC_FALLBACK95 1 every check point that sees this turn's metered clock past
// 95M falls back at once - the decided action, or before any search the cheapest legal step, never no action - so the flush
// (~2.5M) stays inside the judge's 100M. The turn's clock starts at its ROUND line (io.hpp readTurn), at process
// start on the first turn, as the meter does. The check points reuse the clock reads the bot already makes while the
// search runs (a new read there would shift the search budget and change play): Policy's lean test, QSK's limit,
// v10k::speak's ray guard; new reads come only after the clock-dependent work (the sonar block of the non-v10k path,
// the flush). BC_V11_TRACE 1 prints "LOG fb95 <check point>" on a fallback; BC_T13_BURN 1 (test builds only) burns
// the clock past the line at one check point a round (round % 10: 3 Policy, 5 the rays, 9 the flush) and prints "LOG
// t13burn <check point>".
#ifndef BC_FALLBACK95
#define BC_FALLBACK95 0
#endif
#ifndef BC_V11_TRACE
#define BC_V11_TRACE 0
#endif
#ifndef BC_T13_BURN
#define BC_T13_BURN 0
#endif
namespace fb95 {
inline constexpr double kLine = 0.095;
inline double t0 = 0.0;                 // this turn's metered start on clockNow()'s scale
inline char const* hit = nullptr;       // the first check point that fell back this turn (Policy reports through main)
inline char const* burned = nullptr;    // BC_T13_BURN: where this turn's clock was burnt
inline bool past(double now) { return now - t0 > kLine; }
inline double hard() { return t0 + kLine; }
inline void burn(char const* where) {   // BC_T13_BURN: spin until just past the line (work between clock reads: in the sandbox a
    volatile unsigned long long sink = 0;  // clock read is a host call, cheap in points but slow in real time)
    while (clockNow() - t0 <= kLine + 0.0002) {
        for (int i = 0; i < 4096; i++) sink = sink * 6364136223846793005ull + 1442695040888963407ull;
    }
    burned = where;
}
}  // namespace fb95

}  // namespace bc
