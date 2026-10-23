#!/usr/bin/env python3
"""CPU safety check over arena runs: the candidate's worst turn and every turn it lost to the CPU limit.

  python3 infra/lab/cpu_check.py RUN_DIR [RUN_DIR ...] [--limit 95] [--opp]

Reads the cpu.txt that infra/lab/arena.py writes in --sandbox runs (one line per game: cand_max / opp_max = the
largest points spent in one turn by any dragon of that side, in millions; tle / opp_tle = turns lost to the CPU limit
or the wall clock). RUN_DIR may also be a lab pair folder (its orig/ and pv/ runs are read) or a tag folder.

The judge kills a dragon whose turn passes 100 M points. The release floor we used is stricter: no CPU failure and
no turn at or above --limit (95 M) on the largest maps at full unit count, so a slow node or an unlucky position
still leaves a margin. Exit status 1 when the floor is broken, 2 when no cpu.txt was found.
"""
import argparse
import os
import re
import sys

LINE = re.compile(r"^(?P<map>\S+) (?P<first>\S+) vs (?P<second>\S+): cand_max=(?P<cmax>[0-9.]+|-)M "
                  r"opp_max=(?P<omax>[0-9.]+|-)M tle=(?P<tle>\d+) opp_tle=(?P<otle>\d+)")


def files(paths):
    out = []
    for p in paths:
        if os.path.isfile(p):
            out.append(p)
            continue
        for root, _, names in os.walk(p):
            if "cpu.txt" in names:
                out.append(os.path.join(root, "cpu.txt"))
    return sorted(set(out))


def read(path, opp=False):
    rows = []
    with open(path) as f:
        for line in f:
            m = LINE.match(line.strip())
            if not m:
                continue
            top = m["omax"] if opp else m["cmax"]
            rows.append((m["map"], m["first"], m["second"], None if top == "-" else float(top),
                         int(m["otle"] if opp else m["tle"])))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", help="arena output folders, lab pair / tag folders, or cpu.txt files")
    ap.add_argument("--limit", type=float, default=95.0, help="the per-turn floor in M points (default 95)")
    ap.add_argument("--opp", action="store_true", help="check the opponent's columns instead of the candidate's")
    ap.add_argument("--top", type=int, default=10, help="list this many worst games")
    a = ap.parse_args()
    paths = files(a.runs)
    if not paths:
        print("no cpu.txt found (were the runs played with --sandbox?)")
        sys.exit(2)
    rows = [r + (p,) for p in paths for r in read(p, a.opp)]
    metered = [r for r in rows if r[3] is not None]
    fails = sum(r[4] for r in rows)
    over = [r for r in metered if r[3] >= a.limit]
    worst = max((r[3] for r in metered), default=None)
    side = "opponent" if a.opp else "candidate"
    print(f"{len(rows)} games in {len(paths)} runs ({len(metered)} metered); {side}: max "
          f"{'-' if worst is None else f'{worst:.1f}M'} points in one turn, {len(over)} games with a turn >= "
          f"{a.limit:g}M, {fails} turns lost to the CPU limit / wall clock")
    for r in sorted(metered, key=lambda r: -r[3])[:a.top]:
        print(f"  {r[3]:6.1f}M  {r[0]:<28} {r[1]} vs {r[2]}" + (f"  failures {r[4]}" if r[4] else ""))
    ok = fails == 0 and not over
    print("CPU floor: " + ("pass" if ok else "FAIL"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
