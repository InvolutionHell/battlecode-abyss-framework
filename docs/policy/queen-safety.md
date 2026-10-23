# Queen safety

Under rules 1.2.3, dragons 0 and 1 are the queens, and after round 500 the longer queen wins; a dead queen counts 0. In
our ladder games a queen alive at the end won 94% of the time and a dead one 32%. The final bot therefore treats the
queen's survival as a separate layer on top of the search: a set of hard rules for her own moves, a kernel that picks
her landing by safety tiers, gates that wall off dangerous portals, and "friend discipline" rules that stop our own
dragons from boxing her in. This page describes what runs in the final build; inactive queen modules are listed at the
end. Pointers are `file:line` in `bots/abyss`.

The queen is identified by id, not by team letter: a dragon is a queen if and only if its id is 0 or 1 (the engine
numbers the map's starting dragons in order and ladder games swap sides). Her process knows it from its own id;
teammates learn her id from a starter's id parity, from sight (a teammate with id < 2) or from a SELF / LONGEST fact
naming an id below 2.

## What the queen never does

These rules are part of the search and of the post-search actions:

- **no head-on trades**: a step onto an enemy head is illegal for her (policy.hpp:3423); she never sprint-trades, joins
  a squad, hunts, scavenges or takes part in QKILL / QGUARD;
- **no feeding, no risky economy**: she never sacrifices herself in the feeding window, never takes a lane dip, never
  harvests a room behind a portal, and never probes an unknown portal (`BC_QGATE`, policy.hpp:4453);
- **no voluntary reverse split**: she reverse-splits only with no legal move left (policy.hpp:1081); when trapped she
  prefers an unpaired portal (its landing is unknown, not certain death) to any other way of dying (policy.hpp:894);
- **multi-step moves only from the kernel**: `main.cpp` cuts any other multi-step move of hers to its first step
  (main.cpp:85); the R4 free steps never apply to her;
- **no split inside a small closed room**: with `BC_MAPNET_REGION`, no split while her head is in a closed room of fewer
  than 2L + 2 tiles (policy.hpp:4088).

She does split otherwise: the swarm rule halves every dragon of length 4 or more before round 400, and the queen is not
exempt when the kernel hands control back to the search (below). In practice the final build keeps her short and
protected rather than long; the attempts to grow her early are in [lessons.md](lessons.md).

## The Queen Safety Kernel (`qsk.hpp`)

The kernel runs after the search on every turn of the queen's process (qsk.hpp:548). Its budget is the search deadline +
10 M points, capped at the 95 M line - 3 M. It picks her landing by **hard tiers**; the search's own values only order
the moves inside a tier.

### Threat margins

A dragon of length L can move ceil(L/4) steps for free and then pay one segment per extra step while it keeps at least
2, so its reach in one turn is

```text
Rmax(L) = ceil(L/4) + L - 2          (0 for L < 2)       qsk.hpp: rmax()
```

The margin of a tile c is `m(c) = min over threat sources t of (steps(t, c) - Rmax(t))` (qsk.hpp:100). It is computed by
one multi-source Dijkstra pass per source set (qsk.hpp:160): every source starts at -Rmax, a step costs 1, a step onto a
seen pearl costs 0 (an eaten pearl pays for a paid step), values above 4 are not expanded (no reach cap is needed, and
the cost stays one pass). Other dragons' bodies block, except the last ceil(n/4) segments of a whole visible chain,
which will have moved before the next mover; her own body is passable (conservative). m >= 1 means no known threat can
reach the tile this round.

**Basic sources** (qsk.hpp:184):

- visible enemy heads. A whole visible chain of n segments has reach Rmax(n). A chain cut by her 7x7 window is given the
  length max(n + 1, longest enemy known, 6), or an unbounded reach when no enemy length is known yet;
- the tail tip of a whole enemy chain of 4 or more: a split child of length n - 2 moves in the round it is born, from
  the old tail;
- a headless fragment within Chebyshev 2 of her head: its unseen head is assumed max(n + 1, 6) - 2 long;
- enemy heads seen 1-3 rounds ago and out of sight now: Rmax(max(L, 4)) plus ceil(L/4) per round of age;
- portal mouths within 4 of her head: reach 2 (qsk.hpp:274). A dragon of either team that comes through blind lands
  beside the edge and may take a free second step.

**Strict sources** (used by tiers 1 and 2 only) add two more:

- body-derived heads: a split can turn segment j of an enemy chain (2 <= j <= L - 4) into a head of length L - j - 2 in
  the same round (qsk.hpp:210);
- fog: tiles within 6 of her head that were in view neither this round nor last round and lie outside sectors our side
  holds, as heads of max(longest enemy known, 6). Fog counts only while enemies are about (her heat sector is
  enemy-held, or an enemy head was seen or heard within 10 rounds and Chebyshev 8; qsk.hpp:295). Fog everywhere froze
  her in the middle of her view without eating, so it is conditional.

### Landings

Her candidate moves are a time-expanded BFS of her own steps inside this turn's 7x7 window (qsk.hpp:310): visible tiles
only, no other dragon on them, not a cell of her body that has not moved away yet (her tail leaves one cell per step
that eats nothing), no step back onto her own path, and a portal only as the last step. Free landings use 1 to ceil(L/4)
steps; paid ones are looked at only when the free tiers are empty. For each landing the kernel computes her new body,
its room (free tiles reachable with that body as a wall, counted up to L + 8) and its exits (free neighbours).

### Tiers

| tier | condition | used when |
|---|---|---|
| T1 | strict margin >= 1, no visible enemy head within Chebyshev 3, room >= L + 8, 2+ exits, no head next to the landing | some free landing qualifies |
| T2 | strict margin >= 1, room >= L + 8, 2+ exits | no T1 |
| T3 | basic margin >= 1, room >= L + 2 | no T1 / T2 |
| T4 | basic margin >= 1 by paying 1 segment (only from length 4, so she keeps at least 3), then the larger margin; if no paid landing qualifies, the SPLIT 2 "hold" (she stays, a 2-long child takes her last segments; her tile must have margin >= 1, she must keep 4 and the team must be below the unit limit) | no free landing qualifies for T1-T3 |
| T5 | the free landing ranked best by the calibrated margins (EXITFIX, below), else the largest basic margin; paid landings and the hold only when no free step is legal | nothing is safe under the kernel's model |
| T6 | no landing at all: the search's trapped handling (portal first, then the boxed reverse split) | |

Tier conditions are at qsk.hpp:593, T4 at qsk.hpp:676.

**Inside the best of T1-T3** the kernel keeps the landings with the best strict margin (capped at 4), and among their
1-step landings that the search also found legal it maximises

```text
v = search total of that direction (priors included)
  + 1e8 x path steps from the nearest known enemy head, capped at 12        (KEEPAWAY)
  + 1e9 if the landing eats                                                 (GROWFIRST, before round 200, L < 5)
  + 5e8 + 5e7 x (6 - steps to the nearest food), if food is within 6 steps  (GROWFIRST)
```

so, in effect, eating comes first, then food close by and distance from the enemy, then the search's own value. When no
such 1-step landing exists, the tier's best landing (multi-step ones included) is taken by (strict margin, basic margin,
pearls eaten, fewer steps). If the pick is the search's own single step in tier 1 or 2, the kernel hands control back to
the search so that the post-search rules (a bud, for example) can still apply; any other pick is returned at once.

## Landing filters

### EXITFIX: calibrated threat lengths at T5 (`BC_QUEEN_EXITFIX` 1)

In tier 5 every landing is inside some threat's reach under the kernel's model, which gives every chain cut by her
window the length max(visible + 1, longest known, 6). Measured over all queen turns of 321 ladder games, cut chains are
much shorter: with 1 visible segment the true length was 2 or 3 in 91% of cases before round 300 (6 or more in 1-2%),
with 2 visible segments 3 or 4 in 93% (6 or more in 4-9%); only from 3 visible segments on is 6 a fair guess. With every
landing inside a phantom reach, the old T5 took the first-listed largest margin and often walked into a short hunter.

EXITFIX (qsk.hpp:713) runs three extra margin passes at T5 only:

- **certain**: whole visible chains (their length is known);
- **low**: a cut chain is max(n + 1, 3) long, a remembered head as seen;
- **mid**: a cut chain with 1-2 visible segments before round 300 is max(n + 2, 4) long; otherwise the kernel's own
  assumption.

A free landing is ranked by: reachable by no certain threat (only when the certain threats tell the landings apart),
safe under the mid lengths, safe under the low lengths, room >= L + 2, the larger low margin, the larger basic margin,
exits, pearls, fewer steps. A landing that is neither certain-safe nor low-safe is not a candidate; with no candidate
the kernel's plain T5 choice stands. On a counterfactual over 7,720 tier-5 queen turns with an exit, the old rule landed
inside a true reach 60 times and EXITFIX 25 times; where the two differed, EXITFIX was safe and the old rule not in 35
turns, and the reverse in 0.

### NODEAD (`BC_QUEEN_NODEAD` 1)

A dead-end tile (`Board::deadEnd`: tree-like branches found by repeatedly removing tiles with one way on) is a trap for
a short queen: she cannot turn round in it and a 2-4 long dragon has no reverse split to spare. While she stands on an
open tile, every landing on a dead-end tile is dropped unless nothing else is legal (qsk.hpp:361). On an earlier map
set, 35 of 35 queen entries into such lanes were kernel landings and 33 of them died within 10 rounds.

### BREATHE (`BC_QUEEN_BREATHE` 2), her side

Most queen deaths on her own move were box-ins: in one sample of 535 games, 165 such deaths, all boxed, 116 with a
teammate among the blockers. The kernel (qsk.hpp:397) counts a landing's free sides with every body and tail as blocked
(the engine checks the destination before the tail moves) and her new body too:

- while some landing has 2 or more free sides and a basic margin of at least 1, landings with fewer free sides are
  dropped;
- if none has, and she has had at most one free side for 2 turns running (policy.hpp:950), only the landings with the
  most free sides, then the most room, are kept, even into a richer enemy reach.

## Portal gates (`world.hpp`)

A paired portal is a wall in a dragon's adjacency table (`Board::nb`; the edge itself, vision, the mirror and PORTAL
facts are untouched) when its landing is unsafe (world.hpp:366):

| gate | who | unsafe landing |
|---|---|---|
| `BC_DEADPORTAL` 1 | every dragon | a known landing (seen or mirrored) with no open edge: a 1-tile room whose only side is the way in |
| map network (`BC_MAPNET`) | every dragon | the gate's state is DEAD or TRAP, or its portal id is in the shared trap set ([mapnet.md](mapnet.md)) |
| `BC_QGATE` 1 | the queen | also a landing never seen nor mirrored, one with fewer than 2 open edges, a dead-end tile, or a small closed pocket (fewer than max(6, L + 2) tiles, all known, no unpaired portal) |

The queen's gate has one exception (`BC_MAPNET_REGION`): a known closed room with 2 or more beds that she fits in (size
>= L + 1, at least one open edge, not DEAD or TRAP) stays open, so she can use bed rooms (world.hpp:396). The gates are
re-applied whenever the board's edges change, a portal landing comes into view, the trap set changes, or (for her) her
length changes. The dead-end map keeps the values computed before gating, so a gate never turns her own surroundings
into a "dead end".

This layer came from games on portal-heavy maps where 1-tile decoy rooms behind portals killed tens of our dragons a
game, the queen among them.

## Friend discipline

Our own dragons were a large share of the queen's blockers. Four rules act on teammates whose view contains her head:

| rule | effect | code |
|---|---|---|
| ROOMGUARD (`BC_QUEEN_ROOMGUARD` 1) | a first step within Chebyshev 2 of her head that leaves her fewer than max(visible length, 2) + 2 free tiles reachable costs 30 | policy.hpp:3757 |
| BREATHE bubble (`BC_QUEEN_BREATHE` 2) | every first step into the 8 tiles around her head costs 60 | policy.hpp:4239 |
| BOXGUARD (`BC_QUEEN_BOXGUARD` 1) | when she has exactly one free side, no teammate ever takes it, not even as its own last way out when trapped: it dies elsewhere instead | policy.hpp:109 |
| last-exit rule | a teammate's only free exit is a wall for every other dragon (for all teammates, not only the queen; the early king is exempt) | `buildBase` |

Two more rules protect her indirectly: the R4 free steps of other dragons never pass within Chebyshev 2 of her head or
over her last free side (policy.hpp:2473), and `BC_UNIT_SLOT` keeps the last unit slot free for her SPLIT 2 hold: no
other dragon splits at 63 units (main.cpp:100).

Teammates also defend her actively: QGUARD trades with or blocks an enemy head that moves after them and can reach her
this round (see [endgame.md](endgame.md)).

## GROWFIRST and KEEPAWAY

- **GROWFIRST** (`BC_QUEEN_GROWFIRST` 1, policy.hpp:102): before round 200, while she is shorter than 5, her eat weight
  in the search is doubled and the kernel's in-tier order puts a landing that eats first, then the landing nearest to
  food within 6 steps. Food here includes sources out of her view: teammates' HOT facts heard within 10 rounds, beds
  whose tracked countdown ran out unseen, and never-seen mirrored beds whose refill gap has passed. A short queen was
  being traded one for one by short hunters in the opening; growing to 5 doubles her free steps.
- **KEEPAWAY** (`BC_QUEEN_KEEPAWAY` 1, qsk.hpp:475): among the landings the kernel keeps (her best tier, best strict
  margin), prefer the one farther by path (walls and paired portals, bodies ignored) from the nearest known enemy head
  (in view, or heard within 5 rounds), counted up to 12 steps. Above margin 4 the kernel did not price distance at all;
  on one small map our queen stood 5.5 steps on average from the nearest enemy head, the opponents' queens 10.4.

## Inactive queen modules

| switch | value | what it would do |
|---|---|---|
| `BC_QUEEN_KING`, `BC_QUEEN_SPLIT`, `BC_QUEEN_ZONE`, `BC_QUEEN_FEED` | 0 | queen is the king from round 0, splits only by an opening bud, the others keep clear of her and leave her food, all feeding goes to her |
| `BC_QUEEN_SAFE` | 0 | an older safety model inside the search (sprint reach, safe room, dead-end veto); the kernel replaced it |
| `BC_QUEEN_FLEE` | 1, shadowed | a 2-3 step flee search; compiled, but its call site is reachable only with the kernel off and `BC_QUEEN_SAFE` on |
| `BC_QUEEN_GROW`, `BC_QUEEN_GROWUP`, `BC_GROWUP_FEED`, `BC_QUEEN_TINY`, `BC_QUEEN_FEED350` | 0 | growth arms: food-first while short, a growth trigger after a boxed opening, feeding her by sacrifice, a 2-long queen, late feeding from round 350 |
| `BC_QUEEN_HOME`, `BC_QUEEN_ESCORT`, `BC_QUEEN_MODES`, `BC_QUEEN_KINAVOID`, `BC_QUEEN_SLACK`, `BC_QUEEN_YIELD` | 0 | home-side preference, escorts, LOCK / DESPERATE modes, no step next to her, a hard slack filter, yielding teammates |
| `BC_QUEEN_BOXGUARD` bit 2 | off (value 1) | drop her one-exit tier-3 landings whose exit has a teammate's head next to it |
| `BC_QSK_PATHHEAT` | 0 | "enemies about" by path steps instead of Chebyshev distance |

## Known limitations

- Tiers 1-3 still give a cut chain the length max(visible + 1, longest known, 6); only tier 5 uses the calibrated
  lengths. A queen keeps preferring landings that are safe under the phantom model, which is wasteful when the hidden
  chains are short.
- Portal mouths within 4 of her head are reach-2 threat sources whatever is behind them, which can keep her out of
  fountain rooms reachable only through portals.
- She is not exempt from the swarm's halving before round 400 (see above), so her length at round 500 comes mostly from
  the late game.
- Pearl-extended sprints (a pearl on a paid step pays for it) and split children beyond the kernel's model still kill
  queens that took a landing the kernel rated safe.
