"""Event-for-event replay comparison.

Used to prove that a compile-time switch which is off leaves a bot's play unchanged: the same map, seed and opponent must
give the same event stream. Compares every field of every event, in order, and the results; the bot names
(Replay.botA / botB) and the map text are not compared.

  python -m abyss_tools.replay.diff A.replay B.replay [--ignore dragonLog ...]
  abyss-replay-diff ...

Exits 0 when the replays are identical, 1 otherwise, and prints the event counts and the first difference with its round.
Trace builds add dragonLog events: pass `--ignore dragonLog` to compare a trace build with a plain one.
"""
import argparse
import os
import sys

from .io import load


def events(path, ignore=()):
    rep = load(path)
    out = []
    rnd = 0
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        if k in ignore:
            continue
        out.append((rnd, k, e.to_dict()))
    return out, rep.result.to_dict()


def compare(a, b, ignore=()):
    """(same, why, events in a, events in b)"""
    ea, ra = events(a, ignore)
    eb, rb = events(b, ignore)
    for i, (x, y) in enumerate(zip(ea, eb)):
        if x != y:
            return False, f"event {i} (round {x[0]}): {x[1]} {x[2]} != {y[1]} {y[2]}", len(ea), len(eb)
    if len(ea) != len(eb):
        return False, f"lengths differ after {min(len(ea), len(eb))} equal events", len(ea), len(eb)
    if ra != rb:
        return False, f"results differ: {ra} != {rb}", len(ea), len(eb)
    return True, "identical", len(ea), len(eb)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--ignore", nargs="*", default=[], help="event kinds left out (e.g. dragonLog for trace builds)")
    args = ap.parse_args(argv)
    same, why, na, nb = compare(args.a, args.b, set(args.ignore))
    print(f"{os.path.basename(args.a)} vs {os.path.basename(args.b)}: {na} / {nb} events: {why}")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
