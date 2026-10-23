#!/usr/bin/env python3
"""Train the full-action-head ("F1") net on build_f1.py / build_f2.py records (the released bot's move prior is an F1
net trained with this script, no extra inputs and no Q heads).

Input as every net here (17 x 7 x 7 grid + 10 scalars + the round), so the bot's rl_features.hpp serves it; with
--priv also the replay's facts at the turn (own / enemy longest, counts, totals, am-longest: privileged, masked at
random so the net learns to do without them). Heads:
  pol  16 actions: (steps 1 / 2 / 3+) x 4 first-step directions relative to the facing, split 2 / 3-4 / 5+, no action
  aux  the split fraction (logit of n / length) and the second step relative to the first (4)
  val  the win logit, the final longest-dragon gap / 32 and the final total-length gap / 128 (own - enemy)
  q    optional action-value heads, learned on the class that was played only (--q: 2, --q-returns: 3, --q-team: 6)
Each record is weighted by 1 / its keep rate (header byte 15), so the policy learns the natural action frequencies
(--rare-boost scales the rare ones), the losers' turns by --loser-weight, the rows' weight class as pretrain.py.
Warm start (--init) from a pretrain.py model: the trunk as is, the 9-class head spread over the 16 classes; or from an F1
model with fewer extra inputs (the new input columns start at zero); or resume an F1 model of the same shape.
F2 (--mem 24 / 44): build_f2.py's records carry the dragon's own memory inputs (rl/runtime/f2_memory.hpp) after the
features; they go in where --priv's facts would (F1_NPRIV = 24 or 44 in the export), never masked.

  python3 rl/train/train_f1.py "DATA_DIR/f1_*.bin" --out RUN_DIR [--init PRETRAIN_RUN/model.pt] [--channels 64 --hidden 128]

Writes RUN_DIR/model.pt, f1_weights.hpp (int16 rows with a float scale per row: the bot's format, no further export
step) and train_log.json. Shards may be zstd-compressed (.zst; needs the zstd command). NCPUS sets torch's threads;
RL_MPS=1 trains on an Apple GPU when torch supports it.
"""
import argparse
import glob
import json
import math
import os
import subprocess
import sys
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

FC, FS, NA = 17, 10, 16
FDIM = FC * 49 + FS
HDR = 16
NPRIV = 8  # own / enemy longest, own / enemy count, own / enemy total, am-longest, known
WCLASS = [1.0, 0.75, 0.5, 0.25]
GROUPS = {"move1": range(0, 4), "move2": range(4, 8), "move3+": range(8, 12), "split2": [12], "split3-4": [13],
          "split5+": [14], "none": [15]}


class F1Net(nn.Module):
    def __init__(self, c=64, h=128, npriv=0, q=0):
        super().__init__()
        self.npriv = npriv
        self.c1 = nn.Conv2d(FC, c, 3, padding=1)
        self.c2 = nn.Conv2d(c, c, 3, padding=1)
        self.f1 = nn.Linear(c * 49 + FS + 1 + self.npriv, h)
        self.pol = nn.Linear(h, NA)
        self.aux = nn.Linear(h, 5)
        self.val = nn.Linear(h, 3)
        # q = 2 (--q): P(eat within 5 | class), P(enemy-contact death within 10 | class) as logits; q = 3 (--q-returns):
        # the discounted future intake (pearls), the death logit, the team's longest dragon's discounted growth / 4, each
        # per class; q = 6 (--q-team): those three, then that dragon's growth over 50 rounds / 16, its death within 50
        # (logit), the total-length lead's change over 50 / 64
        self.nq = q
        self.q = nn.Linear(h, q * NA) if q else None

    def forward(self, x, rnd, priv=None):
        g = x[:, : FC * 49].view(-1, FC, 7, 7)
        s = x[:, FC * 49:]
        g = torch.relu(self.c2(torch.relu(self.c1(g))))
        parts = [g.flatten(1), s, rnd] + ([priv] if self.npriv else [])
        z = torch.relu(self.f1(torch.cat(parts, 1)))
        return self.pol(z), self.aux(z), self.val(z), (self.q(z) if self.q is not None else None)


def warm_start(model, path, dev):
    """A pretrain.py net (9 classes + split fraction, one value logit): the trunk as is, the head spread over F1's.
    An F1 net (16 classes) with fewer extra inputs: everything as is, the new input columns at zero."""
    sd = torch.load(path, map_location=dev)
    own = model.state_dict()
    if sd["pol.weight"].shape[0] == NA:
        for k, v in sd.items():
            if k not in own:
                continue
            if k == "f1.weight" and v.shape != own[k].shape:
                own[k].zero_()
                own[k][:, : v.shape[1]] = v
            elif v.shape == own[k].shape:
                own[k] = v
        if model.q is not None and ("q.weight" not in sd or sd["q.weight"].shape != own["q.weight"].shape):  # new Q heads
            own["q.weight"].zero_()
            own["q.bias"].zero_()
            if "q.weight" in sd and sd["q.weight"].shape[0] < own["q.weight"].shape[0]:  # 6 heads from a 3-head net: the first heads
                n = sd["q.weight"].shape[0]
                own["q.weight"][:n] = sd["q.weight"]
                own["q.bias"][:n] = sd["q.bias"]
        model.load_state_dict(own)
        return
    for k in ("c1.weight", "c1.bias", "c2.weight", "c2.bias"):
        own[k] = sd[k]
    if own["f1.weight"].shape == sd["f1.weight"].shape:
        own["f1.weight"] = sd["f1.weight"]
    else:  # privileged inputs appended: new columns start at zero
        own["f1.weight"].zero_()
        own["f1.weight"][:, : sd["f1.weight"].shape[1]] = sd["f1.weight"]
    own["f1.bias"] = sd["f1.bias"]
    pw, pb = sd["pol.weight"], sd["pol.bias"]
    w, b = own["pol.weight"], own["pol.bias"]
    for k in range(4):  # the old sprint class covered every 2+ step move
        w[k], b[k] = pw[k], pb[k]
        w[4 + k], b[4 + k] = pw[4 + k], pb[4 + k] - 0.2
        w[8 + k], b[8 + k] = pw[4 + k], pb[4 + k] - 1.6
    for j, off in ((12, 0.25), (13, 1.6), (14, 2.3)):  # split 2 / 3-4 / 5+ shares
        w[j], b[j] = pw[8], pb[8] - off
    w[15].zero_()
    b[15] = pb.max() - 6.0
    own["aux.weight"].zero_()
    own["aux.bias"].zero_()
    own["aux.weight"][0], own["aux.bias"][0] = pw[9], pb[9]
    own["val.weight"].zero_()
    own["val.bias"].zero_()
    own["val.weight"][0], own["val.bias"][0] = sd["val.weight"][0], sd["val.bias"][0]
    model.load_state_dict(own)


def cf(v):
    """A C++ float literal: '0f' / '1f' do not compile, '0.0f' / '1.0f' do."""
    t = f"{v:.9g}"
    return (t if any(c in t for c in ".eEn") else t + ".0") + "f"


def quantised(name, t):
    """int16 values with one float scale per output row (export_bot.py's scheme)."""
    m = t.detach().cpu().float().reshape(t.shape[0], -1).numpy()
    s = np.abs(m).max(1) / 32767.0
    s[s == 0] = 1e-12
    q = np.clip(np.rint(m / s[:, None]), -32767, 32767).astype(np.int64)
    return (f"inline constexpr int16_t {name}_Q[] = {{{','.join(map(str, q.ravel()))}}};\n"
            f"inline constexpr float {name}_S[] = {{{','.join(cf(v) for v in s)}}};\n")


def export(model, path, meta):
    def arr(name, t):
        return f"inline constexpr float {name}[] = {{{','.join(cf(v) for v in t.detach().cpu().numpy().ravel())}}};\n"
    with open(path, "w") as f:
        f.write("// GENERATED by rl/train/train_f1.py -- " + json.dumps(meta) + "\n#pragma once\n#include <cstdint>\n")
        f.write("namespace bc {\n")
        f.write(f"inline constexpr int F1_NC1 = {model.c1.out_channels};\ninline constexpr int F1_NH = {model.f1.out_features};\n"
                f"inline constexpr int F1_NA = {NA};\ninline constexpr int F1_NPRIV = {model.npriv};\n")
        if model.q is not None:
            f.write("#define F1_HAS_Q 1\n" + ("#define F1_Q_RET 1\n" if model.nq >= 3 else "")
                    + ("#define F1_Q_TEAM 1\n" if model.nq == 6 else ""))
            f.write(quantised("F1_W_Q", model.q.weight))
            f.write(arr("F1_B_Q", model.q.bias))
        for name, layer in (("F1_W_C1", model.c1), ("F1_W_C2", model.c2), ("F1_W_F1", model.f1), ("F1_W_POL", model.pol),
                            ("F1_W_AUX", model.aux), ("F1_W_VAL", model.val)):
            f.write(quantised(name, layer.weight))
            f.write(arr(name.replace("_W_", "_B_"), layer.bias))
        f.write("}  // namespace bc\n")


def read_shard(path, width):
    if path.endswith(".zst"):
        buf = subprocess.run(["zstd", "-dc", "-q", "-f", path], capture_output=True, check=True).stdout  # -f: links too
        return np.frombuffer(buf, dtype=np.uint8).reshape(-1, width)
    return np.fromfile(path, dtype=np.uint8).reshape(-1, width)


def load(files, max_samples, nmem=0):
    """Every record's header and features (or a random max_samples subset, the same share of each shard), filled shard
    by shard into preallocated arrays: the peak is the data plus one shard, not twice the data. files: paths, or
    (path, the file's memory inputs, self-play) tuples - a file with fewer memory inputs than nmem is padded with zeros
    (records of other bots' games carry no heard facts). Returns headers, features, memory, the self-play flags."""
    files = [f if isinstance(f, tuple) else (f, nmem, False) for f in files]
    counts = []
    for p, fm, _ in files:
        if p.endswith(".zst"):
            out = subprocess.run(f"zstd -dc -q '{p}' | wc -c", shell=True, capture_output=True, text=True, check=True)
            counts.append(int(out.stdout) // (HDR + FDIM + fm))
        else:
            counts.append(os.path.getsize(p) // (HDR + FDIM + fm))
    base = sum(k for k, (_, _, sp) in zip(counts, files) if not sp)  # --max-samples thins the demonstration records only
    share = min(1.0, max(max_samples - (sum(counts) - base), 0) / max(base, 1)) if max_samples else 1.0
    rng = np.random.default_rng(1)
    picks = [np.sort(rng.choice(k, int(round(k * share)), replace=False)) if share < 1 and not sp else None
             for k, (_, _, sp) in zip(counts, files)]
    n = sum(k if pk is None else len(pk) for pk, k in zip(picks, counts))
    H = np.empty((n, HDR), np.uint8)
    X = np.empty((n, FDIM), np.uint8)
    M = np.zeros((n, nmem), np.uint8)
    SPF = np.zeros(n, bool)
    o = 0
    for (p, fm, sp), pk in zip(files, picks):
        a = read_shard(p, HDR + FDIM + fm)
        if pk is not None:
            a = a[pk]
        H[o:o + len(a)] = a[:, :HDR]
        X[o:o + len(a)] = a[:, HDR:HDR + FDIM]
        M[o:o + len(a), :fm] = a[:, HDR + FDIM:]
        SPF[o:o + len(a)] = sp
        o += len(a)
        del a
    return H[:o], X[:o], M[:o], SPF[:o]


def auc(score, target, weight):
    """Weighted area under the ROC curve; 0.5 when a class is missing."""
    pos = target > 0.5
    if pos.all() or not pos.any():
        return 0.5
    o = np.argsort(score, kind="stable")
    s, t, w = score[o], pos[o], weight[o]
    wn = np.cumsum(np.where(t, 0.0, w))  # negative weight at or below each score
    return float((w[t] * wn[t]).sum() / (w[t].sum() * w[~t].sum()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples", nargs="*", help="record shards (globs; GLOB@nmem for shards with another memory width)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=4)
    ap.add_argument("--batch", type=int, default=4096)
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--init", default="", help="a pretrain.py model.pt (warm start) or a train_f1.py one (resume)")
    ap.add_argument("--hours", type=float, default=1.5, help="the cosine schedule is fitted to this budget")
    ap.add_argument("--loser-weight", type=float, default=0.3)
    ap.add_argument("--rare-boost", type=float, default=1.0, help="x the weight of every action but a single step")
    ap.add_argument("--value-weight", type=float, default=0.5)
    ap.add_argument("--priv", action="store_true", help="the replay's facts as inputs (masked at --priv-drop)")
    ap.add_argument("--priv-drop", type=float, default=0.3)
    ap.add_argument("--mem", type=int, default=0, help="F2: this many memory inputs after the features (build_f2.py: 24)")
    ap.add_argument("--q", action="store_true", help="two action-value heads on the records' consequence labels (build_f2.py --aux)")
    ap.add_argument("--q-weight", type=float, default=1.0)
    ap.add_argument("--sp", nargs="*", default=[], help="expert iteration: the records of the expert bot's own games "
                    "(rl/selfplay -> build_f2.py), GLOB[@nmem]; their policy loss is advantage-weighted, not loser-weighted")
    ap.add_argument("--sp-share", type=float, default=0.5, help="the self-play records' share of the total weight")
    ap.add_argument("--sp-share-value", type=float, default=-1.0, help="their share for the value / Q heads (default --sp-share)")
    ap.add_argument("--awr-beta", type=float, default=0.5, help="0: off. Self-play policy weight exp(A / beta), A = the lineage "
                    "intake return (+ --awr-king x the king return) - its expectation under the policy and the Q heads")
    ap.add_argument("--awr-king", type=float, default=0.2)
    ap.add_argument("--sp-policy", type=float, default=1.0, help="x the self-play records' policy weight (0: they train the "
                    "value and Q heads only - values on the expert's own distribution, the policy anchored to the demonstrations)")
    ap.add_argument("--expert-model", default="", help="the expert's model.pt: self-play records whose executed class is not "
                    "the expert net's top class (a veto or a re-ranking changed it) leave the policy loss")
    ap.add_argument("--expert-mem", type=int, default=24, help="the expert net's memory inputs (the first ones are used)")
    ap.add_argument("--expert-q", type=int, default=3)
    ap.add_argument("--expert-target", default="agree", choices=("agree", "rerank", "soft"), help="agree: keep the self-play turns "
                    "where the executed class was the expert net's top; rerank: the policy learns the expert net's "
                    "re-ranked top class on every self-play turn (its own choice before the safety layer, no veto bias); "
                    "soft: the expert's whole distribution with its top three single-step moves re-weighted by "
                    "exp(lambda qEat - mu qDie + kappa qKing) at their total mass (pi_new ~ pi exp(Q): no class loses mass)")
    ap.add_argument("--expert-lqk", default="1,2,0.2", help="the expert's re-ranking weights lambda, mu, kappa")
    ap.add_argument("--sp-loser-weight", type=float, default=1.0, help="the self-play records' policy weight in lost games "
                    "(imitate the won games; with --awr-beta 0 the game result is the only signal)")
    ap.add_argument("--seed", type=int, default=0, help="the data order, the validation split and fresh layers' init (replicas)")
    ap.add_argument("--freeze-trunk", action="store_true", help="train the heads only (c1 / c2 / f1 kept): a small step from the base")
    ap.add_argument("--q-only", action="store_true", help="critic retraining: only "
                    "the Q heads learn - trunk, policy, aux and value heads frozen - and they start fresh from --seed (the Q "
                    "layer of --init is dropped: no resume). Fresh heads need the steps: 3 epochs at lr 3e-3 over 1.5 M "
                    "demonstration records reached qDie AUC 0.88 held out in our runs, close to a jointly trained head)")
    ap.add_argument("--q-warm", action="store_true", help="with --q-only: keep --init's Q layer and fine-tune it instead")
    ap.add_argument("--q-team", action="store_true", help="six heads: --q-returns' three plus the team heads (bytes 11-13: "
                    "the king's growth over 50 rounds, its death within 50, the lead's change over 50)")
    ap.add_argument("--q-early-w", type=float, default=1.0, help="the Q heads' weight before --q-late-from (e.g. 0.3 with --q-team)")
    ap.add_argument("--q-late-from", type=int, default=300)
    ap.add_argument("--q-returns", action="store_true", help="three heads - the discounted intake (header byte 9 / 25), "
                    "enemy-contact death, the king's discounted growth ((byte 10 - 64) / 4) - regression for the returns")
    ap.add_argument("--max-samples", type=int, default=0, help="keep a random subset of this size (0: all)")
    ap.add_argument("--min-samples", type=int, default=10000)
    args = ap.parse_args()
    if not args.samples and not args.sp:
        ap.error("give record shards (positional) and / or --sp self-play shards")
    os.makedirs(args.out, exist_ok=True)
    torch.set_num_threads(int(os.environ.get("NCPUS", os.cpu_count() or 8)))
    dev = "cuda" if torch.cuda.is_available() else "mps" if os.environ.get("RL_MPS") and torch.backends.mps.is_available() else "cpu"  # RL_MPS=1: an Apple GPU
    def expand(patterns, sp):  # GLOB or GLOB@nmem
        out = []
        for pat in patterns:
            g, _, fm = pat.partition("@")
            out += [(p, int(fm) if fm else args.mem, sp) for p in sorted(glob.glob(g))]
        return out
    entries = expand(args.samples, False) + expand(args.sp, True)
    files = [e[0] for e in entries]
    raw, feats, mem, SPF = load(entries, args.max_samples, args.mem)  # raw: the 16 header bytes only
    n = len(raw)
    if n < args.min_samples:
        sys.exit(f"only {n} samples in {len(files)} files")
    label = raw[:, 0].astype(np.int64)
    rate = np.maximum((raw[:, 15] >> 1).astype(np.float32), 1.0) / 100.0
    iw = 1.0 / rate
    iw *= np.where(label >= 4, args.rare_boost, 1.0).astype(np.float32)
    won = (raw[:, 4] & 1).astype(np.float32)
    wcls = np.array(WCLASS, dtype=np.float32)[(raw[:, 4] >> 5) & 3]
    wpol = (iw * np.where(won > 0.5, 1.0, np.where(SPF, args.sp_loser_weight, args.loser_weight)) * wcls).astype(np.float32)
    wval = (iw * wcls).astype(np.float32)
    if SPF.any() and (~SPF).any():  # expert iteration: the self-play records hold --sp-share of the weight
        for w, share in ((wpol, args.sp_share), (wval, args.sp_share_value if args.sp_share_value >= 0 else args.sp_share)):
            sp_sum, demo_sum = w[SPF].sum(), w[~SPF].sum()
            if sp_sum > 0 and demo_sum > 0:  # (no won self-play game with --sp-loser-weight 0: nothing to share)
                w[SPF] *= share / sp_sum
                w[~SPF] *= (1 - share) / demo_sum
                w *= len(w) / w.sum()
        wpol[SPF] *= args.sp_policy
        if args.expert_model and args.sp_policy > 0:  # learn only the expert's own choices, not what its safety layer made of them
            em = F1Net(args.channels, args.hidden, npriv=args.expert_mem, q=args.expert_q)
            em.load_state_dict(torch.load(args.expert_model, map_location="cpu"))
            em.eval()
            sp_idx = np.where(SPF)[0]
            top = np.empty(len(sp_idx), np.int64)
            soft = np.zeros((len(sp_idx), NA), np.float32) if args.expert_target == "soft" else None
            lmb, mu, kap = (float(v) for v in args.expert_lqk.split(","))
            with torch.no_grad():
                for i in range(0, len(sp_idx), 16384):
                    j = sp_idx[i:i + 16384]
                    pe, _, _, qe = em(torch.from_numpy(feats[j]).float() / 100.0,
                                      torch.from_numpy(raw[j, 5].astype(np.float32) * 2 / 500.0).unsqueeze(1),
                                      torch.from_numpy(mem[j, :args.expert_mem]).float() / 255.0 if args.expert_mem else None)
                    if args.expert_target == "soft" and qe is not None and em.nq >= 3:
                        pr = torch.softmax(pe, 1)
                        top3 = pr.argsort(1, descending=True)[:, :3]
                        adj = torch.exp(lmb * qe.gather(1, top3) - mu * torch.sigmoid(qe.gather(1, top3 + NA))
                                        + kap * 4 * qe.gather(1, top3 + 2 * NA))
                        mv = (top3 < 4).float()
                        pm = pr.gather(1, top3) * mv
                        new = pm * adj
                        new = new * (pm.sum(1, keepdim=True) / new.sum(1, keepdim=True).clamp_min(1e-12))
                        tgt = pr.clone()
                        tgt.scatter_(1, top3, torch.where(mv > 0, new, pr.gather(1, top3)))
                        soft[i:i + len(j)] = tgt.numpy()
                        top[i:i + len(j)] = tgt.argmax(1).numpy()
                    elif args.expert_target == "rerank" and qe is not None and em.nq >= 3:
                        # the expert's re-ranking: the top three classes' single-step moves by log p + lambda qEat
                        # - mu qDie + kappa qKing, splits / sprints / no-action keep their places
                        pr = torch.softmax(pe, 1)
                        top3 = pr.argsort(1, descending=True)[:, :3]
                        sc = (torch.log(pr.gather(1, top3).clamp_min(1e-6)) + lmb * qe.gather(1, top3)
                              - mu * torch.sigmoid(qe.gather(1, top3 + NA)) + kap * 4 * qe.gather(1, top3 + 2 * NA))
                        sc = torch.where(top3 < 4, sc, torch.full_like(sc, -1e9))
                        best = top3.gather(1, sc.argmax(1, keepdim=True))[:, 0]
                        top[i:i + len(j)] = torch.where(top3[:, 0] < 4, best, top3[:, 0]).numpy()
                    else:
                        top[i:i + len(j)] = pe.argmax(1).numpy()
            if args.expert_target == "soft":
                print(f"expert target: its re-weighted distribution on every self-play turn (top class equal to the "
                      f"executed one {(top == label[sp_idx]).mean():.3f})", flush=True)
                SOFT = torch.zeros((len(label), NA))  # soft targets on the self-play rows (zero elsewhere)
                SOFT[torch.from_numpy(sp_idx)] = torch.from_numpy(soft)
            elif args.expert_target == "rerank":
                print(f"expert target: its re-ranked top class on every self-play turn ({(top == label[sp_idx]).mean():.3f} "
                      f"equal to the executed class)", flush=True)
                label_pol = label.copy()
                label_pol[sp_idx] = top
            else:
                agree = top == label[sp_idx]
                wpol[sp_idx[~agree]] = 0.0
                print(f"expert agreement on self-play records {agree.mean():.3f}: the rest leave the policy loss", flush=True)
        print(f"self-play records {SPF.sum()} of {len(SPF)}, weight share {args.sp_share}, policy x {args.sp_policy}", flush=True)
    if "label_pol" not in locals():
        label_pol = label
    if "SOFT" not in locals():
        SOFT = None
    nat = np.bincount(label, weights=iw, minlength=NA)
    print(f"{n} samples from {len(files)} shards on {dev}; stored classes {np.bincount(label, minlength=NA).tolist()}; "
          f"natural shares {', '.join(f'{g} {sum(nat[list(r)]) / nat.sum():.4f}' for g, r in GROUPS.items())}; "
          f"winners' turns {won.mean():.2f}", flush=True)
    rng = np.random.default_rng(args.seed)
    torch.manual_seed(args.seed)
    idx = rng.permutation(n)
    val, tr = idx[: n // 25], idx[n // 25:]
    X = torch.from_numpy(feats)  # uint8, converted per batch
    rnd = torch.from_numpy(raw[:, 5].astype(np.float32) * 2 / 500.0).unsqueeze(1)
    Y = torch.from_numpy(label_pol)   # the policy's targets (--expert-target rerank: the expert's re-ranked choices on self-play turns)
    YQ = torch.from_numpy(label)      # the executed classes: the Q heads learn what followed them
    frac = torch.from_numpy((raw[:, 1] / np.maximum(raw[:, 2], 1)).astype(np.float32).clip(0, 1))
    dir2 = torch.from_numpy(raw[:, 8].astype(np.int64))
    kgap = torch.from_numpy((raw[:, 6].astype(np.float32) - 128) / 32.0)
    tgap = torch.from_numpy((raw[:, 7].astype(np.float32) - 128) / 32.0)
    W = torch.from_numpy(won)
    SPT = torch.from_numpy(SPF)
    WP, WV = torch.from_numpy(wpol), torch.from_numpy(wval)
    if args.mem:  # F2: the dragon's own memory, what the bot computes (uint8, converted per batch)
        PRIV = torch.from_numpy(mem)
    else:
        PRIV = torch.from_numpy(np.concatenate([raw[:, 9:11] / 32.0, raw[:, 11:13] / 32.0, raw[:, 13:15] / 32.0,
                                                (raw[:, 15:16] & 1), np.ones((n, 1))], 1).astype(np.float32))
    model = F1Net(args.channels, args.hidden, npriv=args.mem or (NPRIV if args.priv else 0),
                  q=6 if args.q_team else 3 if args.q_returns else 2 if args.q else 0).to(dev)
    TKG = torch.from_numpy((raw[:, 11].astype(np.float32) - 64.0) / 16.0)   # --q-team: the longest dragon's growth over 50 / 16
    TKD = torch.from_numpy((raw[:, 12] > 0).astype(np.float32))              # the king dies within 50
    TTOT = torch.from_numpy((raw[:, 13].astype(np.float32) - 128.0) * 4.0 / 64.0)  # the lead's change over 50 / 64
    QW = torch.from_numpy(np.where(raw[:, 5].astype(np.int64) * 2 >= args.q_late_from, 1.0, args.q_early_w).astype(np.float32))
    GEAT = torch.from_numpy(raw[:, 9].astype(np.float32) / 25.0)             # --q-returns: discounted intake (pearls)
    GKING = torch.from_numpy((raw[:, 10].astype(np.float32) - 64.0) / 16.0)  # the king's discounted growth / 4
    HAS = torch.from_numpy(((raw[:, 4] >> 7) & 1).astype(np.float32))       # the record carries consequence labels
    EAT = torch.from_numpy(((raw[:, 4] >> 2) & 1).astype(np.float32))       # it ate within 5 turns
    DIE = torch.from_numpy(((raw[:, 4] >> 3) & 1).astype(np.float32))       # it died by enemy contact within 10
    if args.q or args.q_returns or args.q_team:
        print(f"consequence labels on {HAS.mean():.3f} of the records: eat5 {EAT[HAS > 0].mean():.3f}, "
              f"die10 {DIE[HAS > 0].mean():.3f}" + (f"; intake return {GEAT.mean():.3f} (sd {GEAT.std():.3f}), king "
              f"return / 4 {GKING.mean():.3f} (sd {GKING.std():.3f})" if args.q_returns else ""), flush=True)
    if args.init:
        sd = torch.load(args.init, map_location=dev)
        mine = model.state_dict()
        if set(sd) == set(mine) and all(sd[k].shape == mine[k].shape for k in sd):
            model.load_state_dict(sd)
            print(f"resumed from {args.init}", flush=True)
        else:
            warm_start(model, args.init, dev)
            print(f"warm start from {args.init} (a 9-class head spread over 16, new inputs at zero)", flush=True)
    if args.freeze_trunk or args.q_only:
        for layer in (model.c1, model.c2, model.f1) + ((model.pol, model.aux, model.val) if args.q_only else ()):
            for prm in layer.parameters():
                prm.requires_grad_(False)
    if args.q_only:
        if model.q is None:
            sys.exit("--q-only needs Q heads (--q / --q-returns / --q-team)")
        if not args.q_warm:
            model.q.reset_parameters()  # fresh, from --seed's generator
        print(f"Q heads only, {'fine-tuned from --init' if args.q_warm else f're-initialised (seed {args.seed})'}; "
              f"frozen: trunk, policy, aux, value", flush=True)
    opt = torch.optim.AdamW([prm for prm in model.parameters() if prm.requires_grad], lr=args.lr, weight_decay=1e-4)
    spe = math.ceil(len(tr) / args.batch)
    total = args.epochs * spe
    step, t0 = 0, time.time()
    budget = args.hours * 3600 * 0.93
    log = []

    def batch(b, train):
        xb = X[b].to(dev).float() / 100.0
        pv = None
        if args.mem:
            pv = PRIV[b].to(dev).float() / 255.0
        elif args.priv:
            pv = PRIV[b].to(dev)
            if train:  # the bot only estimates these: learn to do without them too
                keep = (torch.rand(len(b), 1, device=dev) >= args.priv_drop).float()
                pv = pv * keep
        return model(xb, rnd[b].to(dev), pv)

    def evaluate():
        model.eval()
        outs = []
        with torch.no_grad():
            for i in range(0, len(val), 16384):
                b = torch.from_numpy(val[i:i + 16384])
                p, a, v, qq = batch(b, False)
                if qq is not None:  # the taken action's heads
                    yqb = YQ[b].to(dev)
                    qq = torch.stack([qq.gather(1, (yqb + j * NA)[:, None])[:, 0] for j in range(model.nq)], 1)
                outs.append((F.softmax(p, 1).cpu(), a.cpu(), v.cpu(), qq.cpu() if qq is not None else None))
        prob = torch.cat([o[0] for o in outs]).numpy()
        a = torch.cat([o[1] for o in outs])
        v = torch.cat([o[2] for o in outs])
        yv, wv = label[val], iw[val]
        nll = float(-(np.log(prob[np.arange(len(yv)), yv] + 1e-9) * wv).sum() / wv.sum())
        m1 = yv < 4
        rec = {"nll": round(nll, 4),
               "acc_nat": round(float(((prob.argmax(1) == yv) * wv).sum() / wv.sum()), 4),
               "move1_dir_acc": round(float((prob[m1, :4].argmax(1) == yv[m1]).mean()), 4)}
        for g, r in (("split", [12, 13, 14]), ("none", [15]), ("sprint", list(range(4, 12)))):
            rec[f"auc_{g}"] = round(auc(prob[:, r].sum(1), np.isin(yv, r).astype(np.float32), wv), 4)
        sp = np.isin(yv, [12, 13, 14])
        spt = torch.from_numpy(sp)
        rec["split_bucket_acc"] = round(float((prob[sp][:, 12:15].argmax(1) + 12 == yv[sp]).mean()), 4) if sp.any() else 0
        rec["frac_mae"] = round(float((torch.sigmoid(a[spt, 0]) - frac[val][spt]).abs().mean()), 4) if sp.any() else 0
        d2 = dir2[val] != 255
        rec["dir2_acc"] = round(float((a[d2, 1:5].argmax(1) == dir2[val][d2]).float().mean()), 4) if d2.any() else 0
        rec["value_acc"] = round(float(((v[:, 0] > 0).float() == W[val]).float().mean()), 4)
        rec["kgap_mae"] = round(float((v[:, 1] - kgap[val]).abs().mean() * 32), 2)
        if model.q is not None:  # how well the taken action's heads rank / fit what followed (records with labels)
            qv = torch.cat([o[3] for o in outs]).numpy()
            hv = HAS[val].numpy() > 0
            def r2(pred, t):
                return round(float(1 - ((pred - t) ** 2).mean() / max(t.var(), 1e-9)), 4)
            if model.nq == 6:
                late = (raw[val, 5].astype(np.int64) * 2 >= args.q_late_from) & hv
                rec["r2_q_kg_late"] = r2(qv[late, 3], TKG[val].numpy()[late])
                rec["auc_q_kd_late"] = round(auc(qv[late, 4], TKD[val].numpy()[late], wv[late]), 4)
                rec["r2_q_tot_late"] = r2(qv[late, 5], TTOT[val].numpy()[late])
            if model.nq >= 3:
                rec["r2_q_eat"] = r2(qv[hv, 0], GEAT[val].numpy()[hv])
                rec["auc_q_die"] = round(auc(qv[hv, 1], DIE[val].numpy()[hv], wv[hv]), 4)
                rec["r2_q_king"] = r2(qv[hv, 2], GKING[val].numpy()[hv])
            else:
                rec["auc_q_eat"] = round(auc(qv[hv, 0], EAT[val].numpy()[hv], wv[hv]), 4)
                rec["auc_q_die"] = round(auc(qv[hv, 1], DIE[val].numpy()[hv], wv[hv]), 4)
        model.train()
        return rec

    print(json.dumps({"epoch": 0, **evaluate()}), flush=True)
    model.train()
    for ep in range(args.epochs):
        perm = torch.from_numpy(rng.permutation(tr))
        for i in range(0, len(perm), args.batch):
            if step == 200:  # fit the schedule to the time budget
                per = (time.time() - t0) / step
                total = min(total, int(budget / per))
                print(f"{per * 1000:.0f} ms a step: {total} steps ({total / spe:.2f} epochs) in the budget", flush=True)
            if step >= total:
                break
            for g in opt.param_groups:
                g["lr"] = args.lr * 0.5 * (1 + math.cos(math.pi * step / total))
            b = perm[i:i + args.batch]
            yb, wp, wv = Y[b].to(dev), WP[b].to(dev), WV[b].to(dev)
            pol, aux, v, qq = batch(b, True)
            spb = SPT[b].to(dev)
            if args.awr_beta > 0 and qq is not None and model.nq >= 3 and bool(spb.any()):  # AWR on the expert's own turns
                with torch.no_grad():
                    pi = F.softmax(pol, 1)
                    ge = GEAT[b].to(dev) + args.awr_king * GKING[b].to(dev)
                    base = (pi * (qq[:, :NA] + args.awr_king * qq[:, 2 * NA:3 * NA])).sum(1)  # the policy's expectation
                    adv = torch.exp(((ge - base) / args.awr_beta).clamp(-3, 3))
                    wp = torch.where(spb, wp * adv, wp)
            ce = F.cross_entropy(pol, yb, reduction="none")
            if SOFT is not None:  # --expert-target soft on the self-play rows
                st = SOFT[b].to(dev)
                has = st.sum(1) > 0
                if bool(has.any()):
                    ce = torch.where(has, -(st * F.log_softmax(pol, 1)).sum(1), ce)
            loss = (ce * wp).sum() / wp.sum()
            sp = yb >= 12
            sp &= yb <= 14
            if sp.any():
                loss = loss + 0.5 * F.binary_cross_entropy_with_logits(aux[sp, 0], frac[b].to(dev)[sp])
            d2 = dir2[b].to(dev)
            m2 = d2 != 255
            if m2.any():
                loss = loss + 0.25 * F.cross_entropy(aux[m2, 1:5], d2[m2])
            lv = (F.binary_cross_entropy_with_logits(v[:, 0], W[b].to(dev), reduction="none")
                  + 0.5 * F.smooth_l1_loss(v[:, 1], kgap[b].to(dev), reduction="none")
                  + 0.25 * F.smooth_l1_loss(v[:, 2], tgap[b].to(dev), reduction="none"))
            loss = loss + args.value_weight * (lv * wv).sum() / wv.sum()
            if qq is not None:  # only the taken action has a label: its heads
                hb = HAS[b].to(dev) * wv * QW[b].to(dev)
                if hb.sum() > 0:
                    yq = YQ[b].to(dev)
                    qe = qq.gather(1, yq[:, None])[:, 0]
                    qd = qq.gather(1, (yq + NA)[:, None])[:, 0]
                    lq = F.binary_cross_entropy_with_logits(qd, DIE[b].to(dev), reduction="none")
                    if model.nq >= 3:
                        qk = qq.gather(1, (yq + 2 * NA)[:, None])[:, 0]
                        lq = lq + F.smooth_l1_loss(qe, GEAT[b].to(dev), reduction="none") \
                            + F.smooth_l1_loss(qk, GKING[b].to(dev), reduction="none")
                    if model.nq == 6:
                        g = [qq.gather(1, (yq + j * NA)[:, None])[:, 0] for j in (3, 4, 5)]
                        lq = lq + F.smooth_l1_loss(g[0], TKG[b].to(dev), reduction="none") \
                            + F.binary_cross_entropy_with_logits(g[1], TKD[b].to(dev), reduction="none") \
                            + F.smooth_l1_loss(g[2], TTOT[b].to(dev), reduction="none")
                    elif model.nq == 2:  # the 2-head variant: qe is the eat-within-5 logit
                        lq = lq + F.binary_cross_entropy_with_logits(qe, EAT[b].to(dev), reduction="none")
                    loss = loss + args.q_weight * (lq * hb).sum() / hb.sum()
            opt.zero_grad()
            loss.backward()
            opt.step()
            step += 1
        log.append({"epoch": ep + 1, "step": step, "loss": round(loss.item(), 4), **evaluate()})
        print(json.dumps(log[-1]), flush=True)
        torch.save(model.state_dict(), os.path.join(args.out, "model.pt"))
        if step >= total:
            break
    meta = {"samples": int(n), "shards": len(files), "priv": args.priv, "mem": args.mem, **log[-1]}
    export(model, os.path.join(args.out, "f1_weights.hpp"), meta)
    json.dump({"meta": meta, "log": log}, open(os.path.join(args.out, "train_log.json"), "w"), indent=1)
    print("done", json.dumps(meta), flush=True)


if __name__ == "__main__":
    main()
