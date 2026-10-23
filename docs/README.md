# Documentation

[简体中文](README.zh-CN.md)

Documentation for our UNSW Battlecode 2026 "Abyss" bot and the framework around it. The final submitted bot is in
[`../bots/abyss`](../bots/abyss); everything here describes that bot, the game it plays, and how we built and
measured it.

## Contents

| document | what it covers |
| --- | --- |
| [rules/game-rules.md](rules/game-rules.md) ([简体中文](rules/game-rules.zh-CN.md)) | The game rules in implementer's terms: the torus board, pearl beds with visible countdowns (mirrored beds share one), dragons and the queen rule, sprints with ceil(L/4) free steps, splits, sonar, portals and kelp, winning, the CPU points budget and the sandbox. |
| [rules/engine-notes.md](rules/engine-notes.md) | Measured engine facts the official docs do not spell out: exact sonar mechanics, the virtual clock and instruction metering, the 95 M fallback, the WASM build cache that ignores header edits, the queen's identity, reproducibility. |
| [policy/README.md](policy/README.md) ([简体中文](policy/README.zh-CN.md)) | The architecture of the final bot, written from its code: world model, search core, economy, queen safety, end game, network priors, CPU safety, and which compiled code is switched off. |
| [methodology.md](methodology.md) | How we evaluated changes: from replay review to a change, mechanism KPIs over win rate, paired seeds with a same-seed baseline arm, pre-registered two-stage decisions, and the safety gates (equivalence, CPU census, unseen maps). |
| [history.md](history.md) | One page on how the bot evolved, from a greedy forager to the queen-centred final line, and the lines that did not make it. |
| [results.md](results.md) ([简体中文](results.zh-CN.md)) | Peak ladder results, the identity of the final version (WASM hash), the data statement for the network weights, and measured effects. |
| [dev-guide.md](dev-guide.md) | Working rules for changing the bot: always emit a valid action, test against the previous best on several maps, sandbox CPU checks, compile-time switches, purging the WASM cache, and how to verify the released bot. |

## Where to start

- **New to the game:** [rules/game-rules.md](rules/game-rules.md), then [rules/engine-notes.md](rules/engine-notes.md).
- **Reading the bot:** [policy/README.md](policy/README.md), with [history.md](history.md) for why it looks the way it
  does.
- **Changing the bot or building your own:** [dev-guide.md](dev-guide.md) and [methodology.md](methodology.md).

The tools that implement the methodology (match runners, replay parsers, KPI scripts) are in [`../tools`](../tools)
and [`../infra`](../infra); the imitation-learning pipeline for the network priors is in [`../rl`](../rl).

English is the reference language of these documents; pages with a Simplified Chinese version link to it at the top.
