"""Replay access shared by the rl scripts.

Replays are read with the repository's `abyss_tools` package (tools/abyss_tools): `load` parses a .replay (plain, .gz or
.zst; needs pycapnp) and `turns` rebuilds, at every turn of every dragon, the exact round block the engine sent it,
paired with the action it took. Rendering the protocol text instead of computing features in Python keeps the bot's own
C++ parser and featurizer the single source of truth for the network inputs (rl/featurize).

The package is found in ../../tools (this checkout), or wherever ABYSS_TOOLS_PATH points, or as an installed package.
"""
import os
import sys

_TOOLS = os.environ.get("ABYSS_TOOLS_PATH") or os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
if os.path.isdir(_TOOLS) and _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from abyss_tools.replay.io import initial_teams, load, map_name  # noqa: E402,F401
from abyss_tools.replay.state import DIR_NAME, UNIT_LIMIT, Board, in_vision, turns  # noqa: E402,F401


def map_size(rep):
    """(width, height) of the replay's map, as strings for the REC header"""
    line = next(l for l in rep.map.splitlines() if l.startswith("MAP "))
    return line.split()[1], line.split()[2]


def block_round(block):
    """the ROUND of a rendered round block"""
    return next((int(l.split()[1]) for l in block[:3] if l.startswith("ROUND")), 0)


def winner(rep):
    """'a', 'b' or None: the engine's own verdict (it applies the round-500 tiebreak rules; never recompute it)"""
    res = rep.result
    return str(res.winner) if res.which() == "winner" else None


def has_bed_info(rep):
    """False when the map text lists no pearl bed (TILE ... hi > 0): the bed / refill feature channels would be empty.
    Replays produced by `unswbc run` always carry them; replays from other sources may not."""
    return bool(Board(rep.map).beds)


def live_dragons(gen):
    """The dragons of a running `turns` generator at its current turn (its own state). The privileged facts of
    build_f1 / build_f2 (own / enemy longest, counts, totals) are read from here."""
    try:
        return gen.gi_frame.f_locals["dragons"]
    except (AttributeError, KeyError, TypeError) as e:
        raise RuntimeError("abyss_tools.replay.state.turns no longer exposes its `dragons` list; "
                           "update rl/data/replay_src.live_dragons") from e


def move1_rate(base, rnd):
    """keep rate of a single-step move: x4 before round 100, x2 before 200 (openings are where the nets matter most)"""
    return min(1.0, base * (4 if rnd < 100 else 2 if rnd < 200 else 1))


def is_move1(label):
    return label.startswith("MOVE ") and len(label) == 6
