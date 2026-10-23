#!/usr/bin/env python3
"""Records for F2 nets: build_f1.py's records plus each dragon's own memory inputs (rl/runtime/f2_memory.hpp, run by
featurize_f2). Every turn of every dragon of the side goes to the featurizer in game order, so each dragon's memory is
built from its own views exactly as a bot builds it; only the kept turns (rare actions at --rare-rate, single steps at
--move1-rate, x4 before round 100, x2 before 200, as build_f1.py) become records. Layout: rl/featurize/featurize_f2.cpp.

  python3 rl/data/build_f2.py --data DATA_DIR --featurize BIN/featurize_f2 --shard 3 --nshards 16 --out DATA_DIR/f2_3.bin

Label options (they reuse header bytes that otherwise hold the privileged facts):
  --aux           consequence labels (aux_labels.py: split5, eat5, die10, kill10) in the outcome byte's bits 1-4 and 7
  --returns       byte 9 = the turn's discounted future intake x 25 (eat_returns: gamma 0.9, 20 rounds), byte 10 = the
                  team's longest dragon's discounted growth x 4 + 64 (king_returns: gamma 0.95, 50 rounds)
  --lineage       with --returns: byte 9 holds the lineage's intake (lineage_returns: the dragon and the dragons split off
                  it from this round on), so a split's value includes what its child eats
  --team-labels   bytes 11-13 = the team's longest-dragon growth over 50 rounds + 64, whether that dragon dies within 50,
                  the change of the total-length lead over 50 / 4 + 128
  --late-boost K  keep rates x K from --late-from on (the record carries its rate: the trainer's 1 / rate undoes it)
"""
import argparse
import json
import os
import random
import subprocess
import sys

from replay_src import UNIT_LIMIT, block_round, has_bed_info, is_move1, live_dragons, load, map_size, move1_rate, turns
from aux_labels import aux_labels, eat_returns, king_returns, lineage_returns, team_labels


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", required=True, help="a folder with index.jsonl (rows: game, team, side, won, path, wclass)")
    ap.add_argument("--featurize", required=True, help="the featurize_f2 binary (featurize_f2h for --nmem 44)")
    ap.add_argument("--shard", type=int, required=True)
    ap.add_argument("--nshards", type=int, required=True)
    ap.add_argument("--move1-rate", type=float, default=0.03,
                    help="a whole number of %% (x2, x4 too): the header stores the rate in whole %%")
    ap.add_argument("--rare-rate", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=-1, help="the sampling seed (default 7000 + shard)")
    ap.add_argument("--nmem", type=int, default=24, help="the featurizer's memory inputs (44: featurize_f2h, -DF2_HEARD)")
    ap.add_argument("--aux", action="store_true")
    ap.add_argument("--returns", action="store_true")
    ap.add_argument("--team-labels", action="store_true")
    ap.add_argument("--late-boost", type=float, default=1.0)
    ap.add_argument("--late-from", type=int, default=300)
    ap.add_argument("--lineage", action="store_true")
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
            except Exception as e:  # noqa: BLE001  (missing or truncated file)
                print(f"skip {r['game']}: {e}", file=sys.stderr)
                continue
            w, h = map_size(rep)
            if not has_bed_info(rep):
                no_beds += 1
            bits = (1 if r.get("won") else 0) | ((int(r.get("wclass", 0)) & 3) << 5)
            aux = aux_labels(rep) if args.aux else {}
            rets = (lineage_returns(rep) if args.lineage else eat_returns(rep)) if args.returns else {}
            kret = king_returns(rep) if args.returns else {}
            tlab = team_labels(rep) if args.team_labels else {}
            gen = turns(rep, side, 1.0, rng)
            games += 1
            for me, block, label in gen:
                rnd = block_round(block)
                q = move1_rate(args.move1_rate, rnd) if is_move1(label) else args.rare_rate
                if rnd >= args.late_from:
                    q = min(1.0, q * args.late_boost)
                if rng.random() >= q:  # not kept: the featurizer only updates the dragon's memory (keep = 0)
                    feat.stdin.write(f"REC {r['game']} {me.id} {me.team} {w} {h} {UNIT_LIMIT} 0 0 0 0 0 {label}\n")
                    feat.stdin.write("\n".join(block))
                    feat.stdin.write("\nEND\n")
                    continue
                ol = ot = on = el = et = en = 0
                for d in live_dragons(gen):  # every dragon alive this turn
                    if not d.alive or not d.body:
                        continue
                    ln = len(d.body)
                    if d.team == me.team:
                        ol, ot, on = max(ol, ln), ot + ln, on + 1
                    else:
                        el, et, en = max(el, ln), et + ln, en + 1
                flags = (1 if len(me.body) >= ol else 0) | (min(100, round(q * 100)) << 1)
                verb = label.split()[0]
                classes[verb] = classes.get(verb, 0) + 1
                b9 = min(255, round(rets[(me.id, rnd)] * 25)) if args.returns else ol
                b10 = min(255, max(0, round(kret[(me.team, rnd)] * 4) + 64)) if args.returns else el
                if args.team_labels:
                    kg, kd, tot = tlab[(me.team, rnd)]
                    mid = f"{min(255, max(0, kg + 64))},{kd},{min(255, max(0, round(tot / 4) + 128))}"
                else:
                    mid = f"{on},{en},{ot // 4}"
                feat.stdin.write(f"REC {r['game']} {me.id} {me.team} {w} {h} {UNIT_LIMIT} {bits | aux.get((me.id, rnd), 0)} "
                                 f"{kgap} {tgap} {b9},{b10},{mid},{et // 4},{flags} 1 {label}\n")
                feat.stdin.write("\n".join(block))
                feat.stdin.write("\nEND\n")
                n += 1
        feat.stdin.close()
        if feat.wait() != 0:
            sys.exit(f"shard {args.shard}: featurize_f2 failed (disk full?); {tmp} left for inspection")
    size, record = os.path.getsize(tmp), 16 + 843 + args.nmem
    if size % record or size // record > n:
        sys.exit(f"shard {args.shard}: {size} bytes is not a whole number of {record}-byte records")
    os.replace(tmp, args.out)
    warn = f"; {no_beds} game sides without pearl-bed info (bed channels empty)" if no_beds else ""
    print(f"shard {args.shard}: {games} of {len(rows)} game sides, {n} turns {classes} -> {args.out}{warn}")


if __name__ == "__main__":
    main()
