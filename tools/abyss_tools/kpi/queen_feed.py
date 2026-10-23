"""A bot's queen in the late game (from r350), per bot (labelled with its arm), from folders of replays.

At round 500 the longer queen wins, so feeding the queen late matters. Per bot: games, wins; games with its queen alive at
r350 and of them short (< 8); her mean length at r350; her pearls eaten r350-500 (each growth of her body by n segments counts
n: an eat grows the head without moving the tail); her deaths r350-500; games with her alive at the end and her mean length
then; games with both queens alive at the end and that bucket's wins.

  python -m abyss_tools.kpi.queen_feed RUN_CANDIDATE/replays RUN_BASELINE/replays [-j 8]
  abyss-queen-feed ...
"""
import argparse
import collections
import concurrent.futures as cf

from ..replay.io import arm_label, base_name, initial_teams, load, replay_files
from ..replay.state import Board

FROM = 350  # the late-game window starts here
SHORT = 8   # a queen shorter than this at FROM is 'short'


class QueenTrack:
    """bodies from replay events (head first); both queens' r350 length, their growth after r350 and their deaths"""

    def __init__(self, bodies, team):
        self.body = {i: list(b) for i, b in bodies.items()}
        self.alive = set(self.body)
        self.team = dict(team)
        self.queen = {s: next(i for i in (0, 1) if self.team.get(i) == s) for s in "AB"}
        self.at = {}                  # side -> her length at r350 (None: dead by then)
        self.eats = collections.Counter()
        self.died = set()             # sides whose queen died from r350 on
        self.round = 0

    def on_round(self, rnd):
        self.round = rnd
        if rnd == FROM:
            for s in "AB":
                q = self.queen[s]
                self.at[s] = len(self.body[q]) if q in self.alive else None

    def on_update(self, did, head, tail):
        b = self.body.setdefault(did, [head])
        self.alive.add(did)
        before = len(b)
        if b[0] != head:
            b.insert(0, head)
        while len(b) > 1 and b[-1] != tail:
            b.pop()
        for s in "AB":
            if did == self.queen[s] and self.at.get(s) is not None and len(b) > before:
                self.eats[s] += len(b) - before

    def on_split(self, parent, child, side, parent_body, child_body):
        self.team[child] = side
        self.body[parent] = list(parent_body)
        self.body[child] = list(child_body)
        self.alive.add(child)

    def on_death(self, did):
        self.alive.discard(did)
        for s in "AB":
            if did == self.queen[s] and self.round >= FROM and self.at.get(s) is not None:
                self.died.add(s)

    def rows(self, names, winner):
        """-> per side (bot, {kpi: value})"""
        both = all(self.queen[s] in self.alive for s in "AB")
        out = []
        for s in "AB":
            q, L = self.queen[s], self.at.get(s)
            r = {"games": 1, "wins": int(winner == s), "alive r350": int(L is not None), "short r350": int(L is not None and L < SHORT),
                 "L r350": L or 0, "eats": self.eats[s], "died": int(s in self.died),
                 "alive end": int(L is not None and q in self.alive), "L end": len(self.body[q]) if L is not None and q in self.alive else 0,
                 "both alive": int(both), "both wins": int(both and winner == s)}
            out.append((names[s], r))
        return out


def job(path):
    rep = load(path)
    bd = Board(rep.map)
    t = QueenTrack({i: b for i, (_, b) in enumerate(bd.dragons)}, initial_teams(rep.map))
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            t.on_round(e.roundStart.round)
        elif k == "dragonUpdate":
            u = e.dragonUpdate
            t.on_update(u.id, (u.head.x, u.head.y), (u.tail.x, u.tail.y))
        elif k == "dragonSplit":
            sp = e.dragonSplit
            t.on_split(sp.parentId, sp.childId, "A" if str(sp.team) == "a" else "B",
                       [(p.x, p.y) for p in sp.parentBody], [(p.x, p.y) for p in sp.childBody])
        elif k == "dragonDeath":
            t.on_death(e.dragonDeath.id)
    res = rep.result
    winner = None if res.which() == "noWinner" else ("A" if res.winner == "a" else "B")
    return t.rows({"A": base_name(rep.botA), "B": base_name(rep.botB)}, winner)


def table(rows):
    """[(bot, kpis)] -> markdown lines, one row a bot"""
    tot = collections.defaultdict(collections.Counter)
    for bot, r in rows:
        tot[bot].update(r)

    def mean(t, a, n):
        return f"{t[a] / t[n]:.1f}" if t[n] else "-"

    out = ["| bot | games | wins | queen alive r350 | short (< 8) | mean L r350 | **pearls she ate r350-500** | per game alive r350 | "
           "died r350-500 | alive at the end | mean L end | both queens alive (wins) |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for bot, t in sorted(tot.items()):
        out.append(f"| {bot} | {t['games']} | {t['wins']} | {t['alive r350']} | {t['short r350']} | {mean(t, 'L r350', 'alive r350')} | "
                   f"{t['eats']} | {mean(t, 'eats', 'alive r350')} | {t['died']} | {t['alive end']} | {mean(t, 'L end', 'alive end')} | "
                   f"{t['both alive']} ({t['both wins']}) |")
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
                rows.extend((f"{bot} [{arm}]", k) for bot, k in r)
    print(f"{n} replays")
    print("\n".join(table(rows)))


if __name__ == "__main__":
    main()
