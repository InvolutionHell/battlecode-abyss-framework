#!/usr/bin/env python3
"""Consequence labels of every dragon-turn of a replay: what the dragon goes on to do. They are the targets of the
auxiliary heads (pretrain.py --aux-weight) and of the action-value heads (train_f1.py --q / --q-returns / --q-team).

  bit 1  split5  the dragon splits in one of its next 5 turns (rounds r+1..r+5)
  bit 2  eat5    it eats a pearl this turn or in the next 4 (r..r+4)
  bit 3  die10   it dies by enemy contact this turn or in the next 9 (r..r+9): head-on with an enemy or running into an
                 enemy's body. Deaths into walls, its own body or a teammate do not count: a deliberate no-action death
                 that feeds a teammate is not a risk signal.
  bit 4  kill10  an enemy dies on it (runs into its body, or a head-on trade) in r..r+9
  bit 7  set: the record carries these labels

Bit 0 stays the game's result, so the byte is the records' outcome field (bits 5-6 hold the row's weight class).
A pearl that vanishes during a dragon's turn was eaten by it; a dragon that dies running into another's body during
its own turn (hitOtherBody) is credited to the dragon whose segment lies on the path of its move; a head-on trade
credits both.

  labels = aux_labels(replay)        # {(dragon id, round): bits}
  python3 rl/data/aux_labels.py GAME.replay ...   # label rates of a few replays

The returns used by the action-value heads are below: eat_returns (a dragon's discounted intake), lineage_returns (the
same over the dragon and the dragons split off it), king_returns (the discounted growth of the team's longest dragon)
and team_labels (the team's longest-dragon growth, its death and the change of the total-length lead over 50 rounds).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay_src import Board, DIR_NAME  # noqa: E402

SPLIT, EAT, DIE, KILL, HAS = 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 7


def path_hit(board, head, moves, bodies, me):
    """The dragon whose segment lies on the path of `me`'s move from `head` (the others' bodies: they do not move
    during its turn)."""
    occupied = {seg: d for d, b in bodies.items() if d != me for seg in b}
    for mv in moves:
        head = board.step(head, mv)
        if head is None:
            return None
        if head in occupied:
            return occupied[head]
    return None


def births(replay):
    """{parent id: [(child id, the round of the split)]}"""
    out, rnd = {}, 0
    for e in replay.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        elif k == "dragonSplit":
            out.setdefault(e.dragonSplit.parentId, []).append((e.dragonSplit.childId, rnd))
    return out


def events(replay):
    """Per dragon: its turn rounds, split / eat / kill rounds and its death round."""
    board = Board(replay.map)
    body = {i: list(b) for i, (_, b) in enumerate(board.dragons)}
    team = {i: t for i, (t, _) in enumerate(board.dragons)}
    turns, splits, eats, kills, death = {}, {}, {}, {}, {}   # death: rounds of deaths by enemy contact
    rnd, cur, move, h2h = 0, None, None, []

    def credit(d, r):
        kills.setdefault(d, []).append(r)

    def settle_h2h():  # head-on deaths come in pairs within one turn: each side is credited with the other
        for (a, ra), (b, rb) in zip(h2h[0::2], h2h[1::2]):
            if team.get(a) != team.get(b):
                credit(a, ra)
                credit(b, rb)
                death[a], death[b] = ra, rb
        h2h.clear()

    for e in replay.events:
        k = e.which()
        if k in ("roundStart", "turnStart"):
            settle_h2h()
            move = None
        if k == "roundStart":
            rnd, cur = e.roundStart.round, None
        elif k == "turnStart":
            cur = e.turnStart.id
            turns.setdefault(cur, []).append(rnd)
        elif k == "dragonAction":
            a = e.dragonAction
            if cur is None or a.tle or not a._has("action"):
                continue
            w = a.action.which()
            if w == "split":
                splits.setdefault(cur, []).append(rnd)
            elif w == "move" and cur in body:
                move = (body[cur][0], [DIR_NAME[str(s)] for s in a.action.move])  # others do not move in its turn
        elif k == "tileChange":
            if not e.tileChange.hasPearl and cur is not None:
                eats.setdefault(cur, []).append(rnd)
        elif k == "dragonUpdate":
            ev = e.dragonUpdate
            head, tail = (ev.head.x, ev.head.y), (ev.tail.x, ev.tail.y)
            b = body.setdefault(ev.id, [head])
            if b[0] != head:
                b.insert(0, head)
            while len(b) > 1 and b[-1] != tail:
                b.pop()
        elif k == "dragonSplit":
            ev = e.dragonSplit
            body[ev.parentId] = [(p.x, p.y) for p in ev.parentBody]
            body[ev.childId] = [(p.x, p.y) for p in ev.childBody]
            team[ev.childId] = "A" if str(ev.team) == "a" else "B"
        elif k == "dragonDeath":
            d, why = e.dragonDeath.id, str(e.dragonDeath.reason)
            if why == "hitOtherBody" and d == cur and move is not None:
                killer = path_hit(board, move[0], move[1], body, d)
                if killer is not None and team.get(killer) != team.get(d):
                    credit(killer, rnd)
                    death[d] = rnd
            elif why == "hitHeadToHead":
                h2h.append((d, rnd))
            body.pop(d, None)
    settle_h2h()
    return turns, splits, eats, death, kills


def aux_labels(replay):
    turns, splits, eats, death, kills = events(replay)
    out = {}
    for d, rounds in turns.items():
        sp, ea, ki = set(splits.get(d, ())), set(eats.get(d, ())), set(kills.get(d, ()))
        dr = death.get(d)
        for r in rounds:
            bits = HAS
            if any(r + i in sp for i in range(1, 6)):
                bits |= SPLIT
            if any(r + i in ea for i in range(5)):
                bits |= EAT
            if dr is not None and r <= dr <= r + 9:
                bits |= DIE
            if any(r + i in ki for i in range(10)):
                bits |= KILL
            out[(d, r)] = bits
    return out


def _rates(paths):
    from replay_src import load
    tot = [0] * 5
    for p in paths:
        lab = aux_labels(load(p))
        tot[0] += len(lab)
        for i, bit in enumerate((SPLIT, EAT, DIE, KILL)):
            tot[i + 1] += sum(1 for v in lab.values() if v & bit)
    n = max(1, tot[0])
    print(f"{tot[0]} dragon-turns: split5 {tot[1] / n:.3f} eat5 {tot[2] / n:.3f} die10 {tot[3] / n:.3f} kill10 {tot[4] / n:.3f}")


def eat_returns(replay, horizon=20, gamma=0.9):
    """Each turn's discounted future intake, sum over k < horizon of gamma^k x the pearls the dragon eats in round r + k
    (its own eats only: a split child's are its own). {(dragon id, round): return}"""
    turns, _, eats, _, _ = events(replay)
    out = {}
    for d, rounds in turns.items():
        per = {}
        for r in eats.get(d, ()):
            per[r] = per.get(r, 0) + 1
        for r in rounds:
            out[(d, r)] = sum(gamma ** k * per.get(r + k, 0) for k in range(horizon))
    return out


def king_returns(replay, horizon=50, gamma=0.95):
    """Each round's discounted growth of the team's longest dragon (the "king"), sum over k < horizon of
    gamma^k x (K(r + k + 1) - K(r + k)), K(r) = the team's longest length at round r's start (0 once wiped). The same for
    every dragon of the team in that round: the action-value head learns what each class adds (a no-action death next
    to the longest dragon feeds it). {("A" / "B", round): return}"""
    board = Board(replay.map)
    body = {i: list(b) for i, (_, b) in enumerate(board.dragons)}
    team = {i: t for i, (t, _) in enumerate(board.dragons)}
    king = {"A": [], "B": []}
    for e in replay.events:
        k = e.which()
        if k == "roundStart":
            r = e.roundStart.round
            for t in "AB":
                king[t].extend([king[t][-1] if king[t] else 0] * max(0, r - len(king[t])))
                king[t].append(max((len(b) for d, b in body.items() if team.get(d) == t), default=0))
        elif k == "dragonUpdate":
            ev = e.dragonUpdate
            head, tail = (ev.head.x, ev.head.y), (ev.tail.x, ev.tail.y)
            b = body.setdefault(ev.id, [head])
            if b[0] != head:
                b.insert(0, head)
            while len(b) > 1 and b[-1] != tail:
                b.pop()
        elif k == "dragonSplit":
            ev = e.dragonSplit
            body[ev.parentId] = [(p.x, p.y) for p in ev.parentBody]
            body[ev.childId] = [(p.x, p.y) for p in ev.childBody]
            team[ev.childId] = "A" if str(ev.team) == "a" else "B"
        elif k == "dragonDeath":
            body.pop(e.dragonDeath.id, None)
    out = {}
    for t in "AB":
        K = king[t]
        for r in range(len(K)):
            out[(t, r)] = sum(gamma ** k * ((K[r + k + 1] if r + k + 1 < len(K) else K[-1]) - (K[r + k] if r + k < len(K) else K[-1]))
                              for k in range(horizon))
    return out


def lineage_returns(replay, horizon=20, gamma=0.9):
    """eat_returns over the dragon's lineage - its own pearls plus those of the dragons split off it (and off them) from
    this round on, gamma^k x the pearls in round r + k, k < horizon. A split's value then holds what the child goes on
    to eat, which the dragon's own intake misses (with the own intake every split scores low, and a policy re-ranked by
    it stops splitting). {(dragon id, round): return}"""
    turns, _, eats, _, _ = events(replay)
    kids = births(replay)
    per = {}
    for d, rs in eats.items():
        for r in rs:
            per.setdefault(d, {}).setdefault(r, 0)
            per[d][r] += 1

    def lineage(d, r, until):
        """d and the dragons split off it (recursively) at rounds in [r, until)"""
        out = [d]
        for c, born in kids.get(d, ()):
            if r <= born < until:
                out += lineage(c, born, until)
        return out

    out = {}
    for d, rounds in turns.items():
        for r in rounds:
            tot = 0.0
            for x in lineage(d, r, r + horizon):
                px = per.get(x)
                if px:
                    tot += sum(gamma ** k * px.get(r + k, 0) for k in range(horizon))
            out[(d, r)] = tot
    return out



def team_labels(replay, horizon=50):
    """Team-level targets per (team, round), from the replay's global truth -
      kg   the team's longest length at r + horizon minus at r (segments; 0 once wiped)
      kd   1 if the team's king at r (its longest, the lowest id on a tie) dies within horizon rounds
      tot  the change of (our total length - the enemy's) over horizon rounds
    the game's last state standing in for rounds past the end. {("A" / "B", round): (kg, kd, tot)}"""
    board = Board(replay.map)
    body = {i: list(b) for i, (_, b) in enumerate(board.dragons)}
    team = {i: t for i, (t, _) in enumerate(board.dragons)}
    snaps, died = [], {}  # per round: {team: (longest, king id, total)}; dragon -> death round
    rnd = 0

    def snap():
        s = {}
        for t in "AB":
            mine = [(len(b), -d, d) for d, b in body.items() if team.get(d) == t and b]
            s[t] = (max(mine)[0], max(mine)[2], sum(m[0] for m in mine)) if mine else (0, -1, 0)
        return s

    for e in replay.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
            while len(snaps) < rnd:
                snaps.append(snaps[-1] if snaps else snap())
            snaps.append(snap())
        elif k == "dragonUpdate":
            ev = e.dragonUpdate
            head, tail = (ev.head.x, ev.head.y), (ev.tail.x, ev.tail.y)
            b = body.setdefault(ev.id, [head])
            if b[0] != head:
                b.insert(0, head)
            while len(b) > 1 and b[-1] != tail:
                b.pop()
        elif k == "dragonSplit":
            ev = e.dragonSplit
            body[ev.parentId] = [(p.x, p.y) for p in ev.parentBody]
            body[ev.childId] = [(p.x, p.y) for p in ev.childBody]
            team[ev.childId] = "A" if str(ev.team) == "a" else "B"
        elif k == "dragonDeath":
            died.setdefault(e.dragonDeath.id, rnd)
            body.pop(e.dragonDeath.id, None)
    snaps.append(snap())  # after the last round
    last = len(snaps) - 1
    out = {}
    for r in range(len(snaps) - 1):
        f = min(r + horizon, last)
        for t, o in (("A", "B"), ("B", "A")):
            kl, kid, tt = snaps[r][t]
            kg = snaps[f][t][0] - kl
            kd = 1 if kid >= 0 and kid in died and died[kid] <= r + horizon else 0
            tot = (snaps[f][t][2] - snaps[f][o][2]) - (tt - snaps[r][o][2])
            out[(t, r)] = (kg, kd, tot)
    return out


if __name__ == "__main__":
    if not sys.argv[1:] or sys.argv[1] in ("-h", "--help"):
        print("usage: python3 rl/data/aux_labels.py GAME.replay ...   (prints the label rates of these replays)")
        sys.exit(0 if sys.argv[1:] else 2)
    _rates(sys.argv[1:])
