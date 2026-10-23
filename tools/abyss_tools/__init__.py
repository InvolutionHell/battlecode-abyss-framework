"""abyss_tools: replay parsing, KPI reports, local match runners and map generators for UNSW Battlecode 2026 "Abyss".

Sub-packages:
  replay   read .replay files (Cap'n Proto), rebuild the per-turn protocol text, compare replays, measure the queen rule
  kpi      per-bot KPI tables over folders of replays (opening, economy lead curve, queen survival, pearl spawns, ...)
  run      local runners: paired-seed arena, worst-case CPU census, unseen-map safety check, compile-time macro sweeps,
           and a decision harness that replays a game through a native debug build
  maps     random symmetric map generators (general and per map class) and pearl-timing variants of existing maps

Every tool runs as `python -m abyss_tools.<sub>.<tool> --help` or through the console scripts listed in pyproject.toml.
"""

__version__ = "1.0.0"
