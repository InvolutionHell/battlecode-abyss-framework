"""The queens' early deaths in folders of replays: why a queen dies before --until (default r150).

For every queen death before --until, both sides, the bots by name:
- the round, the engine's reason, and whether it came on her own move (queen_box.queen_self);
- her length, and how many dragons of her team were alive at that round's start;
- the enemy heads within Chebyshev 3 of her head (length, torus Manhattan distance);
- her best 1-step landing: free of every body, and its margin against every enemy head: the head's path distance to it (walls
  and portals as the map has them, bodies ignored - a lower bound of the enemy's real path) minus the enemy's reach (rmax:
  ceil(n / 4) free steps + n - 2 paid ones). A margin >= 1 is outside every enemy's reach unless pearls on its way pay its
  steps. Margins are capped at CAP.

Arms that share paired seeds (abyss_tools.run.arena) play the same game until their switch acts: the totals count each death
once.

  python -m abyss_tools.kpi.queen_early_deaths DIR [DIR ...] [--until 150] [-v] [-j 8]
  abyss-queen-early-deaths ...
"""
import argparse
import collections
import concurrent.futures as cf
import os

from ..replay.io import base_name, initial_teams, load, map_stem, replay_files
from ..replay.state import Board
from .queen_box import queen_self

DIRS = "NESW"


def rmax(n):
    """the reach of an n-long enemy head in one move: ceil(n / 4) free steps + n - 2 paid ones (the bot's qsk.hpp Qsk::rmax)"""
    return 0 if n < 2 else (n + 3) // 4 + n - 2


def tdist(a, b, W, H):
    dx, dy = abs(a[0] - b[0]), abs(a[1] - b[1])
    return min(dx, W - dx) + min(dy, H - dy)


CAP = 4


def path_dist(bd, src, radius):
    """steps from src to the tiles within radius: walls and portals as the map has them, bodies ignored"""
    dist, frontier = {src: 0}, [src]
    for k in range(1, radius + 1):
        nxt = []
        for t in frontier:
            for dn in DIRS:
                n = bd.step(t, dn)
                if n is not None and n not in dist:
                    dist[n] = k
                    nxt.append(n)
        frontier = nxt
    return dist


def best_landing(bd, head, occ, enemies):
    """the largest margin over her free 1-step landings: min over enemy heads (cell, length) of path distance - rmax, capped at
    CAP; None if boxed"""
    lands = [n for dn in DIRS if (n := bd.step(head, dn)) is not None and n not in occ]
    if not lands:
        return None
    margin = dict.fromkeys(lands, CAP)
    for h, ln in enemies:
        r = rmax(ln)
        dist = path_dist(bd, h, r + CAP - 1)
        for n in lands:
            margin[n] = min(margin[n], dist.get(n, r + CAP) - r)
    return max(margin.values())


def job(args):
    path, until = args
    r = load(path)
    bd = Board(r.map)
    W, H = bd.w, bd.h
    team = initial_teams(r.map)
    body = {i: list(b) for i, (t, b) in enumerate(bd.dragons)}
    alive = set(body)
    q = {s: next(i for i in (0, 1) if team.get(i) == s) for s in "AB"}
    snap, out, rnd = {}, [], 0
    for e in r.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
            if rnd >= until:
                break
            snap = {}
            occ = set(t for d in alive for t in body[d])
            for s in "AB":
                if q[s] not in alive:
                    continue
                qh = body[q[s]][0]
                enemies = [(body[d][0], len(body[d])) for d in alive if team.get(d) != s]
                near = sorted(((ln, tdist(h, qh, W, H)) for h, ln in enemies
                               if max(min(abs(h[0] - qh[0]), W - abs(h[0] - qh[0])), min(abs(h[1] - qh[1]), H - abs(h[1] - qh[1]))) <= 3),
                              key=lambda x: x[1])
                snap[s] = dict(L=len(body[q[s]]), ours=sum(1 for d in alive if team.get(d) == s), near=near,
                               landing=(qh, frozenset(occ), enemies))  # computed at her death only
        elif k == "dragonUpdate":
            u = e.dragonUpdate
            hd, tl = (u.head.x, u.head.y), (u.tail.x, u.tail.y)
            b = body.setdefault(u.id, [hd])
            alive.add(u.id)
            if b[0] != hd:
                b.insert(0, hd)
            while len(b) > 1 and b[-1] != tl:
                b.pop()
        elif k == "dragonSplit":
            sp = e.dragonSplit
            team[sp.childId] = "A" if str(sp.team) == "a" else "B"
            body[sp.parentId] = [(p.x, p.y) for p in sp.parentBody]
            body[sp.childId] = [(p.x, p.y) for p in sp.childBody]
            alive.add(sp.childId)
        elif k == "dragonDeath":
            d = e.dragonDeath.id
            alive.discard(d)
            for s in "AB":
                if d == q[s] and s in snap:
                    o = dict(side=s, round=rnd, reason=str(e.dragonDeath.reason), **snap[s])
                    o["landing"] = best_landing(bd, *o["landing"])
                    out.append(o)
    names = {"A": base_name(r.botA), "B": base_name(r.botB)}
    for o in out:
        qs = queen_self(path, o["side"])
        o["own"] = bool(qs) and qs.get("round") == o["round"]
        o["bot"] = names[o["side"]]
    return os.path.basename(path), out, names


def distinct(deaths):
    """each death once: arms sharing a paired seed repeat the same early game"""
    seen, out = set(), []
    for (mp, _), ds in sorted(deaths.items()):
        for o in ds:
            key = (mp, o["round"], o["reason"], o["L"], o["ours"], tuple(o["near"]), o["landing"])
            if key not in seen:
                seen.add(key)
                out.append(dict(o, map=mp))
    return out


def struck(o):
    return o["reason"] == "hitHeadToHead" and not o["own"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+", help="folders of replays (the arena's replays/)")
    ap.add_argument("--until", type=int, default=150)
    ap.add_argument("-v", action="store_true", help="one line per death")
    ap.add_argument("-j", type=int, default=8)
    a = ap.parse_args(argv)
    files = replay_files(a.dirs)
    games = collections.Counter()
    deaths = collections.defaultdict(list)
    with cf.ProcessPoolExecutor(a.j) as ex:
        for f, out, names in ex.map(job, [(f, a.until) for f in files]):
            mp = map_stem(f)
            for s in "AB":
                games[(mp, names[s])] += 1
            for o in out:
                deaths[(mp, o["bot"])].append(o)
                if a.v:
                    print(f"{f[:46]:46s} {o['bot']:13s} r{o['round']:3d} {o['reason']:14s} own move {o['own']!s:5s} L {o['L']} "
                          f"ours {o['ours']:2d} best landing {o['landing']} enemy heads within 3 (len, dist) {o['near'][:4]}")
    print(f"| map | bot | queen-games | deaths before r{a.until} | struck head-on | ... with <= 2 of ours | ... with a landing out of reach |")
    print("|---|---|---|---|---|---|---|")
    for (mp, b), n in sorted(games.items()):
        ds = deaths[(mp, b)]
        st = [o for o in ds if struck(o)]
        print(f"| {mp} | {b} | {n} | {len(ds)} | {len(st)} | {sum(o['ours'] <= 2 for o in st)} | "
              f"{sum((o['landing'] or 0) >= 1 for o in st)} |")
    tot = distinct(deaths)
    st = [o for o in tot if struck(o)]
    print(f"\ndistinct: {sum(games.values())} queen-games, {len(tot)} queen deaths before r{a.until}; struck head-on on an enemy turn "
          f"{len(st)}; of them with <= 2 of ours {sum(o['ours'] <= 2 for o in st)}, with a landing out of reach "
          f"{sum((o['landing'] or 0) >= 1 for o in st)}; their best landing margins "
          f"{sorted(collections.Counter(o['landing'] for o in st).items(), key=lambda x: (x[0] is None, x[0] or 0))}")
    for o in st:
        if o["ours"] <= 2 or (o["landing"] or 0) >= 1:
            print(f"  {o['map']} {o['bot']} r{o['round']} L {o['L']} ours {o['ours']} best landing {o['landing']} near {o['near'][:4]}")


if __name__ == "__main__":
    main()
