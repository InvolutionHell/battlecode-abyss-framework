#!/usr/bin/env python3
"""Records for the 9-class policy / value net (pretrain.py; the released bot's opening prior is this kind of net).

One shard of an index (index_replays.py): every indexed game side's turns in rounds --min-round .. --max-round, a random
--sample share of them, rendered to the round block the engine sent (abyss_tools.replay.state.turns) and featurized by
featurize_rl (rl/featurize: the bot's own parser and featurizer). Record layout: rl/featurize/featurize_rl.cpp.

  python3 rl/data/build_rl.py --data DATA_DIR --featurize BIN/featurize_rl --shard 3 --nshards 16 --out DATA_DIR/rl_3.bin

The outcome byte: bit 0 the side's result, bits 5-6 the row's wclass, and with --aux the consequence labels of
aux_labels.py in bits 1-4 and 7 (for the auxiliary heads, pretrain.py --aux-weight).
"""
import argparse
import json
import os
import random
import subprocess
import sys

from replay_src import UNIT_LIMIT, block_round, has_bed_info, load, map_size, turns
from aux_labels import aux_labels

RECORD = 6 + 843  # featurize_rl.cpp: label, n, length, steps, outcome, round / 2, FDIM feature bytes


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", required=True, help="a folder with index.jsonl")
    ap.add_argument("--featurize", required=True, help="the featurize_rl binary (rl/featurize/build.sh)")
    ap.add_argument("--shard", type=int, required=True)
    ap.add_argument("--nshards", type=int, required=True)
    ap.add_argument("--top", type=int, default=99, help="keep index rows whose optional 'rank' field (a source tier you "
                    "assign to an index with index_replays.py --rank, e.g. 0 = your newest runs; default 0) is <= "
                    "this; rows without the field count as 99")
    ap.add_argument("--teams", nargs="*", default=[], help="only these bot folder names (e.g. your own arms; "
                    "overrides --top)")
    ap.add_argument("--max-round", type=int, default=200, help="stop at this round (an opening prior: 200)")
    ap.add_argument("--min-round", type=int, default=0, help="skip earlier turns (a mid / late-game prior)")
    ap.add_argument("--oversample", default="", help="comma-separated map names whose games are read --oversample-k "
                    "times (each pass samples its own turns; at --sample 1 the same turns k times)")
    ap.add_argument("--oversample-k", type=int, default=3)
    ap.add_argument("--sample", type=float, default=0.5, help="share of the turns kept")
    ap.add_argument("--aux", action="store_true", help="add each turn's consequence labels (aux_labels.py)")
    ap.add_argument("--seed", type=int, default=-1, help="sampling seed (default 1000 + shard)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    rows = [json.loads(l) for l in open(os.path.join(args.data, "index.jsonl"))]
    over = {m.strip().lower() for m in args.oversample.split(",") if m.strip()}
    keep = (lambda r: str(r.get("team")) in args.teams) if args.teams else (lambda r: r.get("rank", 99) <= args.top)
    rows = sorted((r for r in rows if keep(r)), key=lambda r: (str(r["game"]), str(r["team"]), r["side"]))
    rows = rows[args.shard::args.nshards]
    rng = random.Random(args.seed if args.seed >= 0 else 1000 + args.shard)
    tmp = args.out + ".part"
    n = no_beds = 0
    with open(tmp, "wb") as out:
        feat = subprocess.Popen([args.featurize], stdin=subprocess.PIPE, stdout=out, text=True)
        for r in rows:
            path = r.get("path") or os.path.join(args.data, "replays", f"{r['game']}.replay")
            try:
                rep = load(path)
            except Exception as e:  # noqa: BLE001  (missing or truncated file)
                print(f"skip {r['game']}: {e}", file=sys.stderr)
                continue
            w, h = map_size(rep)
            if not has_bed_info(rep):
                no_beds += 1
            side = "A" if r["side"] == "a" else "B"
            outcome = 1 if r.get("won") else 0
            aux = aux_labels(rep) if args.aux else {}
            wclass = int(r.get("wclass", 0)) & 3  # the row's sample-weight class (pretrain.py: 1, 0.75, 0.5, 0.25)
            name = next((l.split(None, 1)[1].strip().lower() for l in rep.map.splitlines() if l.startswith("MAP_NAME")), "")
            passes = args.oversample_k if name in over else 1
            for _ in range(passes):
                for me, block, label in turns(rep, side, args.sample, rng):
                    rnd = block_round(block)
                    if rnd >= args.max_round:
                        break
                    if rnd < args.min_round:
                        continue
                    bits = outcome | aux.get((me.id, rnd), 0) | (wclass << 5)
                    feat.stdin.write(f"REC {r['game']} {me.id} {me.team} {w} {h} {UNIT_LIMIT} {bits} {label}\n")
                    feat.stdin.write("\n".join(block))
                    feat.stdin.write("\nEND\n")
                    n += 1
        feat.stdin.close()
        if feat.wait() != 0:
            sys.exit(f"shard {args.shard}: featurize_rl failed (disk full?); {tmp} left for inspection")
    size = os.path.getsize(tmp)
    if size % RECORD or size // RECORD > n:
        sys.exit(f"shard {args.shard}: {size} bytes is not a whole number of {RECORD}-byte records")
    os.replace(tmp, args.out)
    warn = f"; {no_beds} game sides without pearl-bed info (bed channels empty)" if no_beds else ""
    print(f"shard {args.shard}: {len(rows)} game sides, {n} turns -> {args.out}{warn}")


if __name__ == "__main__":
    main()
