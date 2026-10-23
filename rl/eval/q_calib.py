#!/usr/bin/env python3
"""Calibration of an F2 net's qDie head: P(the dragon dies by enemy contact within 10 turns | the class it played)
against what followed, on build_f2.py --aux records (held-out games). Reports AUC, ECE over 10 equal-width bins (the
reliability curve), ECE over 10 equal-count bins, Brier and the mean prediction against the base rate - overall, by
phase, by dragon length and by what was played - and the 10-bin table. The Q heads learn from the taken class only, so
only the taken class is scored. Records are weighted by 1 / their keep rate (header byte 15), as in training.

  python3 rl/eval/q_calib.py --data "DATA_DIR/f2_*.bin" --model NAME=RUN_DIR/model.pt [--nmem 24 --q 6] [--json out.json]

A reasonable bar before a bot relies on the head: ECE below 0.03 and AUC above 0.85 on the bot's own games.
"""
import argparse
import glob
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "train"))
NFEAT, HDR = 843, 16


def load(paths, nmem):
    from train_f1 import read_shard
    return np.concatenate([read_shard(p, HDR + NFEAT + nmem) for p in paths])


def qdie(model_path, rec, nmem, q, channels, hidden, batch=16384):
    import torch
    from train_f1 import F1Net
    m = F1Net(channels, hidden, npriv=nmem, q=q)
    m.load_state_dict(torch.load(model_path, map_location="cpu"))
    m.eval()
    cls = rec[:, 0].astype(np.int64)
    out = np.empty(len(rec), np.float64)
    with torch.no_grad():
        for i in range(0, len(rec), batch):
            b = rec[i:i + batch]
            _, _, _, qq = m(torch.from_numpy(b[:, HDR:HDR + NFEAT].astype(np.float32) / 100),
                            torch.from_numpy(b[:, 5].astype(np.float32) * 2 / 500).unsqueeze(1),
                            torch.from_numpy(b[:, HDR + NFEAT:].astype(np.float32) / 255) if nmem else None)
            logit = qq[:, 16:32].numpy()[np.arange(len(b)), cls[i:i + batch]]  # qDie rows NA..2NA, the taken class
            out[i:i + len(b)] = 1 / (1 + np.exp(-logit))
    return out


def auc(p, y, w):
    """weighted rank AUC"""
    pos = y > 0.5
    if pos.all() or not pos.any():
        return float("nan")
    o = np.argsort(p, kind="stable")
    t, ww = pos[o], w[o]
    wn = np.cumsum(np.where(t, 0.0, ww))
    return float((ww[t] * wn[t]).sum() / (ww[t].sum() * ww[~t].sum()))


def calib(p, y, w):
    """dict: n, base rate, mean p, AUC, ECE (10 equal-width bins), ECE (10 equal-count bins), Brier, the table"""
    n = len(p)
    if n == 0:
        return {"n": 0}
    W = w.sum()
    bins = np.minimum((p * 10).astype(int), 9)
    table, ece = [], 0.0
    for b in range(10):
        s = bins == b
        if s.any():
            ws = w[s].sum()
            mp, my = (p[s] * w[s]).sum() / ws, (y[s] * w[s]).sum() / ws
            ece += ws / W * abs(mp - my)
            table.append([b / 10, int(s.sum()), round(float(mp), 4), round(float(my), 4)])
    q = np.argsort(p, kind="stable")
    ecq = 0.0
    for c in np.array_split(q, 10):
        if len(c):
            wc = w[c].sum()
            ecq += wc / W * abs((p[c] * w[c]).sum() / wc - (y[c] * w[c]).sum() / wc)
    return {"n": int(n), "rate": round(float((y * w).sum() / W), 4), "mean_p": round(float((p * w).sum() / W), 4),
            "auc": round(auc(p, y, w), 4), "ece10": round(float(ece), 4), "ece10q": round(float(ecq), 4),
            "brier": round(float((((p - y) ** 2) * w).sum() / W), 5), "table": table}


def groups(rec):
    rnd = rec[:, 5].astype(int) * 2
    cls = rec[:, 0].astype(int)
    return {"all": np.ones(len(rec), bool), "r0-99": rnd < 100, "r100-299": (rnd >= 100) & (rnd < 300), "r300+": rnd >= 300,
            "move1": cls < 4, "move2+": (cls >= 4) & (cls < 12), "split": (cls >= 12) & (cls < 15), "none": cls == 15,
            "L<=3": rec[:, 2] <= 3, "L4-7": (rec[:, 2] >= 4) & (rec[:, 2] <= 7), "L8+": rec[:, 2] >= 8}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", nargs="+", required=True, help="build_f2.py --aux shards (globs)")
    ap.add_argument("--model", action="append", required=True, help="NAME=model.pt (train_f1.py with Q heads)")
    ap.add_argument("--nmem", type=int, default=24)
    ap.add_argument("--q", type=int, default=6, help="the models' Q heads (2, 3 or 6)")
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--sample", type=float, default=1.0, help="score this random share of the turns (seed 0)")
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    paths = sorted({p for x in args.data for p in glob.glob(x)})
    rec = load(paths, args.nmem)
    if args.sample < 1:
        rec = rec[np.random.default_rng(0).random(len(rec)) < args.sample]
    has = (rec[:, 4] >> 7) & 1 == 1
    y = ((rec[:, 4] >> 3) & 1).astype(np.float64)
    w = 100.0 / np.maximum(rec[:, 15] >> 1, 1).astype(np.float64)  # 1 / keep rate: natural frequencies
    print(f"{len(rec)} turns from {len(paths)} files, {has.mean():.3f} with labels; die10 rate {y[has].mean():.4f}")
    res = {}
    gs = groups(rec)
    for spec in args.model:
        name, path = spec.split("=", 1)
        p = qdie(path, rec, args.nmem, args.q, args.channels, args.hidden)
        res[name] = {k: calib(p[s & has], y[s & has], w[s & has]) for k, s in gs.items()}
        a = res[name]["all"]
        print(f"\n{name}: AUC {a['auc']}, ECE10 {a['ece10']} (equal-count {a['ece10q']}), Brier {a['brier']}, "
              f"mean p {a['mean_p']} vs rate {a['rate']}")
        print(f"  {'group':10} {'n':>9} {'rate':>7} {'mean p':>7} {'AUC':>6} {'ECE10':>6} {'ECEq':>6}")
        for k, c in res[name].items():
            if c["n"]:
                print(f"  {k:10} {c['n']:9d} {c['rate']:7.4f} {c['mean_p']:7.4f} {c['auc']:6.3f} {c['ece10']:6.4f} {c['ece10q']:6.4f}")
        print("  reliability (all): bin, n, mean p, observed")
        for row in a["table"]:
            print(f"    {row[0]:.1f}  {row[1]:9d}  {row[2]:.4f}  {row[3]:.4f}")
    if args.json:
        json.dump(res, open(args.json, "w"), indent=1)


if __name__ == "__main__":
    main()
