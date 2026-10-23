"""Economy lead curve: who is ahead in the economy, when, and where.

At the geometric checkpoints r10 / 20 / 40 / 80 / 160 / 320 the economy vector E(t) = ours - theirs:

  total   total length at the round start of t
  units   living dragons at the round start of t
  eat     natural spawns eaten in the window [previous checkpoint, t), ours - theirs, as a share of the window's natural
          spawns (a natural spawn = a pearl appearing at a round start, before the first turn; an eat is counted in the
          round it happens)
  cov     the window's natural spawns with an own head within 5 terrain steps at the spawn, ours - theirs, as a share
  terr    Voronoi territory at the round start of t: the share of the tiles reachable by either side that are strictly
          nearer (terrain distance from every head, bodies ignored) to us, minus the same share for them

r400 / r500 use the tie-break quantities instead: queen length (0 when dead), then the longest dragon, then the total length
(the engine's order at r500); the lead is the sign of the lexicographic comparison.

Per game: the lead curve (sign and size of every checkpoint x dimension cell), the lead index (the share of the r10-r320
cells we lead, ties and empty windows left out), the first lagging checkpoint and dimension (r500 = never lagged), and the
local view: the map is cut into regions (rooms between articulation-point chokepoints - an articulation point whose removal
leaves two parts of >= max(8, 2% of the tiles) - each split into bed clusters: single linkage at terrain distance <= D* / 2,
and a cluster wider than D* cut around farthest-point centres D* apart, so maps with a bed on every tile still get regions;
a tile belongs to the nearest cluster of its room) and, per checkpoint, a region is contested when both sides have a head
within R = D* / 2 terrain steps of it; we lead it when we hold more of its tiles (Voronoi).
D* = clamp(sqrt(tiles) / 2.5, 8, 16). Map files give the beds (TILE x y lo hi with hi > 0); for a replay whose map text
carries no bed gaps, the beds are the tiles where a natural spawn was seen in the game.

A game that ends before a checkpoint carries its final standing forward (the wiped side has 0 units and 0 territory; eat /
cov are empty). Per-game numbers are cached under $ABYSS_CACHE/lead (keyed on the replay's path, size and mtime).
Needs numpy and scipy.

  python -m abyss_tools.kpi.lead_curve ARGS          (or abyss-lead-curve)

  arena folders   lead_curve DIR [DIR2 ..] [--side NAME]     (NAME = the CAND of a CAND_vs_OPP folder by default)
  paired          lead_curve CANDDIR .. --vs BASEDIR .. [--vs-side NAME]
                  games matched by (layout file, the candidate's side, repeat) - the arena's seed slot - and the per-game
                  deltas with bootstrap 95% intervals
  replays         lead_curve FILE.replay[.zst] .. --side NAME     (the per-game curves go to --json)
  options         --jobs N, --json OUT (one line per game), --md OUT (a one-page markdown summary), --boot N, --no-cache
"""
import argparse
import collections
import concurrent.futures as cf
import hashlib
import json
import math
import os
import re
import sys
import time

import numpy as np

from ..paths import cache_dir
from ..replay import terrain
from ..replay.io import initial_teams, load, replay_files_recursive
from ..replay.state import Board
from ..replay.terrain import default_side

CK = (10, 20, 40, 80, 160, 320)                  # the economy checkpoints (lead index cells)
CK_END = (400, 500)                              # the tie-break checkpoints
DIMS = ("total", "units", "eat", "cov", "terr")
SNAP = set(CK) | {100, 150, 200, 300, 400}       # round-start snapshots
ECON_SNAP = (100, 150, 200, 300)
WIN = tuple(zip((0,) + CK[:-1], CK)) + ((320, 400), (400, 500))
INF = 30000
VERSION = "lead3"
OPP = {"A": "B", "B": "A"}


# --------------------------------------------------------------------------------------------------------------- regions

def d_star(n):
    return min(16.0, max(8.0, math.sqrt(n) / 2.5))


def adjacency(bd):
    """undirected terrain graph (the same steps as terrain.dist_matrix)"""
    W, H = bd.w, bd.h
    adj = [set() for _ in range(W * H)]
    for y in range(H):
        for x in range(W):
            u = y * W + x
            for dn in "NESW":
                t = bd.step((x, y), dn)
                if t is not None:
                    v = t[1] * W + t[0]
                    if v != u:
                        adj[u].add(v)
                        adj[v].add(u)
    return [sorted(a) for a in adj]


def chokepoints(adj, nodes, m):
    """articulation points whose removal leaves at least two parts of >= m tiles (iterative Tarjan with subtree sizes)"""
    disc, low, size = {}, {}, {}
    parts = collections.defaultdict(list)          # articulation point -> sizes of the child subtrees it separates
    out = set()
    t = 0
    for root in nodes:
        if root in disc:
            continue
        comp = []
        disc[root] = low[root] = t
        t += 1
        size[root] = 1
        stack = [(root, -1, iter(adj[root]))]
        comp.append(root)
        while stack:
            u, par, it = stack[-1]
            pushed = False
            for v in it:
                if v not in disc:
                    disc[v] = low[v] = t
                    t += 1
                    size[v] = 1
                    comp.append(v)
                    stack.append((v, u, iter(adj[v])))
                    pushed = True
                    break
                if v != par:
                    low[u] = min(low[u], disc[v])
            if pushed:
                continue
            stack.pop()
            if par >= 0:
                low[par] = min(low[par], low[u])
                size[par] += size[u]
                if low[u] >= disc[par]:
                    parts[par].append(size[u])
        n = len(comp)
        for p in comp:
            if p not in parts:
                continue
            sub = parts[p]
            if p == root:
                sizes = sub if len(sub) >= 2 else []
            else:
                sizes = sub + [n - 1 - sum(sub)]
            if sum(1 for s in sizes if s >= m) >= 2:
                out.add(p)
    return out


def split_wide(D, g, ds):
    """a bed cluster wider than D* (beds on every tile chain into one cluster) is cut around farthest-point centres D* apart"""
    g = np.asarray(g)
    centres = [int(g[0])]
    near = np.minimum(D[g[0], g], D[g, g[0]]).astype(np.int32)
    while True:
        i = int(near.argmax())
        if near[i] <= ds:
            break
        centres.append(int(g[i]))
        near = np.minimum(near, np.minimum(D[g[i], g], D[g, g[i]]))
    if len(centres) == 1:
        return [[int(x) for x in g]]
    own = np.stack([np.minimum(D[c, g], D[g, c]) for c in centres]).argmin(0)
    return [[int(x) for x in g[own == k]] for k in range(len(centres)) if (own == k).any()]


def regions(bd, D, beds):
    """tile -> region id (-1 unreachable): rooms between chokepoints (>= m tiles), each cut into bed clusters"""
    W, H = bd.w, bd.h
    N = W * H
    adj = adjacency(bd)
    reach = [u for u in range(N) if adj[u]]
    n = len(reach)
    ds = d_star(n)
    c = max(3, int(round(ds / 2)))
    m = max(8, int(0.02 * n))
    cut = chokepoints(adj, reach, m)
    room = np.full(N, -1, dtype=np.int32)
    rid = 0
    for s0 in reach:
        if s0 in cut or room[s0] >= 0:
            continue
        comp, q = [s0], [s0]
        room[s0] = rid
        while q:
            u = q.pop()
            for v in adj[u]:
                if v not in cut and room[v] < 0:
                    room[v] = rid
                    comp.append(v)
                    q.append(v)
        if len(comp) < m:                       # a small pocket joins its neighbour room below
            room[comp] = -2
        else:
            rid += 1
    # chokepoints and pockets: multi-source BFS from the rooms over the graph
    frontier = [u for u in reach if room[u] >= 0]
    if not frontier:
        room[reach] = 0
        rid = 1
        frontier = []
    while frontier:
        nxt = []
        for u in frontier:
            for v in adj[u]:
                if room[v] < 0:
                    room[v] = room[u]
                    nxt.append(v)
        frontier = nxt
    reg = np.full(N, -1, dtype=np.int32)
    bed_idx = sorted({b[1] * W + b[0] for b in beds})
    nreg = 0
    info = []
    for r in range(rid):
        tiles = np.flatnonzero(room == r)
        rb = [b for b in bed_idx if room[b] == r]
        if not rb:
            reg[tiles] = nreg
            info.append({"tiles": int(len(tiles)), "beds": 0})
            nreg += 1
            continue
        # single linkage over terrain distance <= c (either direction), then clusters wider than D* cut
        from scipy.sparse import csr_matrix
        from scipy.sparse.csgraph import connected_components
        sub = D[np.ix_(rb, rb)]
        _, lab = connected_components(csr_matrix(np.minimum(sub, sub.T) <= c), directed=False)
        groups = collections.defaultdict(list)
        for i, b in enumerate(rb):
            groups[int(lab[i])].append(b)
        gl = []
        for k in sorted(groups):
            gl += split_wide(D, groups[k], ds)
        dist = np.stack([D[g][:, tiles].min(0) for g in gl])       # cluster x tile
        near = dist.argmin(0)
        for k, g in enumerate(gl):
            reg[tiles[near == k]] = nreg + k
            info.append({"tiles": int((near == k).sum()), "beds": len(g)})
        nreg += len(gl)
    return reg, nreg, {"rooms": rid, "chokepoints": len(cut), "c": c, "R": c, "m": m, "Dstar": round(ds, 2), "regions": info}


def local_stats(reg, nreg, R, dA, dB):
    """(contested, led by A, led by B) at one checkpoint"""
    ok = reg >= 0
    r = reg[ok]
    a, b = dA[ok], dB[ok]
    ownA = np.bincount(r, weights=(a < b), minlength=nreg)
    ownB = np.bincount(r, weights=(b < a), minlength=nreg)
    nearA = np.full(nreg, INF)
    nearB = np.full(nreg, INF)
    np.minimum.at(nearA, r, a)
    np.minimum.at(nearB, r, b)
    cont = (nearA <= R) & (nearB <= R)
    return int(cont.sum()), int((cont & (ownA > ownB)).sum()), int((cont & (ownB > ownA)).sum())


# ------------------------------------------------------------------------------------------------------------ one game

def walk(path):
    """Both sides' per-round economy of one replay (side-neutral)."""
    rep = load(path)
    bd = Board(rep.map)
    W, H = bd.w, bd.h
    D = terrain.dist_matrix(bd, terrain.map_key(rep))
    team = initial_teams(rep.map)
    body = {i: list(b) for i, (t, b) in enumerate(bd.dragons)}
    alive = set(body)
    queen = {team[i]: i for i in (0, 1) if i in team}
    spawn = [0] * 500
    cov = {"A": [0] * 500, "B": [0] * 500}
    eat = {"A": [0] * 500, "B": [0] * 500}
    snaps, terr, vor = {}, {}, {}
    seen_beds = set()
    origin = {}
    rnd, actor, tick = 0, -1, False

    def heads(s):
        return [body[d][0][1] * W + body[d][0][0] for d in alive if team.get(d) == s]

    def standing(s):
        L = [len(body[d]) for d in alive if team.get(d) == s]
        q = queen.get(s)
        return [len(L), sum(L), max(L, default=0), len(body[q]) if q in alive else 0]

    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
            tick, actor = True, -1
            if rnd in SNAP:
                snaps[rnd] = {s: standing(s) for s in "AB"}
            if rnd % 5 == 0:
                hA, hB = heads("A"), heads("B")
                if hA and hB:
                    dA = D[hA].min(0)
                    dB = D[hB].min(0)
                    n = max(1, int(((dA < INF) | (dB < INF)).sum()))
                    terr[rnd] = (float((dA < dB).sum()) / n, float((dB < dA).sum()) / n)
                    if rnd in CK or rnd == 400:
                        vor[rnd] = (dA, dB)
        elif k == "turnStart":
            actor = e.turnStart.id
            tick = False
        elif k == "dragonUpdate":
            u = e.dragonUpdate
            d = u.id
            hd, tl = (u.head.x, u.head.y), (u.tail.x, u.tail.y)
            b = body.setdefault(d, [hd])
            if b[0] != hd:
                b.insert(0, hd)
            while len(b) > 1 and b[-1] != tl:
                b.pop()
        elif k == "dragonSplit":
            sp = e.dragonSplit
            t = "A" if str(sp.team) == "a" else "B"
            team[sp.childId] = t
            body[sp.parentId] = [(q.x, q.y) for q in sp.parentBody]
            body[sp.childId] = [(q.x, q.y) for q in sp.childBody]
            alive.add(sp.childId)
        elif k == "tileChange":
            tc = e.tileChange
            t = (tc.tile.x, tc.tile.y)
            if tc.hasPearl:
                if tick:
                    ti = t[1] * W + t[0]
                    hA, hB = heads("A"), heads("B")
                    dA = int(D[hA, ti].min()) if hA else INF
                    dB = int(D[hB, ti].min()) if hB else INF
                    if rnd < 500:
                        spawn[rnd] += 1
                        cov["A"][rnd] += dA <= 5
                        cov["B"][rnd] += dB <= 5
                    origin[t] = ("bed",)
                    seen_beds.add(t)
                else:
                    origin[t] = ("drop",)
            else:
                s = team.get(actor)
                if s and not tick:
                    o = origin.pop(t, ("bed",))
                    if o[0] == "bed" and rnd < 500:
                        eat[s][rnd] += 1
        elif k == "dragonDeath":
            alive.discard(e.dragonDeath.id)
    rounds = rnd + 1
    res = rep.result
    winner = None if res.which() == "noWinner" else ("A" if res.winner == "a" else "B")
    final = {s: standing(s) for s in "AB"}
    got = {"A": [res.teamA.dragonCount, res.teamA.totalLength, res.teamA.longestDragon, res.teamA.queenLength],
           "B": [res.teamB.dragonCount, res.teamB.totalLength, res.teamB.longestDragon, res.teamB.queenLength]}
    fmt2 = rep.formatVersion >= 2
    check = all(final[s][:3] == got[s][:3] and (not fmt2 or final[s][3] == got[s][3]) for s in "AB")
    beds = list(bd.beds) or sorted(seen_beds)
    reg, nreg, rinfo = regions(bd, D, beds)
    local = {}
    for t, (dA, dB) in vor.items():
        local[t] = local_stats(reg, nreg, rinfo["R"], dA, dB)
    mp = next((l.split(None, 1)[1] for l in rep.map.splitlines() if l.startswith("MAP_NAME")), "?")
    return {"file": os.path.basename(path), "map": mp, "W": W, "H": H, "rounds": rounds, "winner": winner,
            "botA": rep.botA, "botB": rep.botB, "check": check, "bed_source": "map" if bd.beds else "seen",
            "snaps": {str(r): v for r, v in snaps.items()}, "terr": {str(r): v for r, v in terr.items()},
            "spawn": spawn, "cov": cov, "eat": eat, "final": final, "local": {str(t): v for t, v in local.items()},
            "regions": {k: v for k, v in rinfo.items() if k != "regions"} | {"n": nreg}}


def walk_cached(args):
    path, cache = args
    key = hashlib.md5(f"{os.path.abspath(path)}|{os.path.getsize(path)}|{int(os.path.getmtime(path))}|{VERSION}".encode()).hexdigest()
    cpath = os.path.join(cache_dir("lead"), key + ".json")
    if cache and os.path.exists(cpath):
        try:
            return path, json.load(open(cpath))
        except Exception:
            pass
    try:
        g = walk(path)
    except Exception:
        import traceback
        return path, {"error": traceback.format_exc()[-800:], "file": os.path.basename(path)}
    if cache:
        os.makedirs(os.path.dirname(cpath), exist_ok=True)
        tmp = cpath + f".{os.getpid()}.tmp"
        json.dump(g, open(tmp, "w"))
        os.replace(tmp, cpath)
    return path, g


# -------------------------------------------------------------------------------------------------------- lead of a side

def state_at(g, t):
    """(standing A, standing B, terr A, terr B, post_end) at the round start of t (the final standing past the game's end)"""
    st = g["snaps"].get(str(t))
    if st is not None and t < g["rounds"]:
        tr = g["terr"].get(str(t))
        if tr is None:          # a side without heads at a round start: the game is over at this round
            tr = (1.0, 0.0) if st["B"][0] == 0 else (0.0, 1.0) if st["A"][0] == 0 else (0.0, 0.0)
        return st["A"], st["B"], tr[0], tr[1], False
    fa, fb = g["final"]["A"], g["final"]["B"]
    tr = (1.0, 0.0) if fb[0] == 0 and fa[0] > 0 else (0.0, 1.0) if fa[0] == 0 and fb[0] > 0 else (0.0, 0.0)
    return fa, fb, tr[0], tr[1], True


def lead(g, s):
    """the lead curve of side s: cells {t: {dim: diff or None}}, index, first lag, local shares, end signs"""
    o = OPP[s]
    cells, post = {}, {}
    for i, t in enumerate(CK):
        sa, sb, ta, tb, pe = state_at(g, t)
        us, th = (sa, sb) if s == "A" else (sb, sa)
        tu, tt = (ta, tb) if s == "A" else (tb, ta)
        lo, hi = WIN[i]
        hi = min(hi, g["rounds"])
        nsp = sum(g["spawn"][lo:hi]) if hi > lo else 0
        row = {"total": us[1] - th[1], "units": us[0] - th[0], "terr": tu - tt}
        if nsp:
            row["eat"] = (sum(g["eat"][s][lo:hi]) - sum(g["eat"][o][lo:hi])) / nsp
            row["cov"] = (sum(g["cov"][s][lo:hi]) - sum(g["cov"][o][lo:hi])) / nsp
        else:
            row["eat"] = row["cov"] = None
        cells[t] = row
        post[t] = pe
    flat = [(t, d, cells[t][d]) for t in CK for d in DIMS if cells[t][d] is not None]
    led = sum(1 for t, d, v in flat if v > 0)
    idx = led / len(flat) if flat else float("nan")
    lag = next(((t, d) for t, d, v in flat if v < 0), None)
    major = 500                                  # the first checkpoint behind in most of its non-empty dimensions
    for t in CK:
        v = [cells[t][d] for d in DIMS if cells[t][d] is not None]
        if v and 2 * sum(1 for a in v if a < 0) > len(v):
            major = t
            break
    loc = {}
    for t in CK:
        x = g["local"].get(str(t))
        if x and x[0]:
            loc[t] = (x[1] if s == "A" else x[2]) / x[0]
    end = {}
    for t in CK_END:
        if t == 500:
            fa, fb = g["final"]["A"], g["final"]["B"]
        else:
            fa, fb, _, _, _ = state_at(g, t)
        us, th = (fa, fb) if s == "A" else (fb, fa)
        ku, kt = (us[3], us[2], us[1]), (th[3], th[2], th[1])
        end[t] = {"us": ku, "them": kt, "sign": (ku > kt) - (ku < kt)}
    snap = {}
    for t in ECON_SNAP:                          # the snapshot block: (our units, total, their units, total)
        st = g["snaps"].get(str(t))
        if st is not None and t < g["rounds"]:
            snap[t] = (st[s][0], st[s][1], st[o][0], st[o][1])
    return {"cells": cells, "post": post, "index": idx, "cells_n": len(flat), "led": led, "snap": snap,
            "lag": lag, "lag_round": lag[0] if lag else 500, "major_round": major, "local": loc, "end": end,
            "won": g["winner"] == s, "rounds": g["rounds"]}


# ------------------------------------------------------------------------------------------------------------ statistics

def boot_ci(x, n=2000, seed=1):
    x = np.asarray([v for v in x if v == v], dtype=float)
    if len(x) == 0:
        return float("nan"), float("nan"), float("nan")
    rng = np.random.default_rng(seed)
    means = x[rng.integers(0, len(x), size=(n, len(x)))].mean(1)
    return float(x.mean()), float(np.percentile(means, 2.5)), float(np.percentile(means, 97.5))


def boot_median_ci(x, n=2000, seed=1):
    x = np.asarray(x, dtype=float)
    if len(x) == 0:
        return float("nan"), float("nan"), float("nan")
    rng = np.random.default_rng(seed)
    meds = np.median(x[rng.integers(0, len(x), size=(n, len(x)))], 1)
    return float(np.median(x)), float(np.percentile(meds, 2.5)), float(np.percentile(meds, 97.5))


def fmt_ci(m, lo, hi, n=2):
    return "-" if m != m else f"{m:+.{n}f} [{lo:+.{n}f}, {hi:+.{n}f}]"


def unit(d, v):
    if v is None or v != v:
        return "-"
    return f"{v:+.1f}" if d in ("total", "units") else f"{v:+.2f}"


# --------------------------------------------------------------------------------------------------------------- reports

def summary(rows, title, P, boot):
    """rows: [(lead dict, opponent bot name)]; a per-opponent table is printed when the games have several opponents"""
    L = [r for r, k in rows]
    n = len(L)
    if not n:
        P(f"\n### {title}: no games")
        return {}
    m, lo, hi = boot_ci([x["index"] for x in L], boot)
    md, mlo, mhi = boot_median_ci([x["lag_round"] for x in L], boot)
    never = sum(1 for x in L if x["lag"] is None)
    won = sum(1 for x in L if x["won"])
    P(f"\n### {title}: {n} games, won {won} ({won / n:.0%})")
    P(f"- lead index r10-r320: **{m:.3f}** [{lo:.3f}, {hi:.3f}] (cells per game {np.mean([x['cells_n'] for x in L]):.1f})")
    P(f"- first lagging round, median: **{md:.0f}** [{mlo:.0f}, {mhi:.0f}]; never lagged {never} ({never / n:.0%})")
    dims = collections.Counter(x["lag"][1] for x in L if x["lag"])
    rnds = collections.Counter(x["lag"][0] for x in L if x["lag"])
    mj, mjlo, mjhi = boot_median_ci([x["major_round"] for x in L], boot)
    P(f"- first round behind in most of its non-empty cells (majority-behind), median: **{mj:.0f}** [{mjlo:.0f}, {mjhi:.0f}]; never "
      f"{sum(1 for x in L if x['major_round'] == 500)}")
    P("- first lag by dimension: " + ", ".join(f"{d} {dims[d]}" for d in DIMS) + "; by checkpoint: "
      + ", ".join(f"r{t} {rnds[t]}" for t in CK))
    P("")
    P("| checkpoint | " + " | ".join(f"{d}: lead / mean diff" for d in DIMS) + " | local: contested led (games) |")
    P("|---|" + "---|" * (len(DIMS) + 1))
    for t in CK:
        cells = []
        for d in DIMS:
            v = [x["cells"][t][d] for x in L if x["cells"][t][d] is not None]
            cells.append(f"{sum(1 for a in v if a > 0) / len(v):.2f} / {unit(d, float(np.mean(v)))}" if v else "-")
        lv = [x["local"][t] for x in L if t in x["local"]]
        cells.append(f"{np.mean(lv):.2f} ({len(lv)})" if lv else "-")
        P(f"| r{t} | " + " | ".join(cells) + " |")
    P("- snapshots, median over the games still running: units / total length (us vs opp), P(our total leads): "
      + "; ".join(f"r{t} n {len(v)} {np.median([a[0] for a in v]):.0f} / {np.median([a[1] for a in v]):.0f} vs "
                  f"{np.median([a[2] for a in v]):.0f} / {np.median([a[3] for a in v]):.0f} lead {np.mean([a[1] > a[3] for a in v]):.2f}"
                  for t in ECON_SNAP for v in [[x["snap"][t] for x in L if t in x["snap"]]] if v))
    for t in CK_END:
        sg = [x["end"][t]["sign"] for x in L]
        P(f"- r{t} tie-break (queen, longest, total): ahead {sum(1 for a in sg if a > 0) / n:.2f}, level {sum(1 for a in sg if a == 0) / n:.2f}, "
          f"behind {sum(1 for a in sg if a < 0) / n:.2f}")
    kinds = collections.defaultdict(list)
    for r, k in rows:
        kinds[k].append(r)
    if len(kinds) > 1:
        P("")
        P("| opponent | games | won | lead index | first lag median | majority-behind median | never lagged |")
        P("|---|---|---|---|---|---|---|")
        for k in sorted(kinds, key=lambda k: (-len(kinds[k]), k)):
            v = kinds[k]
            a, b, c = boot_ci([x["index"] for x in v], boot)
            P(f"| {k} | {len(v)} | {sum(x['won'] for x in v)} | {a:.3f} [{b:.3f}, {c:.3f}] | {np.median([x['lag_round'] for x in v]):.0f} | "
              f"{np.median([x['major_round'] for x in v]):.0f} | {sum(1 for x in v if x['lag'] is None)} |")
    return {"n": n, "index": (m, lo, hi), "lag_median": (md, mlo, mhi), "major_median": (mj, mjlo, mjhi), "never": never, "won": won}


def paired(cand, base, P, boot):
    """cand / base: {key: lead dict}; per-game deltas cand - base"""
    keys = sorted(set(cand) & set(base))
    P(f"\n### paired: {len(keys)} games matched (candidate {len(cand)}, base {len(base)})")
    if not keys:
        return
    di = [cand[k]["index"] - base[k]["index"] for k in keys]
    dl = [cand[k]["lag_round"] - base[k]["lag_round"] for k in keys]
    P(f"- lead index delta: **{fmt_ci(*boot_ci(di, boot), n=3)}** (base {np.nanmean([base[k]['index'] for k in keys]):.3f}, "
      f"candidate {np.nanmean([cand[k]['index'] for k in keys]):.3f})")
    P(f"- first lagging round delta (mean): **{fmt_ci(*boot_ci(dl, boot), n=1)}**; medians base {np.median([base[k]['lag_round'] for k in keys]):.0f}, "
      f"candidate {np.median([cand[k]['lag_round'] for k in keys]):.0f}")
    dm = [cand[k]["major_round"] - base[k]["major_round"] for k in keys]
    P(f"- first majority-behind round delta (mean): **{fmt_ci(*boot_ci(dm, boot), n=1)}**")
    P(f"- wins: base {sum(base[k]['won'] for k in keys)}, candidate {sum(cand[k]['won'] for k in keys)}")
    P("")
    P("| checkpoint | " + " | ".join(f"{d} delta [95%]" for d in DIMS) + " |")
    P("|---|" + "---|" * len(DIMS))
    for t in CK:
        cells = []
        for d in DIMS:
            v = [cand[k]["cells"][t][d] - base[k]["cells"][t][d] for k in keys
                 if cand[k]["cells"][t][d] is not None and base[k]["cells"][t][d] is not None]
            cells.append(fmt_ci(*boot_ci(v, boot), n=1 if d in ("total", "units") else 3) if v else "-")
        P(f"| r{t} | " + " | ".join(cells) + " |")


def pair_key(path, s):
    """the arena's seed slot: (layout file, the candidate's side, repeat)"""
    head, _, rest = os.path.basename(path).partition("__")
    rep = re.search(r"_r(\d+)\.replay", rest)
    return (head, s, rep.group(1) if rep else "0")


def opp_name(g, s):
    """the opponent's bot name without the arena's side suffix (_L / _R)"""
    return re.sub(r"_[LR]$", "", g["botB"] if s == "A" else g["botA"])


# ------------------------------------------------------------------------------------------------------------------ main

def run_walks(files, jobs, cache):
    # the distance matrices once per map before the pool (a cached matrix is read, not rebuilt, by every worker)
    seen = set()
    for p in files:
        rep = load(p)
        k = terrain.map_key(rep)
        if k not in seen:
            seen.add(k)
            terrain.dist_matrix(Board(rep.map), k)
    out = {}
    with cf.ProcessPoolExecutor(jobs) as ex:
        for p, g in ex.map(walk_cached, [(p, cache) for p in files], chunksize=1):
            if "error" in g:
                print(f"failed {g['file']}: {g['error']}", file=sys.stderr)
                continue
            out[p] = g
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="arena output folders, replay folders or replay files (the candidate)")
    ap.add_argument("--side", help="our bot's name prefix (default: CAND of a CAND_vs_OPP folder)")
    ap.add_argument("--vs", nargs="+", help="baseline folders on the same seeds: paired deltas")
    ap.add_argument("--vs-side", help="the baseline's name prefix (default: CAND of its folder)")
    ap.add_argument("--limit", type=int, default=0, help="first N games per group (timing runs)")
    ap.add_argument("--jobs", "-j", type=int, default=8)
    ap.add_argument("--boot", type=int, default=2000)
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--json", help="one JSON line per game (the lead curve of our side)")
    ap.add_argument("--md", help="write the markdown summary here as well")
    args = ap.parse_args(argv)
    t0 = time.time()
    lines = []

    def P(s=""):
        print(s)
        lines.append(s)

    def files_of(roots):
        return sorted({p: None for r in roots for p in (replay_files_recursive(r) if os.path.isdir(r) else [r])})

    groups = collections.OrderedDict()          # group -> [(path, side or None, bot name prefix or None, kind)]
    name = args.side or default_side(args.paths[0])
    if not name:
        sys.exit("--side NAME is needed (no CAND_vs_OPP folder name)")
    groups["candidate" if args.vs else name] = [(p, None, name, None) for p in files_of(args.paths)]
    if args.vs:
        vname = args.vs_side or default_side(args.vs[0])
        if not vname:
            sys.exit("--vs-side NAME is needed")
        groups["base"] = [(p, None, vname, None) for p in files_of(args.vs)]
    if args.limit:
        for k in groups:
            groups[k] = groups[k][:args.limit]
    allfiles = sorted({x[0] for v in groups.values() for x in v})
    walks = run_walks(allfiles, args.jobs, not args.no_cache)
    t1 = time.time()

    results = collections.OrderedDict()
    for grp, items in groups.items():
        rows = []
        for p, side, nm, kind in items:
            g = walks.get(p)
            if g is None:
                continue
            side = "A" if g["botA"].startswith(nm) else "B" if g["botB"].startswith(nm) else None
            if side is None:
                continue
            rows.append((p, side, opp_name(g, side), lead(g, side)))
        results[grp] = rows
    games_n = sum(len(v) for v in results.values())
    P(f"## Economy lead curve ({games_n} games, {len(walks)} replays walked in {t1 - t0:.1f} s with {args.jobs} jobs)")
    P("")
    P("Cells: ours - theirs at the round start of r10 / 20 / 40 / 80 / 160 / 320 (total, units, terr = Voronoi share) and over the window "
      "before it (eat / cov = natural spawns eaten / with an own head within 5, as a share of the window's spawns). Lead index = share of "
      "the non-empty cells we lead; first lagging round = the first checkpoint with a cell behind (500 = never); majority-behind = the first "
      "checkpoint behind in most of its non-empty cells.")
    for grp, rows in results.items():
        summary([(L, k or "-") for p, s, k, L in rows], grp, P, args.boot)
    if args.vs:
        cand = {pair_key(p, s): L for p, s, k, L in results.get("candidate", [])}
        base = {pair_key(p, s): L for p, s, k, L in results.get("base", [])}
        paired(cand, base, P, args.boot)
    if walks:
        reg = [walks[p]["regions"] for p in walks]
        P(f"\nregions per game: median {np.median([r['n'] for r in reg]):.0f} (rooms {np.median([r['rooms'] for r in reg]):.0f}, "
          f"chokepoints {np.median([r['chokepoints'] for r in reg]):.0f}); beds from the map in "
          f"{sum(1 for p in walks if walks[p]['bed_source'] == 'map')}, from the seen spawns in "
          f"{sum(1 for p in walks if walks[p]['bed_source'] == 'seen')}; result check failed in "
          f"{sum(1 for p in walks if not walks[p]['check'])}")
    if args.json:
        with open(args.json, "w") as fo:
            for grp, rows in results.items():
                for p, s, k, L in rows:
                    fo.write(json.dumps({"group": grp, "file": os.path.basename(p), "side": s, "kind": k, "map": walks[p]["map"],
                                         "index": L["index"], "lag": L["lag"], "lag_round": L["lag_round"],
                                         "major_round": L["major_round"], "won": L["won"],
                                         "cells": {str(t): v for t, v in L["cells"].items()},
                                         "local": {str(t): v for t, v in L["local"].items()},
                                         "end": {str(t): v for t, v in L["end"].items()}}) + "\n")
    P(f"\n(total {time.time() - t0:.1f} s)")
    if args.md:
        open(args.md, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
