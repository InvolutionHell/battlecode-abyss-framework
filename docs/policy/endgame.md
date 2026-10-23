# End game

A game ends early by elimination; otherwise, after round 500, the longer queen wins (a dead queen counts 0), then the
longest dragon, then total length. The end-game rules therefore do three things: keep our queen alive
([queen-safety.md](queen-safety.md)), concentrate length in one long dragon for the second tie-break, and kill the enemy
queen whenever a dragon of ours can reach her, because a living queen beats a dead one whatever their lengths. Pointers
are `file:line` in `bots/abyss`.

## Timeline

| round | rule |
|---|---|
| 0-199 | GROWFIRST: our queen eats first while she is shorter than 5 |
| 200 | the early king appears; LONGEST facts go out early in every message list |
| 300 | the longest reserve and its two feeders; sonar every turn; late hunts with 40+ units |
| 350 | dragons of length 10 or more reverse-split only when (nearly) boxed in; the one-king rule is enabled |
| 400 | no more voluntary splits (the swarm, `lateNoSplit` and the feeding round all end them; a boxed-in dragon may still reverse-split); danger weights x2; the feeding window opens |
| 490 | the feeding window closes (a later sacrifice could not be eaten in time) |

## Who keeps length

### The early king (`BC_V9_EKING` 1, from round 200)

"The king" is decided by every process alone from the LONGEST facts (policy.hpp:2548): a dragon is the king when no
teammate it has heard of recently is longer (ties: the lower id) and it is at least 8 long. Nobody is pre-selected. From
round 200 the king (policy.hpp:293):

- keeps every segment (no split, except the survival rule with very few units) and reverse-splits only when the best
  score is below -996;
- looks further for food (its discount is 0.8 instead of 0.7) and needs at most 12 free tiles for a step (a crowd of our
  own is not a trap for it);
- plays safe: danger x3, and a cost for every enemy whose sprint reach covers its landing;
- does not give way: the "never take a teammate's last exit" rule does not bind it;
- may fill the last two unit slots, which every other dragon leaves free from round 200;
- when it speaks, sends its SELF fact on all four rays.

The other dragons that see the king (policy.hpp:324) leave it every pearl within Chebyshev 3 of its head (their value
and eat weight are 0 there) and pay 1.5 x (3 - distance) / 3 for ending a step within 2 of its head. From round 200
every dragon also puts the LONGEST fact early in its message list, ahead of the rotating facts, so feeders and the
reserve rule know who the king is.

The king can be the queen if she is the longest dragon; nothing else in this build makes the queen the king.

### The longest reserve (`BC_LONGEST_RESERVE` 3, from round 300)

From round 300 the team's longest non-queen dragon is the **reserve** (world.hpp:262, applied at main.cpp:68): its own
length against the LONGEST facts fresh within 20 rounds (ties: the lower id); with no fresh fact, any dragon of length 8
or more counts itself the reserve. The reserve:

- eats first (its `eatBonus` is doubled);
- never splits, never takes a lane dip, and reverse-splits only below -996, like the king;
- gets two feeders (policy.hpp:164): a non-queen dragon within 10 BFS steps of the reserve's head, with fewer than two
  other heads of ours (seen, or heard within 10 rounds) nearer to it, leaves the reserve every pearl within Chebyshev 3
  of its head.

Mode 3 replaced two earlier modes. A plain "the longest does not split" rule (mode 1) changed nothing measurable,
because by round 300 the team usually held 62 of its 64 units and the unit cap already blocked splits; starting the
feeding phase at round 300 (mode 2) grew the longest faster for 50 rounds, then starved the army and lost a paired smoke
5-19.

### Late reverse protection

From round 350 a dragon of length 10 or more reverse-splits only below -996, i.e. only when it is (nearly) boxed in
(policy.hpp:1075). Long dragons used to reverse-split in crowds of their own teammates in the last 100 rounds and shed a
large part of their length within a few rounds.

## The feeding window: one king (rounds 400-489)

The top teams concentrate their length into one dragon at the end. In a study of top-ranked teams' ladder games, teams
that ended with a single king won 70-80% of their full-length games, against about a third for our endings with two or
more kings. The final build uses a one-king feeding rule (policy.hpp:642). It is enabled from round 350 (`oneKingFrom`),
but sacrifices only happen inside the feeding window, from `feedRound` 400 to `feedStop` 490, so in practice it governs
all end-game feeding:

1. The king is the dragon named by the LONGEST facts (the longest teammate heard of). If that is the dragon itself, it
   feeds nobody.
2. A non-queen dragon that sees the king's head, with the king at least as long as itself by visible segments (an
   equally long king with a higher id does not count), walks to it (pull 1.55 x 0.7^d) and, once its own head is within
   one step of a tile next to the king's head, steps into its own neck and dies there (policy.hpp:668). Its ceil(L/2)
   pearls land along its body, next to the king. Unlike the older feeding rules, any length feeds the king, not only
   short dragons.
3. A dragon that does not see the king but heard of a longer one within 20 rounds walks toward where it was heard
   (policy.hpp:674); the sacrifice waits until the head is in view.
4. Nobody feeds in a sector the enemy holds (its heat there is at least 0.5 and above ours): the enemy would eat the
   drops (policy.hpp:484).
5. Our queen never feeds anyone. She is fed only if she happens to be the longest dragon.

Feeding is not throttled by the drops already lying around the king in this build (the throttle exists behind
`BC_V9_KFEED`, which is off).

### Inactive end-game code

| code | why it does not run |
|---|---|
| the final push (every short dragon walks to the king from `finalFrom`) | `finalFrom` is 500 in every context |
| the late-king module with a feeding stream and saturation rules | `BC_V9_KING` 0 |
| the first king-feeding rule (feed the longest teammate heard of, medium dragons too) | `kingOn` 0 in every context |
| feeding the queen (`BC_QUEEN_FEED`, `BC_QUEEN_FEED350`), growing her (`BC_QUEEN_GROW`, `BC_QUEEN_GROWUP`) | switches 0 |
| the king library (`v10k.hpp`, `king/`: crown, hunger, feeding protocol) | `V10K_R0` = 501, after the last round |

## The queen's own length

Under the queen rule our queen's length is the first tie-break, yet the final build has no rule that feeds her (she is
fed only when she happens to be the longest dragon, like any king). It protects her and lets her grow by eating:

- GROWFIRST ([queen-safety.md](queen-safety.md#growfirst-and-keepaway)): before round 200, while she is shorter than 5,
  her eat weight is doubled and her kernel prefers landings that eat or lead to food;
- she swarm-splits like any dragon of 4 or more before round 400 when the kernel hands control back to the search;
- from round 400 there are no more voluntary splits (a boxed-in dragon may still reverse-split), and she eats like any
  dragon.

Versions that made her the king from round 0 and sent all feeding to her cost the army more than her length gained; late
feeding variants from round 350 measured flat ([lessons.md](lessons.md)).

## Hunting

### Late hunts (`BC_V9_FINISH` 1)

From round 300, with 40 or more units, a free dragon of length 8 or less (not the king, not feeding, not already
attacking an enemy in view) is pulled (2.0 x 0.85^d) toward the nearest enemy head seen or heard within 6 rounds
(policy.hpp:3359). A big late army that only counted length lost games it could have won by elimination.

### The enemy queen (`BC_QUEEN_QKILL` 1, `BC_QUEEN_HUNT` 1)

**Tracking.** Every dragon records the enemy queen (an enemy with id < 2) when it sees her, and a witnessed death (she
was in view within Chebyshev 2 last round and no part of her is in view now) becomes a "tomb". Sightings within 10
rounds and tombs within 40 rounds go out early in every message list; a sighting this turn or a tomb within 2 rounds
also breaks the quiet opening.

Three strikes run before anything else in `decide` for every dragon with id 2 or more:

- **QKILL** (policy.hpp:1425): if the enemy queen's head is in view and reachable this turn by a path over free tiles
  that does not pass within Chebyshev 1 of our own queen's head, using free and paid steps (up to ceil(L/4) + L - 2
  steps), the dragon moves onto her head. Both die; no trade-value gate applies, because her death decides round 500.
- **QGUARD** (policy.hpp:1444): if our queen is in view and an enemy head that moves after us this round (a larger id)
  can reach her head this round (its reach is computed for at least length 4, and for at least the longest enemy known,
  at least 6, when its body may extend beyond our view), the dragon strikes that head if it can reach it; otherwise it
  moves, on its free steps, onto a tile of the striker's shortest path to her, nearest to the striker.
- **Pursuit** (policy.hpp:2133): with the enemy queen in view but out of strike reach, a dragon of length 5 or more (2
  or more free steps) makes a free multi-step move that brings its head at least 2 steps nearer to her, never within
  Chebyshev 1 of our queen.

**Regicide** (policy.hpp:2173): the k dragons of ours with the shortest ETA to the enemy queen's last known head (seen
or heard within 10 rounds) hunt her, k = clamp(units / 8, 1, 3). ETA is path steps divided by free steps per turn;
visible teammates count by their BFS fields, teammates heard within 2 rounds by torus distance plus age. Every dragon
computes the same ranking, so the hunters emerge without messages. A hunter stops walking to feed, is pulled 3.0 x
0.85^d toward her, and when her head is in view she becomes its attack target. This planner replaces the older "slay the
enemy's longest dragon" planner, which stays compiled but is never called.
