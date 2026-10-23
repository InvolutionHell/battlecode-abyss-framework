"""The yield of entries into small closed rooms, from folders of replays.

A room: a region of at most --cap tiles (default 8) joined by open edges, from the map (walls and portals bound it). Small
rooms with beds are pearl traps: a short dragon that walks in can eat and split, or die boxed in. An entry: one of the bot's
dragons whose head steps into a room; by default only harvest-like entries - the step eats (the tail stays) and the dragon
was 2-3 long before it (--all-entries: every entry). It is followed until the head leaves, the dragon dies or the game ends:
  net segments = the lengths of the children it split while inside + its length if it left alive - its length in
                 (= pearls eaten - 2 when a parent of 2 dies inside)
  net dragons  = children - 1 if it died inside

  python -m abyss_tools.kpi.harvest_yield BOT RUN/replays [--map stripes] [--bot OTHER] [--cap 8] [--all-entries]
  abyss-harvest-yield ...
"""
import argparse
import collections
import statistics as st

from ..replay.io import initial_teams, load, replay_files
from ..replay.state import Board


def rooms(bd, cap=8):
    """{tile: (room id, its size)} for the regions of at most cap tiles joined by open edges"""
    reg, regions = {}, []
    for y in range(bd.h):
        for x in range(bd.w):
            if (x, y) in reg:
                continue
            rid = len(regions)
            q = [(x, y)]
            reg[(x, y)] = rid
            for t in q:
                for d in 'NESW':
                    kind, _ = bd.edge(bd.side(t, d))
                    if kind in ('w', 'p'):
                        continue
                    n = bd.step(t, d)
                    if n is None or n in reg:
                        continue
                    reg[n] = rid
                    q.append(n)
            regions.append(q)
    return {t: (r, len(regions[r])) for t, r in reg.items() if len(regions[r]) <= cap}


def track(events, room, team, side, body):
    """the entries of side's dragons. events: ('split', parent, child, child_side, parent_body, child_body) | ('update', id, head,
    tail) | ('death', id); room: rooms(); team: {id: side}; body: {id: [head .. tail]} at the start (both updated in place)"""
    open_e, done = {}, []
    for ev in events:
        if ev[0] == 'split':
            _, p, ch, ch_side, pb, cb = ev
            team[ch] = ch_side
            body[p], body[ch] = list(pb), list(cb)
            if p in open_e:
                open_e[p]['children'].append(len(cb))
        elif ev[0] == 'update':
            _, d, hd, tl = ev
            b = body.setdefault(d, [hd])
            prev_tail = b[-1]
            moved = b[0] != hd
            if moved:
                b.insert(0, hd)
            while len(b) > 1 and b[-1] != tl:
                b.pop()
            if team.get(d) != side:
                continue
            if hd in room and d not in open_e and moved:
                ate = tl == prev_tail
                open_e[d] = dict(id=d, room=room[hd][0], size=room[hd][1], ate=ate, len_in=len(b) - 1 if ate else len(b),
                                 children=[], died=False)
            elif hd not in room and d in open_e:
                e = open_e.pop(d)
                e['len_out'] = len(b)
                done.append(e)
        elif ev[0] == 'death':
            d = ev[1]
            if d in open_e:
                e = open_e.pop(d)
                e['died'], e['len_out'] = True, 0
                done.append(e)
    for d, e in open_e.items():
        e['len_out'] = len(body.get(d, []))
        done.append(e)
    for e in done:
        e['net_seg'] = sum(e['children']) + e['len_out'] - e['len_in']
        e['net_drg'] = len(e['children']) - (1 if e['died'] else 0)
    return done


def replay_entries(path, bot, cap=8):
    """the entries of `bot`'s dragons (bot matched as a substring of the replay's bot names) in one replay"""
    r = load(path)
    bd = Board(r.map)
    side = 'A' if bot in r.botA else 'B'
    team = initial_teams(r.map)
    body = {i: list(b) for i, (t, b) in enumerate(bd.dragons)}

    def events():
        for e in r.events:
            k = e.which()
            if k == 'dragonSplit':
                sp = e.dragonSplit
                yield ('split', sp.parentId, sp.childId, 'A' if str(sp.team) == 'a' else 'B',
                       [(p.x, p.y) for p in sp.parentBody], [(p.x, p.y) for p in sp.childBody])
            elif k == 'dragonUpdate':
                u = e.dragonUpdate
                yield ('update', u.id, (u.head.x, u.head.y), (u.tail.x, u.tail.y))
            elif k == 'dragonDeath':
                yield ('death', e.dragonDeath.id)
    return track(events(), rooms(bd, cap), team, side, body)


def harvest_like(e):
    return e['ate'] and e['len_in'] in (2, 3)


def table(rows):
    """rows: [(label, entries)] -> markdown, one line per (label, room size)"""
    out = ["| bot | room tiles | entries | net segments (sum / mean) | net dragons (sum / mean) | +2 segments and +1 dragon | died inside |",
           "|---|---|---|---|---|---|---|"]
    for label, es in rows:
        by = collections.defaultdict(list)
        for e in es:
            by[e['size']].append(e)
        for size, g in sorted(by.items()):
            out.append(f"| {label} | {size} | {len(g)} | {sum(e['net_seg'] for e in g):+d} / {st.mean(e['net_seg'] for e in g):+.2f} | "
                       f"{sum(e['net_drg'] for e in g):+d} / {st.mean(e['net_drg'] for e in g):+.2f} | "
                       f"{sum(e['net_seg'] >= 2 and e['net_drg'] >= 1 for e in g)} | {sum(e['died'] for e in g)} |")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('bot', help='the bot whose entries are counted (matched in the replay\'s bot names)')
    ap.add_argument('dirs', nargs='+', help='replay directories (.replay or .replay.zst)')
    ap.add_argument('--bot', dest='others', action='append', default=[], help='another bot to count in the same replays')
    ap.add_argument('--map', default='', help='only replays whose file name starts with this (a map stem); default: all')
    ap.add_argument('--cap', type=int, default=8)
    ap.add_argument('--all-entries', action='store_true', help='count every entry, not only harvest-like ones')
    a = ap.parse_args(argv)
    files = replay_files(a.dirs, a.map)
    rows = []
    for bot in [a.bot] + a.others:
        es = [e for f in files for e in replay_entries(f, bot, a.cap)]
        rows.append((bot, es if a.all_entries else [e for e in es if harvest_like(e)]))
    print(f"{len(files)} replays")
    print(table(rows))


if __name__ == '__main__':
    main()
