"""Tiers of pearl beds by mean gap, and the walking distance to them.

A map file lists every bed as `TILE x y lo hi` (after a pearl is taken, the next one appears lo..hi rounds later; hi 0 = no
bed). Replays written by a local `unswbc run` carry the map text with the gaps; replays whose map text has lo = hi = 0 do
not, and then there are no tiers.

  tiers_of(beds) -> ({"fount": set, "fast": set, "best": set}, g*)   beds = {(x, y): (lo, hi)} (replay.state.Board.beds)
    fount: mean gap (lo + hi) / 2 <= 3 (fountains); fast: <= 20; best: the fastest tiles of the map - the smallest mean gap g*
    that covers at least max(8, 5%) of its bed tiles, ties included.
  bfs_field(board, src, labels=False) -> D[y][x] = walking steps from the tile to the nearest tile of src (through walls and
    portals as the engine does, BIG when unreachable); with labels also L[y][x] = that bed tile.
"""
import collections
import math

BIG = 999


def tiers_of(beds):
    g = {t: (lo + hi) / 2.0 for t, (lo, hi) in beds.items()}
    gs = sorted(g.values())
    need = max(8, math.ceil(0.05 * len(gs)))
    gbest = gs[min(need, len(gs)) - 1] if gs else None
    return {"fount": {t for t, v in g.items() if v <= 3}, "fast": {t for t, v in g.items() if v <= 20},
            "best": {t for t, v in g.items() if gbest is not None and v <= gbest}}, gbest


def bfs_field(board, src, labels=False):
    W, H = board.w, board.h
    D = [[BIG] * W for _ in range(H)]
    L = [[None] * W for _ in range(H)] if labels else None
    dq = collections.deque()
    for t in sorted(src, key=lambda p: (p[1], p[0])):
        D[t[1]][t[0]] = 0
        if labels:
            L[t[1]][t[0]] = t
        dq.append(t)
    while dq:
        t = dq.popleft()
        d = D[t[1]][t[0]]
        for k in "NESW":
            n = board.step(t, k)
            if n is not None and D[n[1]][n[0]] > d + 1:
                D[n[1]][n[0]] = d + 1
                if labels:
                    L[n[1]][n[0]] = L[t[1]][t[0]]
                dq.append(n)
    return (D, L) if labels else D
