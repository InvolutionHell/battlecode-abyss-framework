# World model and the map network

Each dragon is a separate process: a split child starts with no memory and inherits nothing from its parent. What a
dragon knows comes from two sources only, its own 7x7 view and its teammates' sonar. This page describes the per-process
world model (`world.hpp`, `board.hpp`), the sonar fact protocol (`comms.hpp`) and the shared map network built on it
(`BC_MAPNET`, `region.hpp`). Pointers are `file:line` in `bots/abyss`.

## The world model, from vision

`World::observe` (world.hpp:823) runs first every turn.

- **Terrain.** Every edge in view is recorded as open, kelp or portal. Portal ends are remembered by portal id and
  paired once both ends are known (seen, heard in a PORTAL fact, or proven by a teammate's CLEARED landing;
  world.hpp:1336). An unpaired portal is a wall for path finding. `Board::rebuild` recomputes the step table and the
  dead-end tiles (tree-like branches, found by repeatedly removing tiles with one way on; an unpaired portal counts as a
  way out).
- **Pearl beds.** A tile that shows a countdown is a bed. The countdown is tracked while out of view (world.hpp:302);
  when it runs out unseen, a spawn is assumed. The largest countdown ever seen on a bed estimates its refill gap (a
  countdown right after a spawn is the whole gap); a bed is **hot** (a fountain) once its countdown was seen 3 times and
  never above 10 (world.hpp:1366; `hotGap` of `kParams`, the tuned value 20 is not used here).
- **Pearl belief** (world.hpp:316): 1 or 0 in view; a pearl seen earlier decays by 0.943 a round; an assumed unseen
  spawn by 0.993 a round. A never-seen tile holds a pearl with probability (bed share of the tiles seen) x min(1, round
  / mean refill gap) x 0.2 (world.hpp:123).
- **Mirror symmetry.** Every map is symmetric (left-right, top-bottom or point), and a dragon is not told which. Each
  candidate stays alive while every pair of seen tiles that are mirror images matches (bed or not, all four edges); one
  mismatch rules it out (world.hpp:1224). A starting dragon also rules out a candidate that would put the mirror of its
  own starting body, visibly free, in view (world.hpp:1259). Eliminations are exact, so teammates pool them through SYM
  facts. Once one candidate is left, everything seen is copied onto the never-seen mirror tiles: beds, fountain gaps and
  kelp or open edges, but not portals (world.hpp:1307). Mirrored beds share one countdown with their twin, which the bed
  clock and the opening expansion use.
- **Other dragons.** Occupied tiles, heads, facings and visible lengths; the longest enemy seen recently; the enemy
  queen's last position and the witnessed deaths of both queens (a queen seen within Chebyshev 2 last round, with no
  part of her in view now; world.hpp:230).
- **Deaths.** A pearl that appears on a tile seen empty last round, where a dragon stood or which is no bed, is a drop
  (world.hpp:864). Drops feed the map network's danger layer.
- **Coverage.** The map is cut into 9 x 5 sectors; a sector is explored once half its tiles have been in someone's view
  (world.hpp:60). A coarser 5 x 4 grid holds a "heat" of our and enemy heads seen or heard, decaying by 0.85 a round; a
  sector at 0.5 or more counts as held (world.hpp:153).
- **Own body.** The visible contiguous prefix is authoritative, older tracking fills the rest; after a step through an
  unpaired portal the old head is kept as the neck.

From these features `World::mapGroup` picks the parameter group (fountain map, pearl-poor, other) without ever using a
map name ([search-core.md](search-core.md#parameters-map-group-and-phase)).

## The sonar protocol

Protocol 3 lets a dragon send up to four 64-bit SONAR rays a turn, one per direction; a ray travels in a straight line
and reaches whoever is in that row or column (sonar crosses portals, vision does not). In replays roughly a third to a
half of all rays end on a teammate, so straight rows and columns are a real channel. Every dragon sends what it knows
and relays what it heard, so facts spread over several hops.

### Message format and authentication

```text
message  = [16-bit tag][48-bit payload]
tag      = top 16 bits of mix(key(team) ^ mix(payload ^ (send round << 48)))      comms.hpp: tag()
payload  = type(3) x(6) y(6) val(8) aux(12) round(9)      (low bits first; the plain fact types)
```

The key is a fixed 64-bit constant combined with the team letter, so each side of a game uses a different key. During
the contest the key was private, so an enemy could read the rays that hit it but could not forge ours; with this release
the key (comms.hpp:69) is public, so a bot that reuses this protocol must change the key constant (and the same constant
in `rl/runtime/f2_comms.hpp`, which decodes the bot's rays). The inactive king library authenticates its own messages
with kK1, kK2 and kSalt (king/proto.hpp:20-22); change those too if you enable it. A replayed message goes stale after
one round, and in a mirror match the other side's copy of the bot rejects our messages. A receiver accepts a tag
computed for this round or the previous one (a teammate with a lower id sent it earlier this round, a higher id last
round), and widens the 9-bit round field using the current round (comms.hpp:68, comms.hpp:111).

### Fact types

| type | payload | meaning |
|---|---|---|
| SELF (0) | head, val = length, aux = id | the sender itself, every time it speaks |
| ENEMY (1) | head, val = visible length, aux = id, round seen | the freshest enemy head the sender knows; with val 0 and an id below 2, the enemy queen's tomb |
| HOT (2) | tile, val = refill gap | a fountain |
| LONGEST (3) | head, val = length, aux = id, round heard | the longest teammate the sender has heard of (with val 0 and an id below 2: our queen's tomb) |
| PORTAL (4) | edge tile, val = orientation, aux = portal id | a portal end, so teammates pair portals without seeing both ends |
| TEAM (5) | leader's head, val = team size, aux = leader id | a local team's leader ([search-core.md](search-core.md#team-tactics-inside-the-value-function)) |
| EXPLORED (6) | 45-bit sector bitmap | the sectors the sender knows to be explored |
| SYM (7, sub 0) | 3 bits | the symmetry candidates the sender has not ruled out |
| HEAT (7, sub 1) | 20 + 20 bits | the 5 x 4 sectors where the sender knows our / enemy heads lately |
| DROPS (7, sub 2) | tile, count, round | fresh drops for scavengers (not sent: the scavenger is off) |
| EKING (7, sub 3) | head, length, id, round | the longest enemy dragon known |
| GATE (7, sub 0, ext 1) | tile stepped from, val = dir \| state << 2, aux = landing or prober id, round | a portal gate state (below) |
| REGION (7, sub 0, ext 2) | anchor tile, val = size class \| status << 3, aux = beds \| exits << 4 | a closed room seen whole; with aux bit 11 set, a VALUE area |
| TRAPSET (7, sub 0, ext 3) | 38-bit mask | the portal ids whose outside end leads into a dead room |

The map-network types ride on the SYM type's sub-0 slot with a 2-bit extension field, so the plain SYM fact (extension
0) and the new ones decode unambiguously (comms.hpp:73).

### What a dragon receives

`World::hear` (world.hpp:978) decodes every message, drops foreign or stale ones and updates the knowledge tables
(`comms::Knowledge`, comms.hpp:165): teammates (24 entries), enemy heads (24), fountains (64), local teams (16) and the
longest teammate. Entries older than 20 rounds are dropped (fountains are kept). A heard fountain marks the bed and its
mirror as hot; heard SYM eliminations narrow the symmetry; heard HEAT keeps far sectors warm; heard PORTAL ends pair
portals; heard EXPLORED bits mark sectors explored. The longest teammate is whoever has the greatest length among SELF /
LONGEST facts and sightings; the newest fact about the same dragon replaces the older one. (A variant that made the
LONGEST fact always name the queen exists behind `BC_QUEEN_FEED`, which is off.)

### What a dragon sends

`World::tell` (world.hpp:1087) builds up to four facts in this order:

1. SELF;
2. urgent map-network facts: a probe it is about to make (CLAIM) or making (PROBING), its safe landing (CLEARED), a
   closed room seen whole for the first time (REGION), a new VALUE area, and the trap set;
3. the enemy queen's position (seen within 10 rounds) or her tomb, and our queen's tomb;
4. TEAM, if it leads a local team of 2 or more;
5. the freshest enemy head heard of within 8 rounds;
6. from round 200, the longest teammate (LONGEST), ahead of the rotation;
7. a rotation, starting at a different kind each round: LONGEST, a fountain, a portal end, the explored sectors, one
   relayed gate state or VALUE area, the symmetry candidates, the heat bitmap, and from round 250 the longest enemy.

Fact i goes out in direction (i + round) mod 4 (main.cpp:230), so the direction each fact takes rotates from round to
round. On the turn a dragon steps into an unpaired portal its PROBING fact is put on the ray that leaves from its tail,
back through the portal toward the team; the three rays from the head may be walled in by a 1-tile room (main.cpp:219).
The early king, when it speaks, sends its first fact (SELF) on all four rays.

**Quiet opening** (`BC_QUIET`, `sonarFrom` 300; main.cpp:160). Before round 300 a dragon sends nothing unless something
has to reach the team now: a probe announced, a landing cleared, a room seen whole, a new VALUE area, a non-empty trap
set (every turn for 10 rounds after it changed, every third turn per dragon otherwise), or the enemy queen seen this
turn or a queen's death witnessed within 2 rounds. Even then only position and map-network facts go out (SELF, ENEMY,
LONGEST, PORTAL, GATE, REGION, TRAPSET). Replays of strong teams show little sonar before round 300 and much
more after it, and in our sweeps of `sonarFrom` early talking won on some maps and lost on others. From round 300 every
dragon sends every turn.

## The map network (`BC_MAPNET` 1)

The map network turns individual discoveries about portals and rooms into team knowledge, so that each trap is paid for
once per team instead of once per dragon.

### The gate table

`RegionMap` keeps a state for every (tile, direction) step through a portal (region.hpp:451):

| state | set by | effect |
|---|---|---|
| CLAIM | a prober within 2 steps of an unpaired portal it has chosen | nobody else probes it for 10 rounds; never read as a trap |
| PROBING | the prober, on the turn it steps in | the start of the trap inference clock |
| CLEARED | the prober, on its first turn after a safe landing (open edge, not a closed pocket of fewer than 3 tiles), with the landing tile | pairs the portal for every teammate who hears it |
| DEAD | anyone who sees (or mirrors) a landing with no open edge | a wall for every dragon |
| TRAP | inferred from a PROBING that no CLEARED followed (below) | a wall for every dragon; forgotten after 60 rounds |

A stronger state replaces a weaker one (CLEARED > DEAD > TRAP > PROBING > CLAIM), the same state only refreshes its
round, and once the symmetry is known every gate state is also written for the mirror-image step. Each dragon relays one
known gate state per rotation slot, round-robin, skipping the short-lived CLAIM and PROBING (world.hpp:779).

**Probing once per team.** A dragon picks a probe target only among unpaired portals whose gate state is unknown and
that are not in the trap set (policy.hpp:4419), and announces CLAIM when it is within 2 steps. The queen never probes.

### Evidence grading (`BC_MAPNET_EVIDENCE` 2)

A prober that dies in a 1-tile room loses its own rays (a dragon that dies in its own turn sends nothing), so a TRAP can
only be inferred from silence. Inferring it from every unanswered PROBING was wrong most of the time on a portal maze
(63 of 84 inferred traps were doors of food rooms). With evidence grading (world.hpp:511):

- the PROBING carries the prober's id;
- after 12 rounds without a CLEARED (level 2 waits 12 instead of 6, so relayed facts have time to arrive), it becomes a
  TRAP only if the prober was neither heard nor seen after the probe and the landing, when known, is in no known bed
  room (a closed pocket of up to 33 tiles with a bed); otherwise it lapses to unknown (region.hpp:470);
- an inferred TRAP is forgotten after 60 rounds unless confirmed again;
- every other relay turn, a CLEARED gate whose landing is in a bed room is relayed first (the food doors), refreshed
  every 4 rounds.

The three levels of the switch: 0 infers a TRAP from every PROBING left unanswered for 6 rounds and forgets it after 150
rounds; 1 adds the prober's id, the silence and bed-room checks, the 60-round expiry and the priority relay of food
doors; 2 (the final build) also waits 12 rounds instead of 6.

### The trap set (`BC_MAPNET_TRAPSET`)

Per-gate facts reach only the dragons in line of a ray; an audit found that 94% of trap deaths were dragons the per-gate
facts never reached. The TRAPSET fact carries every trap a dragon knows in one 38-bit mask of portal ids, and both ends
of a portal share its id, so one fact walls every step through that portal (world.hpp:483). Only DEAD gates (a landing
actually seen with no open edge) and heard masks enter the set: with inferred TRAPs in the mask, falsely trapped portals
were walled at both ends and a smoke test lost 28 of 40 games. While the mask is news it also forces sonar in the quiet
opening.

### Rooms and danger

- **REGION facts.** The first time a dragon sees a closed room whole (every tile known, no unpaired portal, at most 32
  tiles), it tells the team once: size class, beds, exits and a status (empty, beds, dangerous) (world.hpp:595).
- **Danger layer** (region.hpp:76): a tile where a death was seen, or the anchor of a room reported as dangerous, has
  danger 1, fading linearly to 0 over 200 rounds. The search charges 0.6 x danger for a step onto the tile, unseen tiles
  with danger above 0.3 lose their exploration value, and the VALUE and opening-expansion targets skip tiles with danger
  above 0.5.
- **Room graph** (`BC_MAPNET_ROOMS` 0, inactive): an incremental graph of rooms over the known map, computed only by
  touched tiles. No decision reads it, so it is compiled out; the decision hooks use `World::pocket`, a bounded flood
  from one tile, instead.

### VALUE areas (`BC_MAPNET_VALUE` 1)

Open maps' bed areas are not closed rooms, so the value half of the network uses the 5 x 4 heat sectors. Every 4 rounds
a dragon counts the known beds (seen or mirrored) in each sector; the first sector with 2 or more beds that it has not
heard of goes out once as a VALUE fact (a REGION fact flagged in bit 11 of aux): the anchor bed nearest the sector's
centre, the bed count, and whether one of our heads is in the sector (world.hpp:595). Every dragon keeps up to 64 areas
(newest word per sector) and relays those younger than 100 rounds on alternate relay turns (world.hpp:757).

A non-queen dragon is then pulled toward the best area none of ours holds (policy.hpp:2801): reachable, at least 2 steps
away, not in its own sector, danger at most 0.5, not reported held within 30 rounds and no teammate seen or heard within
3 of the anchor or in its sector; the score is min(beds, 6) / 6 x 0.85^d, and the pull in the value function is 0.15 x
min(beds, 6) / 6 x 0.85^d.

## Exploration

Exploration is part of the value function ([search-core.md](search-core.md#scorefrom-one-step-from-a-simulated-state)):

- a never-seen tile is worth (0.03 + P(pearl) + sector bonus) x 0.7^m, where the sector bonus applies in sectors the
  team has not explored and fades to 0 by `exploreUntil` (383, 184 or 50 rounds by map group);
- the nearest never-seen tile of an unexplored sector also pulls from afar (0.143 x fade x 0.85^m);
- known dangerous tiles are not worth exploring (above);
- a dragon of length 4 or less in a closed region (no unseen tile reachable) with an unpaired portal, whose beds feed
  our dragons there less than 0.08 pearls a round each or that has not eaten for 20 rounds, probes the region's nearest
  unpaired portal with a pull of 3.0 x max(0, 1 - d / 40) (`BC_PORTAL2`, policy.hpp:4265); one prober per portal, chosen
  by distance, ties by id;
- in a closed pocket (all tiles seen, at most 150) with paired portal exits, the shortest dragon stays as its guard and
  the others leave through the exits (pull 1.5 x 0.7^d to the nearest exit).
