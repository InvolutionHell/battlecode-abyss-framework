#!/usr/bin/env python3
"""Reference forward passes in numpy (float64), read straight from a generated weights header - the same numbers the
bot dequantises (int16 rows x a float scale per row, float biases), computed by an independent implementation (no
torch, no C++). Used by make_vectors.py for the consistency test and by rl/tests to check the exports.

  F1Ref(header)   F1 / F2 nets (train_f1.py's f1_weights.hpp, or the released bot's r4_weights.hpp: F1_* symbols)
  RLRef(header)   9-class nets (export_bot.py's rl_weights.hpp: RL_* symbols, or another --prefix)
  ref_for(header) the right one for a header

Input per turn: the record's 843 feature bytes (the observation quantised as round(v x 100), v clipped to [0, 2.55]),
its round byte (min(round / 2, 255)) and, for F2 nets, its memory bytes (F2Memory::features quantised as round(v x 255)):
exactly what the featurizers write and what the bot feeds the net after quantising its own inputs like the records.

  python3 rl/eval/consistency/ref_forward.py WEIGHTS.hpp RECORDS.bin [--header-bytes 16] [--n 3]
"""
import argparse
import re

import numpy as np

FC, FS = 17, 10
NFEAT = FC * 49 + FS  # 843


def parse_header(path, prefix):
    s = open(path).read()
    arrays = {}
    for typ, name, body in re.findall(r"inline constexpr (int16_t|float) (%s_\w+)\[\] = \{([^}]*)\};" % prefix, s):
        vals = [v.strip().rstrip("f") for v in body.split(",") if v.strip()]
        arrays[name] = np.array(vals, dtype=np.float64 if typ == "float" else np.int64)
        if typ == "float":  # the header's floats are float32 literals: round-trip them through float32
            arrays[name] = arrays[name].astype(np.float32)
    ints = {k: int(v) for k, v in re.findall(r"inline constexpr int (%s_\w+) = (\d+);" % prefix, s)}
    defs = set(re.findall(r"#define (%s_\w+) 1" % prefix, s))
    return arrays, ints, defs


def _deq(a, name, rows):
    """the bot's dequantisation: float(int16) x float scale, a float32 product"""
    q = a[name + "_Q"].astype(np.float32).reshape(rows, -1)
    return (q * a[name + "_S"].reshape(rows, 1)).astype(np.float64)


def _conv(x, w, b):
    """3 x 3, zero padding, relu: x [N, cin, 7, 7] -> [N, cout, 7, 7]"""
    n = x.shape[0]
    xp = np.zeros((n, x.shape[1], 9, 9))
    xp[:, :, 1:8, 1:8] = x
    out = np.broadcast_to(b[None, :, None, None], (n, w.shape[0], 7, 7)).copy()
    for kr in range(3):
        for kc in range(3):
            out += np.einsum("nirc,oi->norc", xp[:, :, kr:kr + 7, kc:kc + 7], w[:, :, kr, kc])
    return np.maximum(out, 0)


def _sig(v):
    return 1 / (1 + np.exp(-v))


def _softmax(v):
    e = np.exp(v - v.max(1, keepdims=True))
    return e / e.sum(1, keepdims=True)


def _inputs(feat, rnd):
    """the bot's float32 inputs: byte / 100 and min(round / 2, 255) x 2 / 500"""
    feat = np.asarray(feat).reshape(-1, NFEAT)
    n = feat.shape[0]
    x = (feat.astype(np.float32) / np.float32(100)).astype(np.float64)
    r = (np.asarray(rnd).reshape(n).astype(np.float32) * np.float32(2) / np.float32(500)).astype(np.float64)
    return x, r, n


class F1Ref:
    """train_f1.py's F1Net: 16 classes, split fraction, second step, value (win, longest gap, total gap), Q heads"""
    kind = "f1"

    def __init__(self, header, prefix="F1"):
        a, ints, self.defs = parse_header(header, prefix)
        p = prefix
        self.C, self.H, self.NPRIV, self.NA = ints[p + "_NC1"], ints[p + "_NH"], ints[p + "_NPRIV"], ints[p + "_NA"]
        assert self.NA == 16
        self.nq = 6 if p + "_Q_TEAM" in self.defs else 3 if p + "_Q_RET" in self.defs else 2 if p + "_HAS_Q" in self.defs else 0
        self.ret = p + "_Q_RET" in self.defs
        C, H, NA = self.C, self.H, self.NA
        self.w1 = _deq(a, p + "_W_C1", C).reshape(C, FC, 3, 3)
        self.w2 = _deq(a, p + "_W_C2", C).reshape(C, C, 3, 3)
        self.wf = _deq(a, p + "_W_F1", H)
        self.wp, self.wa, self.wv = _deq(a, p + "_W_POL", NA), _deq(a, p + "_W_AUX", 5), _deq(a, p + "_W_VAL", 3)
        self.b1, self.b2, self.bf = (a[f"{p}_B_{k}"].astype(np.float64) for k in ("C1", "C2", "F1"))
        self.bp, self.ba, self.bv = (a[f"{p}_B_{k}"].astype(np.float64) for k in ("POL", "AUX", "VAL"))
        if self.nq:
            self.wq, self.bq = _deq(a, p + "_W_Q", self.nq * NA), a[p + "_B_Q"].astype(np.float64)
        assert self.wf.shape[1] == C * 49 + FS + 1 + self.NPRIV

    def forward(self, feat, rnd, mem=None):
        """feat uint8 [N, 843], rnd uint8 [N] (the record's round / 2 byte), mem uint8 [N, NPRIV] -> dict of float64"""
        x, r, n = _inputs(feat, rnd)
        pv = np.zeros((n, self.NPRIV))
        if self.NPRIV:
            pv = (np.asarray(mem).reshape(n, self.NPRIV).astype(np.float32) / np.float32(255)).astype(np.float64)
        a1 = _conv(x[:, :FC * 49].reshape(n, FC, 7, 7), self.w1, self.b1)
        a2 = _conv(a1, self.w2, self.b2)
        z = np.concatenate([a2.reshape(n, -1), x[:, FC * 49:], r[:, None], pv], 1)
        h = np.maximum(z @ self.wf.T + self.bf, 0)
        logits = h @ self.wp.T + self.bp
        prob = _softmax(logits)
        aux = h @ self.wa.T + self.ba
        val = h @ self.wv.T + self.bv
        NA = self.NA
        out = {"logits": logits, "prob": prob, "splitProb": prob[:, 12:15].sum(1), "splitFrac": _sig(aux[:, 0]),
               "step2": _softmax(aux[:, 1:5]), "win": _sig(val[:, 0]), "kingGap": val[:, 1] * 32,
               "totalGap": val[:, 2] * 128}
        if self.nq:
            q = h @ self.wq.T + self.bq
            out["qEat"] = q[:, :NA] if self.ret else _sig(q[:, :NA])
            out["qDie"] = _sig(q[:, NA:2 * NA])
            if self.ret:
                out["qKing"] = q[:, 2 * NA:3 * NA] * 4
            if self.nq == 6:
                out["qKG"] = q[:, 3 * NA:4 * NA] * 16
                out["qKD"] = _sig(q[:, 4 * NA:5 * NA])
                out["qTot"] = q[:, 5 * NA:6 * NA] * 64
        return out

    # the order check.cpp reads them in
    ORDER = ["logits", "prob", "splitFrac", "step2", "win", "kingGap", "totalGap", "qEat", "qDie", "qKing", "qKG", "qKD",
             "qTot"]


class RLRef:
    """pretrain.py's Net: 9 classes + the split fraction logit, the value logit, optional auxiliary heads"""
    kind = "rl"
    NPRIV = 0

    def __init__(self, header, prefix="RL"):
        a, ints, defs = parse_header(header, prefix)
        p = prefix
        self.C, self.H = ints[p + "_NC1"], ints[p + "_NH"]
        self.naux = ints.get(p + "_NAUX", 0) if p + "_HAS_AUX" in defs else 0
        C, H = self.C, self.H
        self.w1 = _deq(a, p + "_W_C1", C).reshape(C, FC, 3, 3)
        self.w2 = _deq(a, p + "_W_C2", C).reshape(C, C, 3, 3)
        self.wf = _deq(a, p + "_W_F1", H)
        self.wp, self.wv = _deq(a, p + "_W_POL", 10), _deq(a, p + "_W_VAL", 1)
        self.b1, self.b2, self.bf = (a[f"{p}_B_{k}"].astype(np.float64) for k in ("C1", "C2", "F1"))
        self.bp, self.bv = a[p + "_B_POL"].astype(np.float64), a[p + "_B_VAL"].astype(np.float64)
        if self.naux:
            self.wa, self.ba = _deq(a, p + "_W_AUX", self.naux), a[p + "_B_AUX"].astype(np.float64)
        assert self.wf.shape[1] == C * 49 + FS + 1

    def forward(self, feat, rnd, mem=None):
        x, r, n = _inputs(feat, rnd)
        a1 = _conv(x[:, :FC * 49].reshape(n, FC, 7, 7), self.w1, self.b1)
        a2 = _conv(a1, self.w2, self.b2)
        z = np.concatenate([a2.reshape(n, -1), x[:, FC * 49:], r[:, None]], 1)
        h = np.maximum(z @ self.wf.T + self.bf, 0)
        logits = h @ self.wp.T + self.bp
        out = {"logits": logits, "prob": _softmax(logits[:, :9]), "splitFrac": _sig(logits[:, 9]),
               "value": (h @ self.wv.T + self.bv)[:, 0]}
        if self.naux:
            out["aux"] = _sig(h @ self.wa.T + self.ba)
        return out

    ORDER = ["logits", "prob", "value", "aux"]


def ref_for(header):
    """F1Ref or RLRef, by the symbols the header defines"""
    s = open(header).read()
    if re.search(r"inline constexpr int F1_NC1 = ", s):
        return F1Ref(header)
    m = re.search(r"inline constexpr int (\w+)_NC1 = ", s)
    if not m:
        raise ValueError(f"{header}: no *_NC1 constant - not a generated weights header")
    return RLRef(header, prefix=m.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("weights")
    ap.add_argument("records", help="featurizer records: header bytes + 843 feature bytes (+ memory bytes)")
    ap.add_argument("--header-bytes", type=int, default=16, help="16 for featurize_f1 / f2 records, 6 for featurize_rl")
    ap.add_argument("--nmem", type=int, default=-1, help="memory bytes per record (default: the net's own inputs)")
    ap.add_argument("--n", type=int, default=3)
    args = ap.parse_args()
    net = ref_for(args.weights)
    nmem = net.NPRIV if args.nmem < 0 else args.nmem
    hb = args.header_bytes
    rec = np.fromfile(args.records, dtype=np.uint8).reshape(-1, hb + NFEAT + nmem)[:args.n]
    out = net.forward(rec[:, hb:hb + NFEAT], rec[:, 5], rec[:, hb + NFEAT:hb + NFEAT + net.NPRIV])
    np.set_printoptions(precision=6, suppress=True, linewidth=160)
    for i in range(len(rec)):
        print(f"record {i}: class {rec[i, 0]} round {2 * rec[i, 5]} length {rec[i, 2]}")
        for k, v in out.items():
            print(f"  {k}: {v[i]}")


if __name__ == "__main__":
    main()
