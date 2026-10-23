# Lessons: what did not work

Many modules were built, measured and left out of the final build, or kept in the code but switched off. This page
records the ones worth knowing before trying a similar idea. Results are paired sandbox censuses or replay analyses
unless stated otherwise; game counts are given so that small samples can be recognised. How we measured is described in
[../methodology.md](../methodology.md); the switches named here are listed in [switches.md](switches.md).

## Generality

- **No map knowledge.** Pearl spawns are seeded per match and tournament maps are unseen, so map databases, pearl
  timetables, opening books and per-map-name tuning were ruled out. Parameters are tuned per map *group*, and the group
  is judged online from what a dragon has seen (fountains, refill rate); everything about a map is learnt in play and
  shared by sonar.
- **Fast walks to beds on known maps do not transfer.** A dragon that walks straight to the fast beds in the opening
  relies on knowing the map; on an unseen map a bot has to find the beds first. What does transfer is what happens after
  arrival (pearls eaten per dragon-turn at a bed), and that is where our modules were aimed instead (the bed clock, room
  harvest, opening expansion toward known beds).
- **Per-class networks.** Networks specialised per map class (picked from geometric features of what a dragon has seen)
  were trained and wired in behind `BC_R6`, but not used: the single network trained on all maps was kept.

## Economy and exploration

- **Waiting at beds instead of arriving on time.** A module that made a dragon hold a fast bed (`BC_BED_KEEP`: pulled to
  the bed, with every target farther than 3 tiles from it worth nothing) left the dragon parked at the bed between
  spawns. A revision that pulled it to the bed only when the countdown was about to run out ("on time") was built but
  did not reach the final build. What stayed is the bed clock, which prices a bed by its countdown (a turn of waiting
  costs 0.9) alongside every other target, so a dragon near a bed still eats what is around it.
- **Contesting distant bed cores pulled dragons off their own beds.** An idle-dragon rule that sent dragons to bed areas
  none of ours held (`BC_EXPAND`) took dragons that were waiting at our own beds away from them; the share of spawns
  nearer to us fell on all four maps of its first smoke test. The opening expansion that replaced it targets free bed
  areas, only until round 120 and only on small maps.
- **Exploration triggers that lost head-to-head.** A version that probed every unpaired portal it saw early lost 39-61
  against the older version it was built on; opening scouts per unexplored sector went 41-39 in their comparison (no
  measurable gain, off); a few-unit opening mode that also let the queen cross portals aggressively lost 6 games in 40
  on the maps it targeted (the queen's part was the main cost); the opening expansion without its size gate lost games
  on several larger open maps. What stayed is narrow: hungry probes from closed, poor regions, one prober per portal per
  team, and the gated expansion.
- **Heat spread.** Pulling free dragons toward sectors nobody holds sent everyone to the midline when unexplored sectors
  counted as empty (the enemy's half is unexplored at the start); after the fix the tuning set its weight to 0.
- **Team pearl assignment.** Splitting a local team's targets greedily among its members kept teams clustered and lost
  10-30 against the previous version; membership and TEAM facts stayed, the assignment is off.
- **Room capacity.** Limiting how many of our dragons may stay in a closed room lost in a version whose probes still
  died after crossing; the tuning switched it off.
- **A late "do not split" rule for the longest dragon** changed nothing: by round 300 the team usually held 62 of its 64
  units, so the unit cap already blocked splits. Starting the feeding phase at round 300 instead grew the longest faster
  for 50 rounds, then starved the army and lost a 24-game smoke 5-19. The reserve that stayed keeps the longest whole
  and gives it two feeders, without an early feeding phase.
- **Harvest without an idle gate** pulled small dragons out of 2 x 2 rooms where ordinary foraging already made about +2
  segments and +1 dragon per entry, into 3-tile rooms that yield +1 segment at best; only idle dragons harvest now.
- **The bed clock alone** did nothing measurable; it pays only together with the lane dips.

## The queen

- **A queen-centred team.** Making the queen the king from round 0, letting her split only by an opening bud, keeping
  the others away from her head, and sending all feeding to her cost the army: in a 124-game census against an older
  version of our bot the full set went 24-100, while the minimal queen rules (no trades, no feeding, trapped portal
  first) went 62-61. Removing single parts did not rescue it (without the keep-away zone 34-90, without the feeding
  41-83). The final build protects the queen (the safety kernel, gates, friend discipline) and leaves the army's economy
  alone.
- **A 2-long "tiny" queen** that buds everything else (a small target that offers an attacker little to trade into) was
  built and rejected.
- **Feeding the queen late.** Leaving her the pearls near her head from round 350, letting her eat first, and pulling
  her toward food were built in three versions; all measured flat.
- **Growing her after a boxed opening** (`BC_QUEEN_GROWUP`) stayed off; GROWFIRST, a narrow version (before round 200,
  only while she is shorter than 5), is what stayed.
- **Relaxing the portal-mouth threat.** On a map whose fountains are reachable only through portals, the kernel's
  reach-2 threat at every portal mouth kept the queen out. Quieting the mouths let her eat by round 9 and split by round
  11 in 10 of 10 test games, but the win rate did not move measurably in a small sample, and the change was not adopted.
- **The old flee search** (`BC_QUEEN_FLEE`, a 2-3 step escape chosen from the search's own scores) was replaced by the
  tiered kernel, which ranks every landing she can reach this turn by explicit threat margins.

## Tactics

- **The local fight search** (our move, the enemy's reply, our next move) lost 56-103 in its first version against the
  version it was built on; its weight was tuned to 0 and it never runs.
- **Protecting the longest teammate in view** ("VIP": escorts around it) gathered escorts into exactly the crowds that
  kill our own dragons and lost 33-47; it is off.
- **Free steps everywhere.** Taking the free steps toward food on every step went 39-41 (no gain), and it cut the share
  of games in which our queen survived to the end from 0.30 to 0.16. The free steps that stayed are gated by the R4
  net's prediction, limited to 2 steps, and checked against the queen's surroundings and every known enemy's reach.

## Networks

- **A bigger prior is not better.** The R4 move prior works as a tie-breaker. Lowering its weight from 0.05 to 0.02
  gained +7 in 304 paired mirror games.
- **A "cull" rule** (a short dragon next to our queen sacrifices itself where the net predicts no action) never fired on
  our own late turns and was not used.
- **A random prior as a control.** `BC_R4_NOISE` replaces the net's prior by a pseudo-random one; use it before
  crediting a gain to what the net knows rather than to any tie-breaking.

## Communication

- **Talking early is not free.** Sweeping the first round of full sonar showed early talking winning on some maps and
  losing on others. The final build stays quiet before round 300 except for event facts, which matches what
  replays of strong teams show.
- **Inferred traps must be graded.** Inferring a trap from every unanswered probe was wrong in 63 of 84 cases on a
  portal maze, and putting inferred traps into the team-wide trap mask walled good portals at both ends (a smoke test
  lost 28 of 40). Only traps seen directly enter the mask; inferred ones need evidence and expire.

## Process

- **Tuning saturates; policy changes win.** Tuning on a fixed policy stopped paying. After that, a new version had to
  differ in policy, and parameter tuning was kept small (single knobs, at most about 30 dimensions per module).
- **Lab win rates against our own previous versions overfit.** A version tuned against our own earlier versions beat
  them in the lab and did not improve on the ladder. Mechanism KPIs from replays and paired censuses against varied
  opponents became the gate.
- **The winner's curse.** The best of eight 40-game screens won 25-15 and then went 74-86 in its confirmation; decisions
  are pre-registered and confirmed on fresh seeds.
- **Every executed instruction changes play.** A switch that never triggers still moves the search's stopping point, so
  a small smoke test measures that drift, not the feature; inert changes are verified by WASM hashes or event-identical
  replays.
