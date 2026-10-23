"""How often a bot's queen dies boxed in with a teammate on one of her sides, per bot (labelled with its arm), from folders
of replays.

Per bot: games; its queen's deaths, those before r300; her deaths on her own move (queen_self: the state at her turn start),
and of those the teammate-involved ones - a teammate's segment ("team" or "team tail") on one of her four sides - with how
many free sides she had then (0 / 1 / 2+); and, from trace builds (BC_DEBUG 1 + BC_ECON_TRACE 1), the teammates that died
rather than take her last exit (the "qbS" tag; once per dragon).

  python -m abyss_tools.kpi.queen_box RUN_CANDIDATE/replays RUN_BASELINE/replays [-j 8]
  abyss-queen-box ...
"""
import argparse
import collections
import concurrent.futures as cf

from ..replay.io import arm_label, base_name, initial_teams, load, replay_files
from ..replay.state import Board

DIRS = "NESW"


def queen_self(path, side):
    """side's queen dying on her own move: at her turn start, each side of her head - blocked (wall, or the step's landing
    occupied) or free - and per free cell whether an enemy head could reach it within its next move (ceil(L/4) free steps +
    min(L - 2, 6) paid, torus Manhattan; walls ignored, so "reachable" is generous); None when she did not die on her own move"""
    rep = load(path)
    bd = Board(rep.map)
    W, H = bd.w, bd.h
    team = initial_teams(rep.map)
    qid = next(i for i in (0, 1) if team.get(i) == side)
    body = {i: list(b) for i, (t, b) in enumerate(bd.dragons)}
    alive = set(body)
    rnd, actor, snap, move = 0, -1, None, None

    def man(p, q):
        dx, dy = abs(p[0] - q[0]), abs(p[1] - q[1])
        return min(dx, W - dx) + min(dy, H - dy)

    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        elif k == "turnStart":
            actor = e.turnStart.id
            if actor == qid and qid in alive:
                occ = {}
                for d in alive:
                    for i, t in enumerate(body[d]):
                        occ[t] = ("self" if d == qid else "team" if team.get(d) == side else "enemy", i == len(body[d]) - 1 and i > 0)
                h = body[qid][0]
                heads = [(body[d][0], len(body[d])) for d in alive if team.get(d) and team.get(d) != side and body[d]]
                cells, block = {}, {}
                for dn in DIRS:
                    n = bd.step(h, dn)
                    if n is None:
                        cells[dn] = None
                        block[dn] = "wall"
                    elif n in occ:
                        cells[dn] = None
                        block[dn] = occ[n][0] + (" tail" if occ[n][1] else "")
                    else:
                        reach = [man(hd, n) <= -(-ln // 4) + min(max(ln - 2, 0), 6) for hd, ln in heads]
                        cells[dn] = {"cell": n, "threat": sum(reach),
                                     "near": min((man(hd, n) for hd, _ in heads), default=None)}
                snap = {"round": rnd, "head": h, "len": len(body[qid]), "cells": cells, "block": block}
        elif k == "dragonAction" and e.dragonAction.id == qid:
            a = e.dragonAction
            move = [str(x) for x in a.action.move] if a._has("action") and a.action.which() == "move" else None
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
            if d == qid:
                if actor != qid or snap is None or snap["round"] != rnd:
                    return None
                free = {dn: c for dn, c in snap["cells"].items() if c}
                return {"round": rnd, "reason": str(e.dragonDeath.reason), "len": snap["len"], "head": list(snap["head"]),
                        "move": move, "free": len(free), "safe": sum(1 for c in free.values() if c["threat"] == 0),
                        "block": snap["block"],
                        "cells": {dn: (c and {"cell": list(c["cell"]), "threat": c["threat"], "near": c["near"]})
                                  for dn, c in snap["cells"].items()}}
            alive.discard(d)
    return None


EARLY = 300


def classify(death_round, own):
    """one side of one game -> {kpi: count}: death_round None (alive) or her death round; own: queen_self's result or None"""
    c = collections.Counter(games=1)
    if death_round is not None:
        c["deaths"] += 1
        c["deaths < r300"] += death_round < EARLY
    if own:
        c["own move"] += 1
        if any(str(v).startswith("team") for v in own["block"].values()):
            c["teammate-involved"] += 1
            c["teammate-involved, free %s" % (min(own["free"], 2) if own["free"] < 2 else "2+")] += 1
    return c


def queen_death_round(path, side):
    """the round side's queen died in, None if she lived"""
    rep = load(path)
    team = initial_teams(rep.map)
    qid = next(i for i in (0, 1) if team.get(i) == side)
    rnd = 0
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        elif k == "dragonDeath" and e.dragonDeath.id == qid:
            return rnd
    return None


def sacrifices(rep):
    """trace replays: per side, the dragons that logged the "qbS" tag (died rather than take their queen's last exit)"""
    team = initial_teams(rep.map)
    out = {"A": set(), "B": set()}
    for e in rep.events:
        k = e.which()
        if k == "dragonSplit":
            team[e.dragonSplit.childId] = "A" if str(e.dragonSplit.team) == "a" else "B"
        elif k == "dragonLog" and " qbS" in e.dragonLog.text and team.get(e.dragonLog.id) in out:
            out[team[e.dragonLog.id]].add(e.dragonLog.id)
    return out


def job(path):
    rep = load(path)
    sac = sacrifices(rep)
    out = []
    for side, name in (("A", rep.botA), ("B", rep.botB)):
        c = classify(queen_death_round(path, side), queen_self(path, side))
        c["sacrifices"] += len(sac[side])
        out.append((base_name(name), dict(c)))
    return out


def table(rows):
    tot = collections.defaultdict(collections.Counter)
    for bot, c in rows:
        tot[bot].update(c)
    out = ["| bot | games | queen deaths | before r300 | on her own move | **teammate-involved** | ... 0 free | 1 free | 2+ free | "
           "teammates sacrificed (trace) |",
           "|---|---|---|---|---|---|---|---|---|---|"]
    for bot, t in sorted(tot.items()):
        out.append(f"| {bot} | {t['games']} | {t['deaths']} | {t['deaths < r300']} | {t['own move']} | {t['teammate-involved']} | "
                   f"{t['teammate-involved, free 0']} | {t['teammate-involved, free 1']} | {t['teammate-involved, free 2+']} | "
                   f"{t['sacrifices']} |")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    rows, n = [], 0
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        for d in a.dirs:
            files = replay_files(d)
            n += len(files)
            arm = arm_label(d)
            for r in ex.map(job, files):
                rows.extend((f"{bot} [{arm}]", c) for bot, c in r)
    print(f"{n} replays")
    print("\n".join(table(rows)))


if __name__ == "__main__":
    main()
