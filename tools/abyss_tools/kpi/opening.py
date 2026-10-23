"""Opening KPIs per map and bot, from a folder of replays (both sides of every game).

First table (medians per map): wins; losses by wipe (the side has no dragon left); the team's first eat (the first round
one of its dragons' bodies grows; 999 if never); total length at r40; units at r100 and their share of the opponent's;
the queen (the side's first starting dragon) alive at r100.

Second table (means per map), the opening judge: units at r50 and r100, the r100 unit difference to the opponent (the
opening's main KPI), eats before r100 (body growths) and the round a wiped side lost its last dragon. With paired seeds
(abyss_tools.run.arena), compare an arm's row with its baseline's.

  python -m abyss_tools.kpi.opening RUN/replays [RUN2/replays ...] [-j 8]
  abyss-opening-kpi ...
"""
import argparse
import collections
import concurrent.futures as cf
import os
import statistics as st

from ..replay.io import base_name, initial_teams, load, replay_files
from ..replay.state import Board


def kpis(events, team, body, winner, left):
    """events: ('round', r) | ('update', id, head, tail) | ('split', parent, child, child_side, parent_body, child_body) |
    ('death', id); team: {id: side} and body: {id: [head .. tail]} of the starting dragons (updated in place); winner: 'A' / 'B' /
    None; left: {side: dragons left at the end}. Returns {side: dict(won, wiped, first, l40, u100, share, q100)}"""
    alive = set(body)
    q = {s: next(i for i in sorted(team) if team[i] == s) for s in 'AB'}
    first = {'A': None, 'B': None}
    eats = {'A': 0, 'B': 0}
    gone = {'A': None, 'B': None}
    at = {s: {} for s in 'AB'}
    rnd = 0
    for ev in events:
        if ev[0] == 'round':
            rnd = ev[1]
            if rnd in (40, 50, 100):
                for s in 'AB':
                    mine = [d for d in alive if team.get(d) == s]
                    at[s][rnd] = (sum(len(body[d]) for d in mine), len(mine), q[s] in alive)
        elif ev[0] == 'update':
            _, d, hd, tl = ev
            b = body.setdefault(d, [hd])
            alive.add(d)
            before = len(b)
            if b[0] != hd:
                b.insert(0, hd)
            while len(b) > 1 and b[-1] != tl:
                b.pop()
            s = team.get(d)
            if len(b) > before and s in first:  # the body grew: an eat
                if first[s] is None:
                    first[s] = rnd
                if rnd < 100:
                    eats[s] += 1
        elif ev[0] == 'split':
            _, p, ch, ch_side, pb, cb = ev
            team[ch] = ch_side
            body[p], body[ch] = list(pb), list(cb)
            alive.add(ch)
        elif ev[0] == 'death':
            alive.discard(ev[1])
            s = team.get(ev[1])
            if s in gone and gone[s] is None and not any(team.get(d) == s for d in alive):
                gone[s] = rnd
    out = {}
    for s, o in (('A', 'B'), ('B', 'A')):
        u100, o100 = at[s].get(100, (0, 0, False))[1], at[o].get(100, (0, 0, False))[1]
        out[s] = dict(won=winner == s, wiped=winner == o and left[s] == 0, first=first[s] if first[s] is not None else 999,
                      l40=at[s].get(40, (0, 0, False))[0], u100=u100, share=u100 / o100 if o100 else (1.0 if u100 else 0.0),
                      q100=at[s].get(100, (0, 0, False))[2], u50=at[s].get(50, (0, 0, False))[1], du100=u100 - o100,
                      eat100=eats[s], wround=gone[s] if winner == o and left[s] == 0 else None)
    return out


def replay_events(r):
    """a replay's events in the form kpis() reads"""
    for e in r.events:
        k = e.which()
        if k == 'roundStart':
            yield ('round', e.roundStart.round)
        elif k == 'dragonUpdate':
            u = e.dragonUpdate
            yield ('update', u.id, (u.head.x, u.head.y), (u.tail.x, u.tail.y))
        elif k == 'dragonSplit':
            sp = e.dragonSplit
            yield ('split', sp.parentId, sp.childId, 'A' if str(sp.team) == 'a' else 'B',
                   [(p.x, p.y) for p in sp.parentBody], [(p.x, p.y) for p in sp.childBody])
        elif k == 'dragonDeath':
            yield ('death', e.dragonDeath.id)


def job(path):
    r = load(path)
    bd = Board(r.map)
    res = r.result
    winner = None if res.which() == 'noWinner' else ('A' if res.winner == 'a' else 'B')
    k = kpis(replay_events(r), initial_teams(r.map), {i: list(b) for i, (t, b) in enumerate(bd.dragons)}, winner,
             {'A': res.teamA.dragonCount, 'B': res.teamB.dragonCount})
    mp = os.path.basename(path).split('__')[0]
    return mp, [dict(k['A'], bot=base_name(r.botA)), dict(k['B'], bot=base_name(r.botB))]


def row(label, b, rs):
    return (f"| {label} | {b} | {len(rs)} | {sum(o['won'] for o in rs)} | {sum(o['wiped'] for o in rs)} | {st.median(o['first'] for o in rs)} | "
            f"{sum(o['first'] <= 30 for o in rs)} | {st.median(o['l40'] for o in rs)} | {st.median(o['u100'] for o in rs)} | "
            f"{st.median(o['share'] for o in rs):.2f} | {sum(o['q100'] for o in rs)} |")


def row_open(label, b, rs):
    """the opening-judge row: means of units r50 / r100, the r100 unit difference and eats before r100; wiped games and the median
    round of the wipe"""
    def mean(k):
        return sum(o[k] for o in rs) / len(rs)
    w = [o['wround'] for o in rs if o['wround'] is not None]
    return (f"| {label} | {b} | {len(rs)} | {mean('u50'):.1f} | {mean('u100'):.1f} | {mean('du100'):+.1f} | {mean('eat100'):.1f} | "
            f"{len(w)} | {st.median(w) if w else '-'} |")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dirs", nargs="+", help="folders of replays (.replay, .replay.zst, .replay.gz)")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    files = replay_files(a.dirs)
    rows = collections.defaultdict(list)
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        for mp, out in ex.map(job, files):
            for o in out:
                rows[(mp, o['bot'])].append(o)
    print("| map | bot | games | won | lost by wipe | first eat (median) | first eat <= r30 | total length r40 (median) | units r100 (median) | "
          "share of theirs r100 (median) | queen alive r100 |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    agg = collections.defaultdict(list)
    for (mp, b), rs in sorted(rows.items()):
        agg[b] += rs
        print(row(mp, b, rs))
    for b, rs in sorted(agg.items()):
        print(row('all', b, rs))
    print("\n| map | bot | games | units r50 | units r100 | r100 unit diff | eats before r100 | wiped | wipe round (median) |")
    print("|---|---|---|---|---|---|---|---|---|")
    for (mp, b), rs in sorted(rows.items()):
        print(row_open(mp, b, rs))
    for b, rs in sorted(agg.items()):
        print(row_open('all', b, rs))


if __name__ == '__main__':
    main()
