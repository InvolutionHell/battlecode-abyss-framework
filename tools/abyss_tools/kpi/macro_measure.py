"""Macro curves of both sides of a replay, one JSON object per replay.

Per side: units, total and longest length every 10 rounds; every split (round, parent length before / after, child length);
pearls eaten by the round marks 50 / 100 / 150 / 200 / 300 / 500 (all, and natural spawns only - a pearl dropped by a dead
body is not a natural spawn); dragon-turns before round 200; deaths by reason and the first death's round; the nearest head
to a bed at rounds 20-100; the bed tiers (bedtiers.py) - the first round a head is within 1 step of a `best` / `fast` /
`fount` bed and the walking distance to them at rounds 0-100; [pearls, dragon-turns] before rounds 12 and 60 (the early
foraging yield); how and when the game ended and the final standings (units, longest, total, queen length).

A pearl eaten = a tileChange that removes a pearl during a dragon's turn; a split = a dragonSplit event (the child takes the
tail part).

  python -m abyss_tools.kpi.macro_measure OUT.jsonl REPLAY_OR_DIR [...] [-j 8]
  abyss-macro-measure ...
"""
import argparse
import concurrent.futures as cf
import json
import os

from ..replay.io import initial_teams, load, replay_files_recursive
from ..replay.state import Board
from .bedtiers import bfs_field, tiers_of

STEP = 10
EAT_MARKS = (50, 100, 150, 200, 300, 500)
BED_MARKS = (0, 10, 20, 30, 40, 60, 100)  # rounds at which the nearest head's walking distance to each bed tier is kept
BED_LAST = 200  # t1 is looked for up to this round


def macro(path):
    """one replay -> its macro row (a JSON-able dict)"""
    rep = load(path)
    board = Board(rep.map)
    team = initial_teams(rep.map)
    body = {i: list(b) for i, (t, b) in enumerate(board.dragons)}  # the cells of each dragon, head first
    alive = set(body)
    units = {"A": {}, "B": {}}
    tot = {"A": {}, "B": {}}
    lmax = {"A": {}, "B": {}}
    splits = {"A": [], "B": []}
    eat = {"A": 0, "B": 0}
    eat_at = {"A": {}, "B": {}}
    dturns200 = {"A": 0, "B": 0}
    deaths = {"A": {}, "B": {}}
    first_death = {"A": None, "B": None}
    start_units = {s: sum(1 for d in alive if team.get(d) == s) for s in "AB"}
    beds = set(board.beds)  # the pearl spawn tiles of the map: a pearl eaten on one is a natural spawn, elsewhere a corpse's drop
    W, H = board.w, board.h
    eat_nat = {"A": 0, "B": 0}
    origin = {}      # tile -> 'n' natural spawn | 'd' corpse drop, of the pearl now on it (pearls of round 0 count as natural)
    chain = False    # True between a dragonDeath and the next event that is not a tileChange: its tileChanges are the drops of the body
    eatn_at = {"A": {}, "B": {}}
    bed_near = {"A": {}, "B": {}}  # at rounds 20 / 40 / 60 / 80 / 100: [min torus distance of a head to a bed, number of dragons with a head within 3 of a bed]
    rnd, actor = 0, None
    # bed tiers of the map by mean gap (bedtiers.py) when the replay's map text carries the gaps (local `unswbc run` replays do):
    # t1 = the first round a living head is within 1 step of a bed of the tier (None = not by round BED_LAST), bed_dmin = the
    # nearest head's walking distance to the tier at the BED_MARKS
    tiers, gbest = tiers_of(board.beds) if board.beds else ({}, None)
    tiers = {k: v for k, v in tiers.items() if v}
    bdist = {k: bfs_field(board, v) for k, v in tiers.items()}
    t1 = {s: {k: None for k in tiers} for s in "AB"}
    bed_dmin = {s: {k: {} for k in tiers} for s in "AB"}
    dt_cum = {"A": 0, "B": 0}     # dragon-turns of the rounds before the current one
    ymarks = {"A": {}, "B": {}}   # [pearls eaten, dragon-turns] before rounds 12 and 60: the yield of rounds 12-59
    for e in rep.events:
        k = e.which()
        if k not in ("tileChange", "dragonDeath"):
            chain = False
        if k == "roundStart":
            nxt = e.roundStart.round
            for m in EAT_MARKS:
                if rnd < m <= nxt:
                    for s in "AB":
                        eat_at[s][str(m)] = eat[s]
                        eatn_at[s][str(m)] = eat_nat[s]
            for m in (12, 60):
                if rnd < m <= nxt:
                    for s in "AB":
                        ymarks[s][str(m)] = [eat[s], dt_cum[s]]
            rnd = nxt
            actor = None
            if tiers and rnd <= BED_LAST:
                for s in "AB":
                    heads = [body[d][0] for d in alive if team.get(d) == s and body.get(d)]
                    if not heads:
                        continue
                    for tk, D in bdist.items():
                        dm = min(D[hy][hx] for hx, hy in heads)
                        if t1[s][tk] is None and dm <= 1:
                            t1[s][tk] = rnd
                        if rnd in BED_MARKS:
                            bed_dmin[s][tk][str(rnd)] = dm
            for s in "AB":
                Ls = [len(body[d]) for d in alive if team.get(d) == s]
                dt_cum[s] += len(Ls)
                if rnd < 200:
                    dturns200[s] += len(Ls)
                if rnd in (20, 40, 60, 80, 100) and beds:
                    heads = [body[d][0] for d in alive if team.get(d) == s and body.get(d)]
                    dd = [min(min(abs(hx - bx), W - abs(hx - bx)) + min(abs(hy - by), H - abs(hy - by)) for bx, by in beds) for hx, hy in heads]
                    bed_near[s][str(rnd)] = [min(dd) if dd else None, sum(1 for v in dd if v <= 3)]
                if rnd % STEP == 0:
                    units[s][str(rnd)] = len(Ls)
                    tot[s][str(rnd)] = sum(Ls)
                    lmax[s][str(rnd)] = max(Ls, default=0)
        elif k == "turnStart":
            actor = e.turnStart.id
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
            before = len(body.get(sp.parentId, ())) or len(sp.parentBody) + len(sp.childBody)
            team[sp.childId] = t
            body[sp.parentId] = [(q.x, q.y) for q in sp.parentBody]
            body[sp.childId] = [(q.x, q.y) for q in sp.childBody]
            alive.add(sp.childId)
            splits[t].append((rnd, before, len(sp.parentBody), len(sp.childBody)))
        elif k == "tileChange":
            tc = e.tileChange
            t = (tc.tile.x, tc.tile.y)
            if tc.hasPearl:
                origin[t] = "d" if chain else "n"
            else:
                o = origin.pop(t, "n")
                if actor is not None and actor in team:
                    eat[team[actor]] += 1
                    if o == "n":
                        eat_nat[team[actor]] += 1
        elif k == "dragonDeath":
            chain = True
            d = e.dragonDeath.id
            s = team.get(d)
            if s:
                why = str(e.dragonDeath.reason)
                deaths[s][why] = deaths[s].get(why, 0) + 1
                if first_death[s] is None:
                    first_death[s] = rnd
            alive.discard(d)
    for m in (12, 60):
        for s in "AB":
            ymarks[s].setdefault(str(m), [eat[s], dt_cum[s]])
    for m in EAT_MARKS:
        for s in "AB":
            eat_at[s].setdefault(str(m), eat[s])
            eatn_at[s].setdefault(str(m), eat_nat[s])
    res = rep.result
    winner = None if res.which() == "noWinner" else ("A" if res.winner == "a" else "B")
    name = next((l.split(None, 1)[1] for l in rep.map.splitlines() if l.startswith("MAP_NAME")), "?")
    how = "wipe" if str(res.endReason) == "teamEliminated" else "r500"
    return {"file": os.path.basename(path), "map": name, "bots": [rep.botA, rep.botB], "rounds": rnd + 1, "winner": winner, "how": how,
            "start_units": start_units, "units": units, "tot": tot, "lmax": lmax, "splits": splits, "eat_at": eat_at, "eatn_at": eatn_at, "bed_near": bed_near, "n_beds": len(beds), "bed_tiers": {k: len(v) for k, v in tiers.items()}, "bed_gbest": gbest, "t1": t1, "bed_dmin": bed_dmin, "ymarks": ymarks, "dturns200": dturns200,
            "deaths": deaths, "first_death": first_death,
            "final": {"A": [res.teamA.dragonCount, res.teamA.longestDragon, res.teamA.totalLength, res.teamA.queenLength],
                      "B": [res.teamB.dragonCount, res.teamB.longestDragon, res.teamB.totalLength, res.teamB.queenLength]}}


def bed_kpis(row, side, cap=200):
    """The exploration KPIs of a side on an unseen map: the first round a dragon is within 1 step of a bed of each tier (never
    within `cap` rounds counts as `cap`), whether that happened by round 60, the nearest head's walking distance to the `best`
    beds at rounds 20 / 40 / 60, units and total length at round 60. None where the replay carries no bed gaps or the map has
    no bed of the tier."""
    out = {}
    t1 = (row.get("t1") or {}).get(side) or {}
    for tk in ("best", "fast", "fount"):
        v = t1.get(tk, "absent")
        out[f"t1 {tk} (never = {cap})"] = None if v == "absent" else float(cap if v is None else min(v, cap))
        out[f"at a {tk} bed by r60"] = None if v == "absent" else float(v is not None and v <= 60)
    dm = ((row.get("bed_dmin") or {}).get(side) or {}).get("best") or {}
    for m in ("20", "40", "60"):
        out[f"nearest head to the best beds @{m}"] = float(dm[m]) if m in dm else None
    out["units@60"] = row["units"][side].get("60")
    out["total length@60"] = row["tot"][side].get("60")
    return out


def yield_kpis(row, side):
    """the early foraging yield: pearls eaten per 100 dragon-turns in rounds 12-59 (None for a row without ymarks)"""
    ym = (row.get("ymarks") or {}).get(side) or {}
    if "12" not in ym or "60" not in ym:
        return {"pearls / 100 dragon-turns (r12-59)": None}
    de, dt = ym["60"][0] - ym["12"][0], ym["60"][1] - ym["12"][1]
    return {"pearls / 100 dragon-turns (r12-59)": (100.0 * de / dt) if dt > 0 else None}


def safe(path):
    try:
        return macro(path)
    except Exception as ex:  # a broken replay must not stop a batch
        return {"file": os.path.basename(path), "error": repr(ex)[:200]}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="JSONL output; rows already in it (by file name) are skipped, so a run can be resumed")
    ap.add_argument("replays", nargs="+", help="replay files or folders")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    paths = [f for p in a.replays for f in (replay_files_recursive(p) if os.path.isdir(p) else [p])]
    run(a.out, paths, a.jobs)


def run(out, paths, jobs=8, quiet=False):
    """append the macro rows of `paths` to the JSONL file `out` (skipping files already in it); returns the rows written"""
    done = set()
    if os.path.exists(out):
        for line in open(out):
            try:
                done.add(json.loads(line)["file"])
            except Exception:  # noqa: BLE001 - a torn last line of an interrupted run
                pass
    todo = [p for p in paths if os.path.basename(p) not in done]
    if not quiet:
        print(f"{len(paths)} replays, {len(todo)} to do, {jobs} workers", flush=True)
    n = 0
    with open(out, "a") as f, cf.ProcessPoolExecutor(jobs) as ex:
        for r in ex.map(safe, todo, chunksize=4):
            f.write(json.dumps(r) + "\n")
            n += 1
            if n % 200 == 0:
                f.flush()
                if not quiet:
                    print(f"  {n} / {len(todo)}", flush=True)
    if not quiet:
        print("done", flush=True)
    return n


if __name__ == "__main__":
    main()
