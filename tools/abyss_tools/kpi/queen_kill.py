"""How a bot's queen dies, per bot (labelled with its arm), from folders of replays.

Per bot: games; its queen's deaths before the end; of them on an enemy's turn (the dragon moving is the other side's); of those,
the head-on kills (`hitHeadToHead`) by the mover's move of n steps, split by the mover's length L before its move: a step
(n = 1), a free sprint (2 <= n <= ceil(L / 4): no segment paid) and a paid sprint (n > ceil(L / 4)); per 100 games the
free-sprint kills and the enemy-turn deaths; and bucket 2 (its queen dead and the other's alive at the end: a lost game at r500).

  python -m abyss_tools.kpi.queen_kill RUN_CANDIDATE/replays RUN_BASELINE/replays [-j 8]
  abyss-queen-kill ...
"""
import argparse
import collections
import concurrent.futures as cf

from ..replay.io import arm_label, base_name, initial_teams, load, replay_files
from ..replay.state import Board
from .queen_feed import QueenTrack


def kind(steps, mover_len):
    """the kill's move: step / free sprint / paid sprint"""
    if steps <= 1:
        return "step"
    return "free" if steps <= (mover_len + 3) // 4 else "paid"


class KillTrack(QueenTrack):
    """QueenTrack plus the mover of each turn: who moved, how many steps, its length before the move"""

    def __init__(self, bodies, team):
        super().__init__(bodies, team)
        self.actor, self.steps, self.mover_len = -1, 0, 0
        self.deaths = collections.Counter()      # side -> her deaths
        self.enemy_turn = collections.Counter()  # ... on an enemy's turn
        self.kills = {s: collections.Counter() for s in "AB"}  # ... head-on by the mover's step / free / paid sprint

    def on_turn(self, actor):
        self.actor, self.steps, self.mover_len = actor, 0, len(self.body.get(actor, []))

    def on_move(self, steps):
        self.steps = steps
        self.mover_len = len(self.body.get(self.actor, []))

    def on_death_why(self, did, why):
        for s in "AB":
            if did != self.queen[s]:
                continue
            self.deaths[s] += 1
            att = self.team.get(self.actor)
            if self.actor != did and att is not None and att != s:
                self.enemy_turn[s] += 1
                if why == "hitHeadToHead":
                    self.kills[s][kind(self.steps, self.mover_len)] += 1
        self.on_death(did)

    def kill_rows(self, names):
        out = []
        for s in "AB":
            o = "B" if s == "A" else "A"
            r = {"games": 1, "deaths": self.deaths[s], "enemy turn": self.enemy_turn[s], "step": self.kills[s]["step"],
                 "free": self.kills[s]["free"], "paid": self.kills[s]["paid"],
                 "bucket2": int(self.queen[s] not in self.alive and self.queen[o] in self.alive)}
            out.append((names[s], r))
        return out


def job(path):
    rep = load(path)
    bd = Board(rep.map)
    t = KillTrack({i: b for i, (_, b) in enumerate(bd.dragons)}, initial_teams(rep.map))
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            t.on_round(e.roundStart.round)
        elif k == "turnStart":
            t.on_turn(e.turnStart.id)
        elif k == "dragonAction":
            a = e.dragonAction
            if not a.tle and a._has("action") and a.action.which() == "move":
                t.on_move(len(a.action.move))
        elif k == "dragonUpdate":
            u = e.dragonUpdate
            t.on_update(u.id, (u.head.x, u.head.y), (u.tail.x, u.tail.y))
        elif k == "dragonSplit":
            sp = e.dragonSplit
            t.on_split(sp.parentId, sp.childId, "A" if str(sp.team) == "a" else "B",
                       [(p.x, p.y) for p in sp.parentBody], [(p.x, p.y) for p in sp.childBody])
        elif k == "dragonDeath":
            t.on_death_why(e.dragonDeath.id, str(e.dragonDeath.reason))
    return t.kill_rows({"A": base_name(rep.botA), "B": base_name(rep.botB)})


def table(rows):
    """[(bot, kpis)] -> markdown lines, one row a bot"""
    tot = collections.defaultdict(collections.Counter)
    for bot, r in rows:
        tot[bot].update(r)

    def per100(t, a):
        return f"{100.0 * t[a] / t['games']:.1f}" if t["games"] else "-"

    out = ["| bot | games | queen deaths | on an enemy turn | head-on: step | **free sprint** | paid sprint | "
           "free sprints / 100 games | enemy-turn deaths / 100 games | bucket 2 (hers dead, theirs alive) |",
           "|---|---|---|---|---|---|---|---|---|---|"]
    for bot, t in sorted(tot.items()):
        out.append(f"| {bot} | {t['games']} | {t['deaths']} | {t['enemy turn']} | {t['step']} | {t['free']} | {t['paid']} | "
                   f"{per100(t, 'free')} | {per100(t, 'enemy turn')} | {t['bucket2']} |")
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
