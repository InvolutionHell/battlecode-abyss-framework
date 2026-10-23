"""The round-500 queen rule (rules 1.2.3) measured from replays.

At round 500 the winner is the side with the longer QUEEN (the team's dragon among ids 0 / 1; dead = length 0), then the
longer longest dragon, then the larger total length. On a split the parent keeps its id. A move's first ceil(L / 4) steps are
free; every further step costs a segment. Replay format 2 adds TeamStanding.queenLength. The map text may list team B's
dragon first, so id 0 is not always team A's.

measure(path) returns, per side: how the game ended (wipe / queen dead / queen length / longest / total), queen survival
(death round, cause, own move or not), queen length at r100 .. r499 and the side's total then, the share of rounds the queen
is the side's longest, multi-step moves by dragon length and the number of paid steps, head-on kills of the enemy queen and of
other dragons, the queen's roaming (distinct head tiles from r100), her mean distance to the nearest enemy head, fresh pearl
spawns by the side nearer to them, and two consistency checks: `check` (the tracked final standings equal the replay's
result) and `rule_ok` (the winner predicted by the queen rule equals the engine's; None for a wipe).

  python -m abyss_tools.replay.rules REPLAY [REPLAY ...] [--json OUT.jsonl] [-j 8]
  abyss-rules ...
"""
import argparse
import collections
import concurrent.futures as cf
import json
import math
import os

from .io import initial_teams, load
from .state import Board

CK = (100, 200, 300, 400, 499)
BUCKETS = ((1, 3), (4, 7), (8, 15), (16, 999))


def measure(path):
    """one replay -> a JSON-able dict of the quantities listed in the module doc"""
    import numpy as np
    rep = load(path)
    board = Board(rep.map)
    team = initial_teams(rep.map)
    body = {i: list(b) for i, (t, b) in enumerate(board.dragons)}
    alive = set(body)
    rnd, actor = 0, -1
    kinds = collections.Counter(e.which() for e in rep.events)
    queen = {team[i]: i for i in (0, 1) if i in team}
    qlen = {"A": {}, "B": {}}
    side_tot = {"A": {}, "B": {}}
    qlong = {"A": 0, "B": 0}
    qrounds = {"A": 0, "B": 0}
    qdeath = {"A": None, "B": None}
    moves = {s: collections.Counter() for s in "AB"}      # (bucket, steps) -> turns
    paid = {s: 0 for s in "AB"}
    # queen hunting, per attacking side: exposure (enemy queen-rounds, enemy non-queen dragon-rounds at L >= 2) and
    # head-on kills made on the attacker's turn; strikes = the queen kills with the killer's move, steps and length
    hunt = {s: {"exp_queen": 0, "exp_other": 0, "k_queen": 0, "k_other": 0, "strikes": []} for s in "AB"}
    steps_now = 0
    qtiles = {s: set() for s in "AB"}                   # the queen's head tiles r100-r499 (roaming)
    # fresh spawns: a pearl appearing on a tile that held no dead body in the last 60 rounds (r > 0); which side's
    # nearest head is closer by terrain steps (bodies ignored, BFS <= 30), and who ate it; per phase r0-199 / r200-499
    corpse, spend = {}, {}
    spawn = {ph: collections.Counter() for ph in ("r0-199", "r200-499")}
    qdist = {s: [] for s in "AB"}                       # torus Manhattan distance to the nearest enemy head, every 10 rounds
    W, H = board.w, board.h
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
            for s in "AB":
                L = {d: len(body[d]) for d in alive if team.get(d) == s}
                q = queen[s]
                o = "B" if s == "A" else "A"
                if q in alive:
                    qrounds[s] += 1
                    qlong[s] += L[q] >= max(L.values())
                    hunt[o]["exp_queen"] += 1
                    if rnd >= 100 and rnd % 10 == 0:
                        hx, hy = body[q][0]
                        ds = [min(abs(hx - x), W - abs(hx - x)) + min(abs(hy - y), H - abs(hy - y))
                              for d in alive if team.get(d) == o for x, y in body[d][:1]]
                        if ds:
                            qdist[s].append(min(ds))
                hunt[o]["exp_other"] += sum(1 for d, x in L.items() if d != q and x >= 2)
                if rnd in CK:
                    qlen[s][rnd] = L.get(q, 0)
                    side_tot[s][rnd] = sum(L.values())
        elif k == "turnStart":
            actor = e.turnStart.id
            steps_now = 0
        elif k == "dragonAction":
            a = e.dragonAction
            if actor in alive and not a.tle and a._has("action") and a.action.which() == "move":
                n = len(a.action.move)
                steps_now = n
                L = len(body[actor])
                b = next(i for i, (lo, hi) in enumerate(BUCKETS) if lo <= L <= hi)
                s = team.get(actor)
                moves[s][(b, min(n, 4))] += 1
                paid[s] += max(0, n - math.ceil(L / 4))
        elif k == "dragonUpdate":
            u = e.dragonUpdate
            d = u.id
            hd, tl = (u.head.x, u.head.y), (u.tail.x, u.tail.y)
            b = body.setdefault(d, [hd])
            before = len(b)
            if b[0] != hd:
                b.insert(0, hd)
                if d < 2 and rnd >= 100:
                    qtiles[team.get(d)].add(hd)
            while len(b) > 1 and b[-1] != tl:
                b.pop()
            if len(b) > before and hd in spend:
                ph, closer = spend.pop(hd)
                spawn[ph][f"closer {closer} eaten by {team.get(d)}"] += 1
        elif k == "dragonSplit":
            sp = e.dragonSplit
            team[sp.childId] = "A" if str(sp.team) == "a" else "B"
            body[sp.parentId] = [(q.x, q.y) for q in sp.parentBody]
            body[sp.childId] = [(q.x, q.y) for q in sp.childBody]
            alive.add(sp.childId)
        elif k == "tileChange":
            tc = e.tileChange
            t = (tc.tile.x, tc.tile.y)
            if tc.hasPearl and rnd > 0 and not (t in corpse and rnd - corpse[t] <= 60):
                heads = collections.defaultdict(set)
                for d in alive:
                    if body.get(d):
                        heads[body[d][0]].add(team.get(d))
                dist, q, found = {t: 0}, [t], {}
                for x in q:
                    for tm in heads.get(x, ()):
                        found.setdefault(tm, dist[x])
                    if len(found) == 2 or dist[x] >= 30:
                        if len(found) == 2:
                            break
                        continue
                    for dn in "NESW":
                        y = board.step(x, dn)
                        if y is not None and y not in dist:
                            dist[y] = dist[x] + 1
                            q.append(y)
                a, bb = found.get("A", 99), found.get("B", 99)
                closer = "A" if a < bb else "B" if bb < a else "tie"
                ph = "r0-199" if rnd < 200 else "r200-499"
                spawn[ph]["n"] += 1
                spawn[ph]["closer " + closer] += 1
                spend[t] = (ph, closer)
        elif k == "dragonDeath":
            d = e.dragonDeath.id
            s = team.get(d)
            why = str(e.dragonDeath.reason)
            for t in body.get(d, []):
                corpse[t] = rnd
            if s and d == queen[s]:
                qdeath[s] = (rnd, why, d == actor)
            att = team.get(actor)
            if why == "hitHeadToHead" and d != actor and att and att != s:
                if d == queen[s]:
                    hunt[att]["k_queen"] += 1
                    hunt[att]["strikes"].append((rnd, len(body[d]), steps_now, len(body.get(actor, [])) if actor in alive else -1))
                elif len(body[d]) >= 2:
                    hunt[att]["k_other"] += 1
            alive.discard(d)
    res = rep.result
    w = None if res.which() == "noWinner" else ("A" if res.winner == "a" else "B")
    final = {}
    for s in "AB":
        L = [len(body[d]) for d in alive if team.get(d) == s]
        final[s] = (len(L), max(L, default=0), sum(L), len(body[queen[s]]) if queen[s] in alive else 0)
    got = {"A": (res.teamA.dragonCount, res.teamA.longestDragon, res.teamA.totalLength, res.teamA.queenLength),
           "B": (res.teamB.dragonCount, res.teamB.longestDragon, res.teamB.totalLength, res.teamB.queenLength)}
    check = all(final[s] == got[s] for s in "AB")
    # how it ended, from the replay's own standings
    if str(res.endReason) == "teamEliminated":
        how = "wipe"
    elif got["A"][3] != got["B"][3]:
        how = "queen dead" if min(got["A"][3], got["B"][3]) == 0 else "queen length"
    elif got["A"][1] != got["B"][1]:
        how = "longest"
    else:
        how = "total"
    # does the replay's winner agree with the queen rule (r500 games)?
    if how != "wipe":
        key = lambda s: (got[s][3], got[s][1], got[s][2])  # noqa: E731
        pred = "A" if key("A") > key("B") else "B" if key("B") > key("A") else None
        rule_ok = pred == w
    else:
        rule_ok = None
    qlen = {s: {str(r): v for r, v in qlen[s].items()} for s in "AB"}        # string keys, as after a JSON round trip
    side_tot = {s: {str(r): v for r, v in side_tot[s].items()} for s in "AB"}
    return {"file": os.path.basename(path), "rounds": rnd + 1, "winner": w, "how": how, "rule_ok": rule_ok,
            "format": rep.formatVersion, "queen_ids": queen,
            "check": check, "final": final, "result": got, "kinds": dict(kinds), "bots": (rep.botA, rep.botB),
            "qlen": qlen, "side_tot": side_tot, "qdeath": qdeath,
            "qlong_share": {s: qlong[s] / max(1, qrounds[s]) for s in "AB"},
            "moves": {s: {f"{b}:{n}": c for (b, n), c in moves[s].items()} for s in "AB"}, "paid": paid,
            "hunt": hunt, "qroam": {s: len(qtiles[s]) for s in "AB"}, "spawn": {ph: dict(c) for ph, c in spawn.items()},
            "qdist": {s: (float(np.mean(qdist[s])) if qdist[s] else None) for s in "AB"}}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="+")
    ap.add_argument("--json", help="write one JSON object per replay here")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        rows = list(ex.map(measure, a.replays))
    for m in rows:
        print(f"{m['file']}: rounds {m['rounds']}, winner {m['winner']}, ended by {m['how']}, queen rule ok {m['rule_ok']}, "
              f"parser check {m['check']}; queens (A, B) final {m['result']['A'][3]} / {m['result']['B'][3]}, "
              f"deaths {m['qdeath']['A']} / {m['qdeath']['B']}")
    if a.json:
        with open(a.json, "w") as fo:
            for m in rows:
                fo.write(json.dumps(m) + "\n")


if __name__ == "__main__":
    main()
