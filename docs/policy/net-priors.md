# Network priors

The final bot carries two small convolutional networks, both trained by imitation. They do not decide moves on their
own: they add a prior to the root values of the search, give a split trigger, and (for the second one) decide where a
dragon takes extra free steps. Pointers are `file:line` in `bots/abyss`.

| net | used by | when | what it contributes | code | weights |
|---|---|---|---|---|---|
| RL (opening prior) | the queen's process | rounds 0-199, not on lean turns | root prior with weight 0.05; split trigger at P(split) >= 0.4 | `rl_net.hpp` | `rl_weights.hpp` |
| R4 (move prior) | every other dragon | the whole game, not on lean turns | root prior with weight 0.02; split trigger at P(split) >= 0.4 before round 200; free-step gate and second-step choice | `r4net.hpp` | `r4_weights.hpp` |

Both weight files are published as they were used in the final build. **Data statement:** trained by imitation on
aggregated public ladder replays of top-ranked teams; contains no per-team model; the training data is not included. The
RL net learned from such games' turns of rounds 0-200 (about 5.1 M samples, policy top-1 accuracy 0.73); the R4 net from
the non-queen dragons of such games (about 6.0 M samples). The training summary in the first line of `r4_weights.hpp`
records a single-step direction accuracy of 73.4% on its validation split; in a separate evaluation on held-out games
both nets were scored on the same first steps, the R4 net at 73.5% top-1 and the RL net at 72.1%. "RL" and "R4" are the
names the code uses for the two nets (`rl_net.hpp` / `RLNet` / `BC_RL_PRIOR`, and `r4net.hpp` / `BC_R4NET`). The
training pipeline is documented in [rl/README.md](../../rl/README.md).

## Shared input and trunk

Both nets read the same observation (rl_features.hpp:28), built from the dragon's own round block only:

- a 7 x 7 grid around the head, **rotated so that the dragon's facing is "up"** (moves are learned relative to the
  facing: forward, right, back, left), with 17 channels: pearl, bed, bed countdown (as exp(-countdown / 8)), own body,
  teammate body, enemy body, teammate head, enemy head, kelp on each of the 4 sides, portal on each of the 4 sides, and
  the facing of other heads;
- 10 scalars: round / 500, length / 16 (capped at 32), units / 64, messages received / 4, the 5 echo counts / 2
  (capped), and a flag for round 400 or later;
- every input quantised to steps of 0.01 in [0, 2.55], exactly as the training records were, plus the round (in steps of
  2) as an extra input.

Trunk: 3x3 convolution 17 -> 64 with ReLU, 3x3 convolution 64 -> 64 with ReLU, dense (64 x 49 + 10 + 1) -> 128 with
ReLU. Weights are stored as int16 per output row with one float scale per row and de-quantised once per process (the net
object is a function-local static). The forward pass keeps the output channel innermost so the compiler can vectorise it
under `-msimd128`, and skips zero inputs (the grid is mostly one-hot zeros); a plain scalar version cost about 30 M
points a turn in the sandbox (rl_net.hpp:122).

## RL: the queen's opening prior (`BC_RL_PRIOR` 1)

Head: 9 action classes relative to the facing (single step forward / right / back / left, multi-step move with that
first step, split), plus a split-fraction output and a value head that the bot does not use (rl_net.hpp:19).

It runs in the queen's process before round 200, when the turn is not lean (policy.hpp:365):

```text
total[d] += 0.05 * log(P(step d) + P(multi-step starting with d) + 0.001)        for each direction d
split trigger: P(split) >= 0.4  (then trySplit may halve a dragon of 4 or more)
```

(policy.hpp:760). For the queen the prior enters the totals that the Queen Safety Kernel uses to order the moves inside
a tier ([queen-safety.md](queen-safety.md)), so it can break ties between safe landings but never overrules a tier. Its
split trigger is redundant with the swarm rule in this build.

## R4: the move prior for the other dragons (`BC_R4NET` 1)

Head: 16 action classes (move with 1, 2 or 3+ steps for each of the 4 first-step directions; split 2, 3-4 or 5+; no
action), a split-fraction output, a second-step head (the direction of the second step relative to the first, given a
multi-step move), and value heads (win, final longest-dragon gap, final total-length gap) that the bot does not use
(r4net.hpp:31).

Every non-queen dragon evaluates it once per turn when the turn is not lean (policy.hpp:419). The non-queen processes do
not build the RL net at all.

**Root prior** (policy.hpp:813), over the legal first steps only:

```text
p(d)      = P(first step d, any step count)
total[d] += 0.02 * log(p(d) / (sum of p over the legal first steps) + 0.001)
```

At weight 0.02 the prior changes the search's choice on about 7% of the turns it runs on and gives up 0.006 of search
value on average: it is a tie-breaker between moves the hand-written value function rates almost equally. Lowering the
weight from 0.05 to 0.02 gained +7 games in 304 paired mirror games and +12 in 152 against sparring opponents, for -1 in
76 against an aggressive sparring set.

**Split trigger**: before round 200, P(split 2) + P(split 3-4) + P(split 5+) >= 0.4. Like RL's, it is redundant with the
swarm rule in this build.

**Free steps** (`BC_R4_SPRINT` 6 = bits 2 and 4; policy.hpp:2415). A dragon of length L may move ceil(L/4) steps a turn
for free. The top teams' dragons of length 5 or more took more than one step in about half of their moves, ours almost
never. The R4 net supplies both the decision and the path:

- the gate: after the search picked a first step d, the dragon tries free steps when P(multi-step | first step d) = (P(2
  steps, d) + P(3+ steps, d)) / P(d) is at least 0.3 (policy.hpp:1097);
- which first steps qualify: plain kinds only ("forage", "scout", "disperse", "detach", "team-target") and, with bit 4,
  also "leave-pocket", "expand", "idle-bed", "finish" and an eating step; never a trade, a split, a portal edge, or a
  dragon whose body is not fully known; never the queen;
- the path: at most min(ceil(L/4), 2) steps in total (`BC_R4_MAXSTEPS` 2). The second step goes forward, right or left
  of the first (never back), onto a visible free tile our tail has already left and not across a portal; it is the
  direction the net's second-step head rates highest, a visible pearl adding 0.25. The move stops at the first pearl;
- safety checks on the result: no cell of the path within Chebyshev 2 of our queen's head, and not her last free side;
  the landing must lie farther (Chebyshev) from every known enemy head than that head's free reach + 1 (a visible head
  counts as max(visible segments + 1, 6) long; a head seen 1-3 rounds ago, at least 4 long, has had its free steps once
  per round since; policy.hpp:2393); and the landing must keep room for L + pearls eaten + 2 tiles.

The cheap landing rule replaced a check through the Queen Safety Kernel's threat map, which cost up to about 10 M points
and needed a clock guard; together with the lower gate (0.3 instead of 0.5) it measured +3.9 games in 152 paired mirror
games and no change (0 in 152) against sparring opponents.

## Inactive variants in the code

All of these are compiled out or switched off in the final build ([switches.md](switches.md)):

- `BC_R4_CULL`: a short dragon next to our queen sacrifices itself where the net predicts "no action" (on our own late
  turns the net never predicted it often enough to fire);
- `BC_R4_NOISE`: a pseudo-random prior instead of the net, a control arm that measures what the net adds over arbitrary
  tie-breaking;
- `BC_R4_CALM`, `BC_R4_CONF`, `BC_R4_BAND`: apply the prior only without enemies in view, only where the net is
  confident, or only among near-best moves;
- `BC_R5_GATE`, `BC_R5_KINDS`, `BC_R5_NETSPLIT`, `BC_R5_RLUNTIL`, other `BC_R5_*` values: a stochastic free-step gate,
  more step kinds, the net as the only split rule, a different split window;
- `BC_R6`: per-map-class networks picked from observed geometric features. Its headers are not shipped, so the switch
  cannot be turned on in this release;
- `BC_R7_TOPB_ON`, `BC_R7_KINDW_ON`: a fixed bonus for the net's top move, and prior weights by the kind of step;
- `BC_T64_FIELD`: a much larger prior weight near fast beds.
