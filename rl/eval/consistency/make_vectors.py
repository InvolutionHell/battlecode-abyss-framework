#!/usr/bin/env python3
"""Test vectors for the train / serve consistency check (check.cpp): whole dragon lives from replays - every turn from
the dragon's first to its last, since an F2 net's memory accumulates from birth - as

  turns.txt          the round blocks the engine sent (abyss_tools.replay.state.turns), in featurize_f2's REC ... END
                     format
  expect_NAME.txt    one line per REC, same order: game dragon round, the 843 feature bytes in hex (the training
                     featurizer's round(clip(v, 0, 2.55) x 100)), the memory bytes in hex ("-" for nets without memory
                     inputs), then ref_forward.py's float64 outputs in check.cpp's order

  python3 rl/eval/consistency/make_vectors.py REPLAY [REPLAY ...] --featurize BIN/featurize_f2 --out DIR \\
      --weights r4=bots/abyss/r4_weights.hpp --weights rl=bots/abyss/rl_weights.hpp [--side A] [--dragons 3]

Then compile check.cpp against the bot folder whose code should match (see its header) and run
  ./check DIR/turns.txt DIR/expect_r4.txt
Any replay works: self-play games of your own bot are enough (rl/examples/make_example.sh does exactly this).
"""
import argparse
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "..", "..", "data")]
from ref_forward import NFEAT, ref_for  # noqa: E402
from replay_src import block_round, initial_teams, load, map_size, turns  # noqa: E402


def lives(rep, side, k):
    """the k dragons of `side` with the most turns (whole lives)"""
    team, count = dict(initial_teams(rep.map)), {}
    for e in rep.events:
        w = e.which()
        if w == "dragonSplit":
            team[e.dragonSplit.childId] = "A" if str(e.dragonSplit.team) == "a" else "B"
        elif w == "turnStart" and team.get(e.turnStart.id) == side:
            count[e.turnStart.id] = count.get(e.turnStart.id, 0) + 1
    return set(sorted(count, key=lambda d: (-count[d], d))[:k])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="+")
    ap.add_argument("--featurize", required=True, help="featurize_f2 (24 memory inputs) or featurize_f2h (44)")
    ap.add_argument("--weights", action="append", required=True, help="NAME=weights.hpp: writes expect_NAME.txt")
    ap.add_argument("--side", choices=["A", "B"], default="A")
    ap.add_argument("--dragons", type=int, default=3, help="whole lives per replay (the longest-lived dragons)")
    ap.add_argument("--max-turns", type=int, default=3000, help="stop after this many turns in all")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    turns_txt = os.path.join(args.out, "turns.txt")
    rounds, keys = [], []
    with open(turns_txt, "w") as f:
        for path in args.replays:
            if len(rounds) >= args.max_turns:
                break
            rep = load(path)
            game = os.path.basename(path).split(".replay")[0].replace(" ", "_")
            w, h = map_size(rep)
            want = lives(rep, args.side, args.dragons)
            n = 0
            for me, block, label in turns(rep, args.side, 1.0, None):
                if me.id not in want:
                    continue
                p = label.split()
                if p[0] == "MOVE" and (len(p) < 2 or not p[1]):
                    label = "NONE"  # the featurizer keeps only well-formed labels; the forward does not read it
                f.write(f"REC {game} {me.id} {me.team} {w} {h} 64 0 0 0 0,0,0,0,0,0,200 1 {label}\n")
                f.write("\n".join(block))
                f.write("\nEND\n")
                rounds.append(block_round(block))
                keys.append((game, me.id))
                n += 1
            print(f"{game} side {args.side} dragons {sorted(want)}: {n} turns", flush=True)
    raw = subprocess.run([args.featurize], stdin=open(turns_txt), capture_output=True, check=True).stdout
    if not rounds or len(raw) % len(rounds):
        sys.exit(f"the featurizer wrote {len(raw)} bytes for {len(rounds)} turns")
    width = len(raw) // len(rounds)
    nmem = width - 16 - NFEAT
    rec = np.frombuffer(raw, dtype=np.uint8).reshape(-1, width)
    for spec in args.weights:
        name, path = spec.split("=", 1)
        net = ref_for(path)
        if net.NPRIV not in (0, nmem):
            sys.exit(f"{path}: the net reads {net.NPRIV} memory inputs, the featurizer wrote {nmem}")
        out = net.forward(rec[:, 16:16 + NFEAT], rec[:, 5], rec[:, 16 + NFEAT:16 + NFEAT + net.NPRIV])
        with open(os.path.join(args.out, f"expect_{name}.txt"), "w") as f:
            for i in range(len(rec)):
                vals = []
                for k in net.ORDER:
                    if k in out:
                        vals.extend(f"{x:.9g}" for x in np.atleast_1d(out[k][i]))
                mem = rec[i, 16 + NFEAT:].tobytes().hex() if net.NPRIV else "-"
                f.write(f"{keys[i][0]} {keys[i][1]} {rounds[i]} {rec[i, 16:16 + NFEAT].tobytes().hex()} {mem} "
                        f"{' '.join(vals)}\n")
        print(f"expect_{name}.txt: {net.kind} net, {net.NPRIV} memory inputs", flush=True)
    print(f"{len(rec)} turns -> {turns_txt} (rounds {min(rounds)}-{max(rounds)})")


if __name__ == "__main__":
    main()
