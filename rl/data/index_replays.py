#!/usr/bin/env python3
"""Index replays for the record builders (build_rl.py, build_f1.py, build_f2.py): one JSON line per game side.

  python3 rl/data/index_replays.py RUN_DIR [RUN_DIR ...] --out DATA_DIR/index.jsonl [--only BOT ...] [--recursive]

Each row: game (a unique token: the replay's path below its run folder), path, team (the bot's name, the arena's _L / _R
side suffix removed), side ("a" / "b"), won (the engine's result for that side), rank (0), wclass (0), map. Compressed
replays (.replay.gz / .replay.zst) are fine; unreadable files (a killed job's partial write) are counted and skipped.

  --only NAME ...    keep only sides whose bot name starts with one of these (e.g. the bot whose decisions you want)
  --reward units100  label a side "won" when it had more dragons than the other side at round 100 (an opening KPI)
                     instead of the game's result
  --rank N / --wclass K   values written to every row: a source tier you assign to this index (e.g. 0 = your newest
                     runs; build_rl.py --top filters by it); the trainers weight a record by its wclass (0-3 -> 1, 0.75,
                     0.5, 0.25), e.g. to down-weight an older or weaker source

Indexes of several sources can be concatenated (cat a.jsonl b.jsonl > index.jsonl) or merged without duplicates with
merge_index.py.
"""
import argparse
import glob
import json
import os
import sys

from replay_src import initial_teams, load, map_name, winner


def units_at(rep, rnd):
    """each team's dragons that take a turn in round `rnd` (fewer if the game ended before)"""
    team = dict(initial_teams(rep.map))
    count = {"A": 0, "B": 0}
    cur = 0
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            cur = e.roundStart.round
            if cur > rnd:
                break
        elif k == "dragonSplit":
            team[e.dragonSplit.childId] = "A" if str(e.dragonSplit.team) == "a" else "B"
        elif k == "turnStart" and cur == rnd:
            count[team[e.turnStart.id]] += 1
    return count


def base_name(name):
    return name[:-2] if name.endswith(("_L", "_R")) else name


def replays_in(d, recursive):
    pats = ["**/*.replay", "**/*.replay.gz", "**/*.replay.zst"] if recursive else \
        ["*.replay", "*.replay.gz", "*.replay.zst", "replays/*.replay", "replays/*.replay.gz", "replays/*.replay.zst"]
    return sorted({p for pat in pats for p in glob.glob(os.path.join(d, pat), recursive=recursive)})


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs", nargs="+", help="folders holding replays (directly or in replays/)")
    ap.add_argument("--out", required=True, help="the index file to write (e.g. DATA_DIR/index.jsonl)")
    ap.add_argument("--only", nargs="*", default=[], help="keep only sides whose bot name starts with one of these")
    ap.add_argument("--reward", choices=["result", "units100"], default="result")
    ap.add_argument("--recursive", action="store_true", help="search the folders recursively")
    ap.add_argument("--rank", type=int, default=0, help="a source tier you assign to this index, written to every row "
                    "(e.g. 0 = your newest runs; build_rl.py --top filters by it)")
    ap.add_argument("--wclass", type=int, default=0, choices=[0, 1, 2, 3], help="a weight class written to every row "
                    "(0-3 -> weight 1, 0.75, 0.5, 0.25 in the trainers)")
    a = ap.parse_args()
    rows, bad, n = [], 0, 0
    for run in a.runs:
        root = os.path.abspath(run)
        for path in replays_in(root, a.recursive):
            n += 1
            try:
                rep = load(path)
                win = winner(rep)
                bots = (rep.botA, rep.botB)
                mp = map_name(rep.map)
            except Exception:  # noqa: BLE001  (a truncated file)
                bad += 1
                continue
            rel = os.path.relpath(path, os.path.dirname(root))
            game = rel.split(".replay")[0].replace(os.sep, "__").replace(" ", "_")
            units = units_at(rep, 100) if a.reward == "units100" else None
            for side, bot in zip("ab", bots):
                name = base_name(bot)
                if a.only and not any(name.startswith(o) for o in a.only):
                    continue
                won = win == side
                if units is not None:
                    me, other = (units["A"], units["B"]) if side == "a" else (units["B"], units["A"])
                    won = me > other
                rows.append({"game": game, "path": os.path.abspath(path), "team": name, "side": side, "won": won,
                             "rank": a.rank, "wclass": a.wclass, "map": mp})
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")
    print(f"{a.out}: {len(rows)} game sides from {n - bad} replays ({bad} unreadable), "
          f"won {sum(r['won'] for r in rows)}", file=sys.stderr)


if __name__ == "__main__":
    main()
