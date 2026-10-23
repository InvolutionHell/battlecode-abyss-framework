#!/usr/bin/env python3
"""A model soup: the element-wise average of train_f1.py models trained from one start (replicas of one recipe with
different --seed), exported as one f1_weights.hpp - an ensemble's stability at one net's cost in the bot. Nets that
share a start stay in one basin, so their average is a valid net. In our runs soups (and logit ensembles) gave no
steady gain over a single replica; the tool is kept for that comparison.

  python3 rl/train/soup.py RUN1/model.pt RUN2/model.pt RUN3/model.pt --out SOUP_DIR [--npriv 24 --q 6] [--weights 1,1,1]

--npriv / --q / --channels / --hidden must match the models (train_f1.py's --mem or --priv, the number of Q heads).
"""
import argparse
import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train_f1 import F1Net, export  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("models", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--npriv", type=int, default=0, help="extra inputs of the models (memory 24 / 44, --priv 8)")
    ap.add_argument("--q", type=int, default=0, help="Q heads of the models (0, 2, 3 or 6)")
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--weights", default="", help="comma-separated mixing weights (default equal)")
    args = ap.parse_args()
    sds = [torch.load(p, map_location="cpu") for p in args.models]
    w = [float(v) for v in args.weights.split(",")] if args.weights else [1.0] * len(sds)
    if len(w) != len(sds):
        sys.exit("one mixing weight per model")
    tot = sum(w)
    keys = sds[0].keys()
    for sd in sds[1:]:
        if sd.keys() != keys or any(sd[k].shape != sds[0][k].shape for k in keys):
            sys.exit("the models differ in shape: a soup needs one architecture")
    soup = {k: sum(wi * sd[k].float() for wi, sd in zip(w, sds)) / tot for k in keys}
    model = F1Net(args.channels, args.hidden, npriv=args.npriv, q=args.q)
    model.load_state_dict(soup)
    os.makedirs(args.out, exist_ok=True)
    torch.save(model.state_dict(), os.path.join(args.out, "model.pt"))
    # base names only: the metadata ends up in the generated header's first line
    meta = {"soup": [os.path.basename(os.path.dirname(os.path.abspath(p))) + "/" + os.path.basename(p) for p in args.models],
            "weights": w}
    export(model, os.path.join(args.out, "f1_weights.hpp"), meta)
    json.dump(meta, open(os.path.join(args.out, "soup.json"), "w"), indent=1)
    print(f"soup of {len(sds)} models -> {os.path.join(args.out, 'f1_weights.hpp')}")


if __name__ == "__main__":
    main()
