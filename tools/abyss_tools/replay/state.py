"""Replay -> the exact text each dragon received every turn, plus what it did.

Rebuilds the full game state from a replay's events and, at every turnStart event, renders the round block the engine
sent that dragon (a port of the engine's protocol renderer). Paired with the dragon's action this is behaviour-cloning
data, and it lets a native build of a bot be fed the inputs it had in a recorded game (abyss_tools.run.decision_harness).
Rendering the protocol text (instead of features) keeps the bot's own C++ parser the single source of truth.

  python -m abyss_tools.replay.state GAME.replay [--team A|B] [--limit N] > turns.txt
  abyss-replay-state ...

Output records:
  REC <game> <dragonId> <team> <W> <H> <unitLimit> <label>
  <round block lines, exactly as the engine would send them>
  END
where <label> is "MOVE <dirs>", "SPLIT <n>" or "NONE" (timeout / suicide).

The Board class (map geometry with the engine's wall / portal / wrap semantics) is shared by the KPI tools.
"""
import argparse
import os
import sys

from .io import load

DIRS = "NESW"
DXY = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
DIR_NAME = {"north": "N", "east": "E", "south": "S", "west": "W"}
VISION = 7
RADIUS = 3
UNIT_LIMIT = 64
ECHO_SLOT = {"kelp": 0, "ally": 1, "allyHead": 2, "enemy": 3, "enemyHead": 4}  # "empty": nothing


class Board:
    """Map geometry with the engine's semantics: walls (kelp) and portals on tile edges, the map wraps (a torus)."""

    def __init__(self, text):
        self.beds = {}
        self.h_edges = {}  # (x, y) -> (kind, portalId) for the top edge of tile (x, y)
        self.v_edges = {}  # (x, y) -> (kind, portalId) for the left edge of tile (x, y)
        self.partner = {}  # ('H'|'V', x, y) -> ('H'|'V', x, y)
        self.dragons = []
        self.unit_limit = UNIT_LIMIT
        ends = {}
        for line in text.splitlines():
            p = line.split()
            if not p:
                continue
            if p[0] == "END":
                break
            if p[0] == "MAP":
                self.w, self.h = int(p[1]), int(p[2])
            elif p[0] == "UNIT_LIMIT":
                self.unit_limit = int(p[1])
            elif p[0] == "TILE":
                x, y, lo, hi = map(int, p[1:5])
                if hi > 0:
                    self.beds[(x, y)] = (lo, hi)
            elif p[0] == "EDGE":
                idx, kind, pid = map(int, p[1:4])
                where = self._edge_index_of(idx)
                if where is None or kind == 0:
                    continue
                o, x, y = where
                table = self.h_edges if o == "H" else self.v_edges
                if kind == 1:
                    table[(x, y)] = ("w", -1)
                else:
                    old = table.get((x, y))
                    table[(x, y)] = ("p", min(pid, old[1]) if old and old[0] == "p" else pid)
                    ends.setdefault(pid, []).append((o, x, y))
            elif p[0] == "DRAGON":
                team, n = int(p[1]), int(p[2])
                coords = list(map(int, p[3:3 + 2 * n]))
                self.dragons.append(("A" if team == 0 else "B", [(coords[2 * i], coords[2 * i + 1]) for i in range(n)]))
        for pid, lst in ends.items():
            if len(lst) == 2:
                a, b = lst
                self.partner[a] = b
                self.partner[b] = a
                # the engine shows both ends of a pair with the smaller of their two portal ids
                shown = min(self.edge(a)[1], self.edge(b)[1])
                for (o, x, y) in lst:
                    (self.h_edges if o == "H" else self.v_edges)[(x, y)] = ("p", shown)

    def _edge_index_of(self, index):
        stride = self.w + 1
        col, row = index % stride, index // stride
        if index < 0 or row > 2 * self.h:
            return None
        if row % 2 == 0:
            if col == self.w or row == 2 * self.h:
                return None
            return ("H", col, row // 2)
        if col == self.w:
            return None
        return ("V", col, (row - 1) // 2)

    def wrap(self, x, y):
        return (x % self.w, y % self.h)

    def side(self, tile, d):
        x, y = tile
        if d == "N":
            return ("H", x, y)
        if d == "S":
            return ("H",) + self.wrap(x, y + 1)
        if d == "W":
            return ("V", x, y)
        return ("V",) + self.wrap(x + 1, y)

    def edge(self, key):
        o, x, y = key
        return (self.h_edges if o == "H" else self.v_edges).get((x, y), (".", -1))

    def step(self, tile, d):
        key = self.side(tile, d)
        kind, _ = self.edge(key)
        if kind == "w":
            return None
        if kind == "p":
            po, px, py = self.partner[key]
            if po == "H":
                return self.wrap(px, py if d == "S" else py - 1)
            return self.wrap(px if d == "E" else px - 1, py)
        dx, dy = DXY[d]
        return self.wrap(tile[0] + dx, tile[1] + dy)

    def dir_between(self, a, b):
        for d in DIRS:
            if self.step(a, d) == b:
                return d
        raise ValueError(f"not adjacent: {a} {b}")

    def edge_token(self, key):
        kind, pid = self.edge(key)
        return "w" if kind == "w" else str(pid) if kind == "p" else "."


class Dragon:
    __slots__ = ("id", "team", "body", "facing", "alive", "inbox", "echo", "initial", "turns")

    def __init__(self, did, team, body, facing, initial=False):
        self.id, self.team, self.body, self.facing = did, team, list(body), facing
        self.alive, self.inbox = True, []
        self.echo = [0, 0, 0, 0, 0]  # kelp, ally, ally_head, enemy, enemy_head (protocol 3)
        self.initial, self.turns = initial, 0


def in_vision(board, head, tile):
    def within(a, b, size):
        off = ((b - a) % size + size) % size
        return off <= RADIUS or off >= size - RADIUS
    return within(head[0], tile[0], board.w) and within(head[1], tile[1], board.h)


def round_block(board, dragons, pearls, countdown, rnd, me):
    head = me.body[0]
    alive_team = sum(1 for d in dragons if d.alive and d.team == me.team)
    out = [f"ROUND {rnd}", f"DIR {me.facing}", f"LENGTH {len(me.body)}", f"UNIT_COUNT {alive_team}",
           f"NUM_MSGS {len(me.inbox)}"]
    out += [str(v) for v in me.inbox]
    # Protocol 3: every block has ECHOES except a starting dragon's very first turn.
    if not (me.initial and me.turns == 0):
        out.append("ECHOES " + " ".join(map(str, me.echo)))
    for row in range(VISION):
        for col in range(VISION):
            t = board.wrap(head[0] + col - RADIUS, head[1] + row - RADIUS)
            pin = countdown.get(t, 0) if t in board.beds else -1
            out.append(f"{t[0]} {t[1]} {1 if t in pearls else 0} {pin}")
    parts = []
    for d in dragons:
        if not d.alive:
            continue
        for i, seg in enumerate(d.body):
            if not in_vision(board, head, seg):
                continue
            f = d.facing if i == 0 else board.dir_between(seg, d.body[i - 1])
            parts.append(f"{d.team} {d.id} {seg[0]} {seg[1]} {f} {1 if i == 0 else 0}")
    out.append(f"DRAGON_BODIES {len(parts)}")
    out += parts
    for row in range(VISION + 1):
        toks = []
        for col in range(VISION):
            if row < VISION:
                key = board.side(board.wrap(head[0] + col - RADIUS, head[1] + row - RADIUS), "N")
            else:
                key = board.side(board.wrap(head[0] + col - RADIUS, head[1] + row - 1 - RADIUS), "S")
            toks.append(board.edge_token(key))
        out.append(" ".join(toks))
    for row in range(VISION):
        toks = []
        for col in range(VISION + 1):
            if col < VISION:
                key = board.side(board.wrap(head[0] + col - RADIUS, head[1] + row - RADIUS), "W")
            else:
                key = board.side(board.wrap(head[0] + col - 1 - RADIUS, head[1] + row - RADIUS), "E")
            toks.append(board.edge_token(key))
        out.append(" ".join(toks))
    return out


def turns(replay, only_team=None, sample=1.0, rng=None):
    """Yield (dragon, block_lines, label) for every turn in the replay (or a random
    `sample` share of them: the state is still tracked every turn)."""
    board = Board(replay.map)
    dragons = []
    for i, (team, body) in enumerate(board.dragons):
        dragons.append(Dragon(i, team, body, board.dir_between(body[1], body[0]), initial=True))
    by_id = {d.id: d for d in dragons}
    pearls, countdown = set(), {}
    rnd = 0
    pending = None  # (dragon, block) waiting for its action event
    for e in replay.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
            # the engine decrements every bed's countdown each round but only emits an event when it draws a new
            # countdown; do the silent decrement here
            for t in countdown:
                countdown[t] -= 1
        elif k == "pearlCountdown":
            ev = e.pearlCountdown
            countdown[(ev.tile.x, ev.tile.y)] = ev.countdown
        elif k == "tileChange":
            ev = e.tileChange
            t = (ev.tile.x, ev.tile.y)
            (pearls.add if ev.hasPearl else pearls.discard)(t)
        elif k == "turnStart":
            me = by_id[e.turnStart.id]
            wanted = (only_team is None or me.team == only_team) and (sample >= 1.0 or rng.random() < sample)
            block = round_block(board, dragons, pearls, countdown, rnd, me) if wanted else None
            me.inbox = []
            me.echo = [0, 0, 0, 0, 0]
            me.turns += 1
            pending = (me, block)
        elif k == "dragonAction":
            a = e.dragonAction
            me, block = pending
            if block is not None:
                if a.tle or not a._has("action"):
                    label = "NONE"
                else:
                    w = a.action.which()
                    label = ("MOVE " + "".join(DIR_NAME[str(s)] for s in a.action.move) if w == "move"
                             else f"SPLIT {a.action.split}" if w == "split" else "NONE")
                yield me, block, label
            pending = None
        elif k == "dragonUpdate":
            ev = e.dragonUpdate
            d = by_id[ev.id]
            head, tail = (ev.head.x, ev.head.y), (ev.tail.x, ev.tail.y)
            d.facing = DIR_NAME[str(ev.facing)]
            if d.body[0] != head:
                d.body.insert(0, head)
            while len(d.body) > 1 and d.body[-1] != tail:
                d.body.pop()
        elif k == "dragonSplit":
            ev = e.dragonSplit
            parent = by_id[ev.parentId]
            parent.body = [(p.x, p.y) for p in ev.parentBody]
            child = Dragon(ev.childId, "A" if str(ev.team) == "a" else "B",
                           [(p.x, p.y) for p in ev.childBody], DIR_NAME[str(ev.childFacing)])
            dragons.append(child)
            by_id[child.id] = child
        elif k == "dragonDeath":
            by_id[e.dragonDeath.id].alive = False
        elif k == "sonarPing":
            ev = e.sonarPing
            sender = by_id[ev.senderId]
            end = (ev.end.x, ev.end.y)
            value = ev.value64 or ev.value
            if ev.which() == "hitId":
                by_id[ev.hitId].inbox.append(value)
            kind = str(ev.hitKind)
            if kind in ECHO_SLOT:
                sender.echo[ECHO_SLOT[kind]] += 1
            elif kind == "unknown":  # older replays: classify by hand
                if ev.which() == "hitId":
                    hit = by_id[ev.hitId]
                    head = bool(hit.body) and hit.body[0] == end
                    sender.echo[(1 if hit.team == sender.team else 3) + (1 if head else 0)] += 1
                elif board.step(end, DIR_NAME[str(ev.direction)]) is None:
                    sender.echo[0] += 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="+")
    ap.add_argument("--team", choices=["A", "B"])
    ap.add_argument("--limit", type=int, default=0, help="stop after this many turns (debugging)")
    args = ap.parse_args(argv)
    n = 0
    w = sys.stdout.write
    for path in args.replays:
        r = load(path)
        game = os.path.basename(path).split(".replay")[0]
        board = Board(r.map)
        for me, block, label in turns(r, args.team):
            w(f"REC {game} {me.id} {me.team} {board.w} {board.h} {board.unit_limit} {label}\n")
            w("\n".join(block))
            w("\nEND\n")
            n += 1
            if args.limit and n >= args.limit:
                return


if __name__ == "__main__":
    main()
