"""Terrain distances of a map and small helpers shared by the economy tools.

dist_matrix(board, key): all-pairs BFS distance over the terrain (kelp is a wall, a paired portal one step, the map wraps),
int16 with 30000 = unreachable, cached on disk under $ABYSS_CACHE/dist/<key>.npy where key = map_key(replay) hashes the
map's MAP and EDGE lines (so maps that differ only in their beds share one matrix). Needs numpy and scipy.
"""
import hashlib
import os
import re

from ..paths import cache_dir

INF = 30000
PHASES = ((0, 99), (100, 199), (200, 299), (300, 499))
PHASE_NAMES = ("r0-99", "r100-199", "r200-299", "r300-499")


def phase(r):
    return 0 if r < 100 else 1 if r < 200 else 2 if r < 300 else 3


def cheb(a, b, W, H):
    """Chebyshev distance on the torus"""
    dx = abs(a[0] - b[0])
    dy = abs(a[1] - b[1])
    return max(min(dx, W - dx), min(dy, H - dy))


def manhattan(a, b, W, H):
    """Manhattan distance on the torus"""
    dx = abs(a[0] - b[0])
    dy = abs(a[1] - b[1])
    return min(dx, W - dx) + min(dy, H - dy)


def map_key(rep_or_text):
    text = rep_or_text if isinstance(rep_or_text, str) else rep_or_text.map
    return hashlib.md5("\n".join(l for l in text.splitlines() if l.startswith(("MAP ", "EDGE"))).encode()).hexdigest()[:12]


def dist_matrix(bd, key=None, cache=True):
    """All-pairs terrain distance of a replay_state.Board: D[u, v] with u = y * W + x (see the module doc)."""
    import numpy as np
    path = None
    if cache and key:
        d = cache_dir("dist")
        path = os.path.join(d, key + ".npy")
        if os.path.exists(path):
            return np.load(path)
    from scipy.sparse import csr_matrix
    from scipy.sparse.csgraph import shortest_path
    W, H = bd.w, bd.h
    N = W * H
    rows, cols = [], []
    for y in range(H):
        for x in range(W):
            for dn in "NESW":
                t = bd.step((x, y), dn)
                if t is not None:
                    rows.append(y * W + x)
                    cols.append(t[1] * W + t[0])
    g = csr_matrix((np.ones(len(rows)), (rows, cols)), shape=(N, N))
    D = shortest_path(g, method="D", directed=True, unweighted=True)
    D[~np.isfinite(D)] = INF
    D = D.astype(np.int16)
    if path:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + f".{os.getpid()}.tmp.npy"
        np.save(tmp, D)
        os.replace(tmp, path)
    return D


def side_of(rep, name):
    """'A' / 'B': the side whose bot name starts with `name` (None when neither does)"""
    return "A" if rep.botA.startswith(name) else "B" if rep.botB.startswith(name) else None


def default_side(root):
    """an arena output folder named CAND_vs_OPP: CAND is the default candidate"""
    base = os.path.basename(os.path.normpath(root))
    if base == "replays":
        base = os.path.basename(os.path.dirname(os.path.normpath(root)))
    m = re.match(r"(.+?)_vs_", base)
    return m.group(1) if m else None
