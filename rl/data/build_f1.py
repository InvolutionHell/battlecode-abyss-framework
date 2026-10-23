#!/usr/bin/env python3
"""Records for the full-action-head ("F1") net (train_f1.py; the released bot's move prior is this kind of net).

Every round of a game side, with the full action (16 classes, the no-action turns included), the game's end (final
longest-dragon gap, total-length gap) and the replay's facts at the turn (own / enemy longest, dragon counts, totals:
privileged, the bot can only estimate them). Record layout: rl/featurize/featurize_f1.cpp.

Single-step moves are about 95% of all turns, so they are thinned: rare actions are kept at --rare-rate, single steps at
--move1-rate (x4 before round 100, x2 before 200). A first pass over the events finds each turn's action, the second
runs abyss_tools' turns() with a scripted random-number source that keeps exactly the chosen turns. Each record carries
its keep rate (header byte 15, bits 1-7, in %) so the trainer weights by 1 / rate and learns the natural frequencies.

  python3 rl/data/build_f1.py --data DATA_DIR --featurize BIN/featurize_f1 --shard 3 --nshards 16 --out DATA_DIR/f1_3.bin
  --no-queen   keep no turn of the queens (dragon ids 0 / 1): the foraging / positioning policy of the other dragons,
               which is what the released bot's move prior was trained on
"""
import argparse
import json
import os
import random
import subprocess
import sys

from replay_src import (UNIT_LIMIT, block_round, has_bed_info, initial_teams, is_move1, live_dragons, load, map_size,
                        move1_rate, turns)

RECORD = 16 + 843  # featurize_f1.cpp


def plan(rep, side, args, rng):
    """Pass 1: for each turn of `side` in event order (the order turns() draws its random numbers in), keep it or not by
    its action's class and round."""
    team = dict(initial_teams(rep.map))
    keep, pending, rnd = [], {}, 0
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        elif k == "dragonSplit":
            ev = e.dragonSplit
            team[ev.childId] = "A" if str(ev.team) == "a" else "B"
        elif k == "turnStart":
            i = e.turnStart.id
            if team.get(i) == side:
                pending[i] = len(keep)
                keep.append(False)
        elif k == "dragonAction":
            a = e.dragonAction
            j = pending.pop(a.id, None)
            if j is None:
                continue
            single = (not a.tle and a._has("action") and a.action.which() == "move" and len(a.action.move) == 1)
            q = move1_rate(args.move1_rate, rnd) if single else args.rare_rate
            keep[j] = rng.random() < q and not (args.no_queen and a.id < 2)  # ids 0 / 1 are the two queens
    return keep


class Script:
    """The random-number source for turns(): 0.0 (below any sample rate) keeps a turn, 0.99 skips it."""

    def __init__(self, keep):
        self.keep, self.i = keep, 0

    def random(self):
        k = self.keep[self.i] if self.i < len(self.keep) else False
        self.i += 1
        return 0.0 if k else 0.99


def side_facts(gen, me):
    """own / enemy longest, total length and dragon count at this turn (the replay's global truth)"""
    ol = ot = on = el = et = en = 0
    for d in live_dragons(gen):
        if not d.alive or not d.body:
            continue
        ln = len(d.body)
        if d.team == me.team:
            ol, ot, on = max(ol, ln), ot + ln, on + 1
        else:
            el, et, en = max(el, ln), et + ln, en + 1
    return ol, el, on, en, ot, et


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", required=True, help="a folder with index.jsonl (rows: game, team, side, won, path, wclass)")
    ap.add_argument("--featurize", required=True, help="the featurize_f1 binary (rl/featurize/build.sh)")
    ap.add_argument("--shard", type=int, required=True)
    ap.add_argument("--nshards", type=int, required=True)
    ap.add_argument("--move1-rate", type=float, default=0.03,
                    help="a whole number of %% (x2, x4 too): the header stores the rate in whole %%")
    ap.add_argument("--rare-rate", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=-1, help="the sampling seed (default 7000 + shard)")
    ap.add_argument("--no-queen", action="store_true", help="keep no turn of the queens (dragon ids 0 / 1)")
    ap.add_argument("--teams", nargs="*", default=[], help="only these bot folder names (e.g. your own arms)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    rows = [json.loads(l) for l in open(os.path.join(args.data, "index.jsonl"))]
    if args.teams:
        rows = [r for r in rows if str(r.get("team")) in args.teams]
    rows = sorted(rows, key=lambda r: (str(r["game"]), str(r["team"]), r["side"]))[args.shard::args.nshards]
    rng = random.Random(args.seed if args.seed >= 0 else 7000 + args.shard)
    tmp = args.out + ".part"
    n = games = no_beds = 0
    classes = {}
    with open(tmp, "wb") as out:
        feat = subprocess.Popen([args.featurize], stdin=subprocess.PIPE, stdout=out, text=True)
        for r in rows:
            try:
                rep = load(r["path"])
                side = "A" if r["side"] == "a" else "B"
                res = rep.result
                own, enemy = (res.teamA, res.teamB) if side == "A" else (res.teamB, res.teamA)
                kgap = own.longestDragon - enemy.longestDragon
                tgap = int((own.totalLength - enemy.totalLength) / 4)
                keep = plan(rep, side, args, rng)
            except Exception as e:  # noqa: BLE001  (missing or truncated file)
                print(f"skip {r['game']}: {e}", file=sys.stderr)
                continue
            w, h = map_size(rep)
            if not has_bed_info(rep):
                no_beds += 1
            bits = (1 if r.get("won") else 0) | ((int(r.get("wclass", 0)) & 3) << 5)
            gen = turns(rep, side, 0.5, Script(keep))
            games += 1
            for me, block, label in gen:
                rnd = block_round(block)
                ol, el, on, en, ot, et = side_facts(gen, me)
                q = move1_rate(args.move1_rate, rnd) if is_move1(label) else args.rare_rate
                flags = (1 if len(me.body) >= ol else 0) | (min(100, round(q * 100)) << 1)
                verb = label.split()[0]
                classes[verb] = classes.get(verb, 0) + 1
                feat.stdin.write(f"REC {r['game']} {me.id} {me.team} {w} {h} {UNIT_LIMIT} {bits} {kgap} {tgap} "
                                 f"{ol},{el},{on},{en},{ot // 4},{et // 4},{flags} {label}\n")
                feat.stdin.write("\n".join(block))
                feat.stdin.write("\nEND\n")
                n += 1
        feat.stdin.close()
        if feat.wait() != 0:
            sys.exit(f"shard {args.shard}: featurize_f1 failed (disk full?); {tmp} left for inspection")
    size = os.path.getsize(tmp)
    if size % RECORD or size // RECORD > n:
        sys.exit(f"shard {args.shard}: {size} bytes is not a whole number of {RECORD}-byte records")
    os.replace(tmp, args.out)
    warn = f"; {no_beds} game sides without pearl-bed info (bed channels empty)" if no_beds else ""
    print(f"shard {args.shard}: {games} of {len(rows)} game sides, {n} turns {classes} -> {args.out}{warn}")


if __name__ == "__main__":
    main()
