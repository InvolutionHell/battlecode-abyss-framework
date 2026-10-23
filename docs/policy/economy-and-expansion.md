# Economy and expansion

The economy layer decides how the team turns pearls into units and length: when to split, which pearls and beds to go
for, how to farm dead-end lanes and fountain rooms behind portals, and how far to push out in the opening. In self-play
every elimination game went to the side that was ahead in units at round 50, so the first half of the game is a unit
race; the second half converts units into the length that decides round 500 ([endgame.md](endgame.md)). Pointers are
`file:line` in `bots/abyss`.

## Splitting: the swarm

`trySplit` (policy.hpp:4055) runs after the search and the queen's kernel. In the final build one rule makes almost
every split, the **swarm** (policy.hpp:4070):

- before round 400 (`swarmUntil`, and before the feeding round 400), every dragon of length 4 or more splits in half
  (`SPLIT L/2`: the rear half becomes a new dragon whose head is the old tail);
- also on a turn whose best step eats, also in a crowd of our own heads, also when it is a grower;
- not the early king, not the longest reserve, not a dragon leaving a pocket, not a dragon that is attacking (a squad
  attacker or a trade), and not the queen inside a small closed room ([queen-safety.md](queen-safety.md)).

Safety checks still apply to every split: no enemy head within Chebyshev 1 of our head, and both halves need a "roomy
move" with the other half as a wall (a first step from which at least max(int(1.56 n) + 1, 4) tiles are reachable;
policy.hpp:4155).

**Unit caps.** The team limit is 64. From round 200 every dragon except the early king leaves 2 unit slots free (the
king's reserve for a reverse split; policy.hpp:4062), and with `BC_UNIT_SLOT` no dragon but the queen splits at 63 units
or more, so she always has a slot for her SPLIT 2 hold (policy.hpp:222; `main.cpp` repeats the check as a backstop).

**From round 400 no more voluntary splits** (`lateNoSplit`; policy.hpp:4067); a boxed-in dragon may still reverse-split
(policy.hpp:1082).

Other split triggers exist but are redundant in this build: the networks' split triggers (RL for the queen, R4 for the
others, before round 200 when P(split) >= 0.4; [net-priors.md](net-priors.md)) require length 4 like the swarm; the
older rules (survival reproduction of 2-long children, the pocket guard's buds, grower buds, a crowd veto, late
plain-split limits by unit count) sit behind the swarm branch and matter only in corner cases, for example the early
king in survival mode.

**Reverse splits** keep a 2-long stub in place while the rest leaves through the old tail: the boxed-in escape when the
best score is below -500 (-996 for the early king, the reserve and, from round 350, dragons of length 10 or more), and
the deliberate exit of a lane dip (below). See [search-core.md](search-core.md#picking-the-move-and-trapped-handling).

## Foraging

The value function ([search-core.md](search-core.md#scorefrom-one-step-from-a-simulated-state)) prices every pearl, bed
countdown and unseen tile within the 48-step BFS horizon. The rules that spread the team are local and need no messages:
a pearl that a visible teammate reaches first (ties: the lower id) is worth 0; a fountain is skipped when a teammate is
nearer and within 10 steps of it; fountains pull from afar with a weight that depends on the map group (`wHot` 0 / 0.75
/ 1.99), and a fountain known only from its mirror image (usually on the enemy's side) pulls 1.23 times as much.

**`BC_FORAGE_PATH` (on, but inert).** It replaces the torus distance from a teammate heard out of sight to a target by
path steps over the known map (paired portals included, at most 12 steps; policy.hpp:3038). The distance is then used
only to multiply a pearl's value by `remoteYield`, which is 1.0 in every tuned context (policy.hpp:3562). The code runs
and costs CPU (which moves the search's stopping point) but does not change any value.

## The bed spawn clock (`BC_ECON_BED` 1, before round 300)

A bed's countdown is public: every dragon that sees it knows when it spawns, and mirror-image beds share one countdown.
The clock rules (policy.hpp:3546):

- **wait value**: a bed that spawns in `cd` rounds at distance `m` is worth 0.9 x 0.7^m x 0.9^max(0, cd + 1 - m) instead
  of 0.9 x 0.7^max(m, cd + 1): waiting costs 0.9 a turn, not the discount of distance. A bed 6 turns from spawning at
  distance 2 was worth 0.074 and is now worth about 0.26, so dragons go to beds early and wait next to them;
- **countdown completion** (`buildTargets`): an unseen bed's countdown is its mirror's, if the mirror is in view; else
  an estimate from its last spawn and 0.75 x its largest countdown seen (policy.hpp:2998);
- **unlock**: a pearl lying on a bed voids that bed's next spawn, so eating it earns + min(0.5, 1 / gap)
  (policy.hpp:3964);
- **no-block**: our head on a visible bed that spawns next round, without eating, voids the spawn: -1.5
  (policy.hpp:3967);
- **idle to bed** (policy.hpp:1657): a non-queen dragon of length 3 or less that has not eaten for 30 rounds heads for
  the nearest known bed (refill gap at most 150) that is not in an enemy-held sector and that no visible teammate is
  nearer to; pull 0.5 x 0.85^d.

The queen is exempt from the unlock and no-block terms and from idle-to-bed.

## Lane dips (`BC_ECON_DIP` 1)

A lane is a one-tile-wide dead-end branch (`Board::deadEnd`). A snake cannot turn round in it: it dies there or leaves
by a reverse split, which costs a 2-long stub. Top teams farm fountain lanes exactly that way. The dip
(policy.hpp:2035):

- **entry gate**: a non-queen dragon (id 2 or more, not the early king, not rebuilding) that will be shorter than 4
  after the step, with 8 to 62 units and its body fully known, may enter a lane when the lane holds at least max(2, 4 -
  L) pearls it can eat soon (in view, believed, or a bed spawning within 3 rounds; at most 12 tiles looked at), no other
  head is within Chebyshev 3 of the mouth (an enemy, or a teammate with a lower id, which goes first), no enemy can
  reach the mouth within the dip's depth + 2 turns (path steps divided by its free steps, remembered heads included),
  and the mouth is on our side of the map (nearer to our heads than to theirs);
- **inside**: the dragon may continue while the food ahead brings it to length 4; the step counts as saved by a reverse
  split, so the room veto does not stop it (policy.hpp:4010);
- **at the tip**: the lookahead values a dead end with a possible reverse split at -(2 - 0.5) x eatBonus (the stub's
  cost minus the half of its drops we expect to eat back) instead of -900;
- **exit** (policy.hpp:2091): with the head in the lane, length 4 or more, no food ahead and no bed spawning within 2
  rounds in front, it splits L - 2: the child (our old tail) walks out, the stub stays. The longest reserve never dips.

## Room harvest (`BC_HARVEST` 1)

Some maps hide their fastest beds in tiny closed rooms behind paired portals. A dragon cannot turn round in a 3-tile
room, so the room is a death trap for the usual foraging rules; strong teams were seen to farm such rooms by eating
their way in and splitting the rear out. The harvest (policy.hpp:1848 to 1925):

- **a harvest room**: the landing of a paired portal, flooded over open edges through known tiles (seen or mirrored):
  closed (no open edge onto an unknown tile), 2 to 8 tiles, the portal's outside tile not in it, with a bed refilling
  within 3 rounds (its largest countdown seen, else the mirrored gap); the door must not be DEAD or TRAP and the landing
  must have an open edge;
- **who goes**: a non-queen dragon of length 2-3, idle (no meal for 12 rounds), while the team is below half the unit
  limit, to the nearest free entry (no teammate's head in the room or beside the entry); pull 3.0 x max(0, 1 - d / 40),
  the entry needs no room and ends the lookahead line;
- **the routine**: step in only onto a pearl (an eating step keeps the tail outside); inside, eat while the next step
  eats; then split L - 2 while the tail is still outside, so the child (head = old tail, outside) takes the eaten mass
  and the 2-long parent dies inside; if trapped at 4 or more, split 2 once more.

Net yield: +1 segment per entry in a 3-tile room (3 pearls eaten, the 2-long parent lost), at no cost in dragons. The
idle gate matters: without it the harvest pulled small dragons away from 2 x 2 rooms where the normal rules already made
about +2 segments and +1 dragon per entry, and the paired smoke test lost; with it an 8-game paired smoke went 7-1 where
the baseline went 5-3 on the map that motivated it (a small sample), and the harvest never acted on maps without such
rooms.

## Opening expansion (`BC_OPEN_EXPAND` 1, gate 28)

On small maps the spawns of the first 200 rounds were falling nearer to the opponent: our dragons stayed in their start
area while the opponent took the central bed blocks. The opening expansion (policy.hpp:2891) sends non-queen dragons
outward until round 120:

- **target**: the known bed area with the most free beds. A candidate is a known bed (seen or mirrored) whose next spawn
  is at most 100 rounds away (weight 1) or unknown (weight 1/2), more than 5 tiles (Chebyshev) from our start (this
  process's first head) and from the enemy's start (its mirror image), with no other head of ours or of the enemy (seen,
  or heard within 10 rounds) within 4. Its score is min(12, weights of the candidate beds within 3) x (1 - d / max(40, W
  + H)), over reachable beds of danger at most 0.5. With no candidate, the map's centre while it is unseen;
- **pull**: 0.4 x max(0, max(40, W + H) - d), linear, so every step nearer is worth the same from afar
  (policy.hpp:3616);
- **while expanding**, the start area's beds no longer hold the dragon: their countdowns are ignored (only pearls in
  sight count there), and an idle-bed or VALUE target there is dropped;
- **the escort stays**: with our queen known (seen, or heard within 10 rounds) and at least one other teammate known,
  the non-queen nearest to her (ties: the lower id) keeps its normal rules; the queen never expands.

**The gate** (`BC_OPEN_EXPAND_GATE` 28 = bits 4 | 8 | 16; policy.hpp:2858) is decided once per process and is sticky:

| bit | condition | |
|---|---|---|
| 1 | at the first turn from round 10, at most 3 known beds within 10 steps | off |
| 4 | the team started with at most 2 dragons (UNIT_COUNT at round 0) and the map has at most 700 tiles | on |
| 8 | with bit 4: a dragon born later (it cannot know the start) on a map of at most 700 tiles | on |
| 16 | a map of at most 700 tiles, whatever the start | on |

Bit 16 contains bits 4 and 8, so in effect the expansion runs on every map of at most 700 tiles (W x H) and never on
larger ones. An ungated version had lost games on several larger open maps.

**Measured effect** (paired sandbox census, 950 games on five small official maps, both sides, the same seeds and the
same panel of sparring opponents, against the same bot without the expansion):

| KPI | with expansion | without |
|---|---|---|
| games won | 666 | 483 |
| games in which we were eliminated | 196 | 347 |
| our units at round 100 (paired difference) | +2.5 +/- 0.5 | |
| unit difference to the opponent at round 100 | +4.2 | -1.7 |
| pearls eaten by round 100 (paired difference) | +12.7 +/- 1.7 | |
| our queen alive at round 500 | 416 | 318 |
| deaths per game (all causes) | 155.9 | 128.1 |

The gain came mostly from three of the five maps (+45, +87 and +49 wins), one map was flat (-5) and one slightly
positive (+7). The expanding dragons die more often, but the economy more than pays for it. Narrower gate variants
gained less on the same games (576 and 630 wins).

## VALUE areas (`BC_MAPNET_VALUE` 1)

Throughout the game a non-queen dragon is also pulled toward the best known bed area that none of ours holds (0.15 x
min(beds, 6) / 6 x 0.85^d). The areas are shared as VALUE facts; see
[mapnet.md](mapnet.md#value-areas-bc_mapnet_value-1).

## Measured effects of the lane dip and the bed clock

In a paired census over 312 games against an older version of our own bot, the lane dip and the bed clock together won
176-136 where the same build without them went 139-173. The bed clock alone changed nothing measurable; its targets were
reached only with the dips in the build. Together they raised pearls eaten by 16% and units at round 150 by 8%.
