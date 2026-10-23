#!/usr/bin/env python3
"""How far the auxiliary heads of a 9-class net (pretrain.py --aux-weight) can be trusted, before a bot sets thresholds
on them: per head, calibration by predicted-probability decile (mean prediction against the observed rate), AUC by
dragon length and by round bucket, and the relative risk of the top decile against the base rate. Run it on held-out
shards (build_rl.py --aux records of games the net never saw).

  python3 rl/eval/aux_eval.py RUN_DIR/model.pt SHARD.bin[.zst] [...] [--max 400000] [--channels 64 --hidden 128]
"""
import argparse
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "train"))
import pretrain as P  # noqa: E402

NAMES = ["split5", "eat5", "die10", "kill10"]


def auc(s, t):
    return P.auc(torch.from_numpy(s), torch.from_numpy(t))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("model")
    ap.add_argument("shards", nargs="+")
    ap.add_argument("--max", type=int, default=400000)
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--min-n", type=int, default=2000, help="smallest group reported")
    a = ap.parse_args()
    raw = np.concatenate([P.read_shard(p) for p in a.shards])[: a.max]
    raw = raw[(raw[:, 4] >> 7).astype(bool)]
    if not len(raw):
        sys.exit("no record carries consequence labels (build the shards with build_rl.py --aux)")
    m = P.Net(a.channels, a.hidden, aux=True)
    m.load_state_dict(torch.load(a.model, map_location="cpu"))
    m.eval()
    preds = []
    with torch.no_grad():
        for i in range(0, len(raw), 65536):
            b = raw[i:i + 65536]
            x = torch.from_numpy(b[:, P.HDR:].astype(np.float32) / 100.0)
            r = torch.from_numpy(b[:, 5].astype(np.float32) * 2 / 500.0).unsqueeze(1)
            preds.append(torch.sigmoid(m(x, r, with_aux=True)[2]).numpy())
    p = np.concatenate(preds)
    y = np.stack([(raw[:, 4] >> (k + 1)) & 1 for k in range(4)], 1).astype(np.float32)
    length, rnd = raw[:, 2].astype(int), raw[:, 5].astype(int) * 2
    print(f"{len(raw)} labelled records")
    for k, name in enumerate(NAMES):
        base = y[:, k].mean()
        order = np.argsort(p[:, k])
        dec = np.array_split(order, 10)
        cal = " ".join(f"{p[d, k].mean():.3f}/{y[d, k].mean():.3f}" for d in dec)
        top = dec[-1]
        print(f"\n{name}: base {base:.3f}, AUC {auc(p[:, k], y[:, k]):.3f}, top decile rate {y[top, k].mean():.3f} "
              f"({y[top, k].mean() / max(base, 1e-9):.1f}x base), top decile from p >= {p[top, k].min():.3f}")
        print(f"  calibration by decile (predicted/observed): {cal}")
        for lo, hi in ((2, 3), (4, 7), (8, 15), (16, 999)):
            s = (length >= lo) & (length <= hi)
            if s.sum() > a.min_n and 0 < y[s, k].mean() < 1:
                print(f"  length {lo}-{hi if hi < 999 else '+'}: n {s.sum()}, rate {y[s, k].mean():.3f}, AUC {auc(p[s, k], y[s, k]):.3f}")
        for lo, hi in ((0, 100), (100, 300), (300, 400), (400, 500)):
            s = (rnd >= lo) & (rnd < hi)
            if s.sum() > a.min_n and 0 < y[s, k].mean() < 1:
                print(f"  rounds {lo}-{hi}: n {s.sum()}, rate {y[s, k].mean():.3f}, AUC {auc(p[s, k], y[s, k]):.3f}")


if __name__ == "__main__":
    main()
