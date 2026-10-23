"""Random symmetric maps for map-agnostic testing and tuning (the tournament maps are unseen).

  python -m abyss_tools.maps.mapgen --out maps/gen --count 600 --seed 1                  # a training pool
  python -m abyss_tools.maps.mapgen --out maps/held --count 25 --seed 900000 --prefix h  # held out (unseen-map checks)
  python -m abyss_tools.maps.mapgen --check maps/gen                 # every map through the engine once (starter vs starter)
  python -m abyss_tools.maps.mapgen --check maps/gen --check-bot DIR # the same with another bot folder
  abyss-mapgen ...

The engine requires the DRAGON lines to alternate teams (ids follow the file order and ids 0 / 1 are the queens): a dragon
is written right after its mirror, A first (--b-first: B first, so id 0 is team B's queen).

Each map is deterministic in (seed + index). Styles follow the official map pool: size 16-64, x / y / xy symmetry, kelp
walls, rooms and corridors (the open tiles stay connected), paired portals, and pearl beds that are uniform-slow (Big
Empty), sparse (Trophy, Default), fountain clusters (Devil, Stronghold) or medium (Schooltime). 2-4 length-3 dragons per
team, mirrored. Map file format: https://game.battlecode.au/docs/map-files
"""
import argparse
import concurrent.futures as cf
import os
import random
import subprocess

from ..paths import bots_dir, unswbc

SIZES = [(16, 16), (20, 20), (25, 25), (25, 35), (32, 16), (32, 32), (40, 24), (48, 24), (48, 32),
         (40, 40), (60, 40), (64, 32), (64, 64)]


class Gen:
    def __init__(self, rng, w, h, sym):
        self.r, self.w, self.h, self.sym = rng, w, h, sym
        self.kelp = set()        # ("h", x, y): north side of (x, y); ("v", x, y): west side of (x, y)
        self.portal = {}         # edge -> id
        self.beds = {}           # (x, y) -> (min, max)
        self.dragons = []        # (team, [(x, y) head first])

    # ---- symmetry -----------------------------------------------------------
    def tiles_mirror(self, x, y):
        out = {(x, y)}
        if self.sym in ("x", "xy"):
            out |= {(a, self.h - 1 - b) for a, b in list(out)}
        if self.sym in ("y", "xy"):
            out |= {(self.w - 1 - a, b) for a, b in list(out)}
        return out

    def fx(self, e):  # y -> h-1-y: a north side becomes the north side of the tile below
        k, a, b = e
        return (k, a, (self.h - b) % self.h if k == "h" else self.h - 1 - b)

    def fy(self, e):  # x -> w-1-x: a west side becomes the west side of the tile to the right
        k, a, b = e
        return (k, (self.w - a) % self.w if k == "v" else self.w - 1 - a, b)

    def edge_images(self, e):
        """Images of an edge under the symmetry group, one per group element, in a fixed order."""
        e = (e[0], e[1] % self.w, e[2] % self.h)
        if self.sym == "x":
            return [e, self.fx(e)]
        if self.sym == "y":
            return [e, self.fy(e)]
        return [e, self.fx(e), self.fy(e), self.fx(self.fy(e))]

    def edge_mirror(self, e):
        return set(self.edge_images(e))

    def team_b(self, x, y):
        """Team B's spawn is team A's mirrored through the map's symmetry."""
        if self.sym == "x":
            return x, self.h - 1 - y
        if self.sym == "y":
            return self.w - 1 - x, y
        return self.w - 1 - x, self.h - 1 - y

    # ---- geometry -----------------------------------------------------------
    def side(self, x, y, d):
        """Edge between (x, y) and its neighbour in direction d (0 N, 1 E, 2 S, 3 W)."""
        if d == 0:
            return ("h", x, y)
        if d == 2:
            return ("h", x, (y + 1) % self.h)
        if d == 3:
            return ("v", x, y)
        return ("v", (x + 1) % self.w, y)

    def step(self, x, y, d):
        return ((x + (1 if d == 1 else -1 if d == 3 else 0)) % self.w,
                (y + (1 if d == 2 else -1 if d == 0 else 0)) % self.h)

    def component(self, start):
        seen, stack = {start}, [start]
        while stack:
            x, y = stack.pop()
            for d in range(4):
                if self.side(x, y, d) in self.kelp:
                    continue
                n = self.step(x, y, d)
                if n not in seen:
                    seen.add(n)
                    stack.append(n)
        return seen

    def add_kelp(self, e):
        for m in self.edge_mirror(e):
            self.kelp.add(m)

    def walls(self, density):
        """Straight kelp walls until about `density` of the edges are kelp; walls that
        would cut the open tiles into pieces are undone."""
        total = 2 * self.w * self.h
        tries = 0
        while len(self.kelp) < density * total and tries < 400:
            tries += 1
            x, y = self.r.randrange(self.w), self.r.randrange(self.h)
            horiz = self.r.random() < 0.5
            length = self.r.randint(2, max(3, min(self.w, self.h) // 3))
            added = []
            for i in range(length):
                e = ("h", (x + i) % self.w, y) if horiz else ("v", x, (y + i) % self.h)
                for m in self.edge_mirror(e):
                    if m not in self.kelp:
                        self.kelp.add(m)
                        added.append(m)
            if len(self.component((0, 0))) < self.w * self.h:
                for m in added:
                    self.kelp.discard(m)

    def portals(self, pairs):
        pid = 0
        for _ in range(pairs * 10):
            if pid >= 2 * pairs:
                break
            k = self.r.choice("hv")
            a = (k, self.r.randrange(self.w), self.r.randrange(self.h))
            b = (k, self.r.randrange(self.w), self.r.randrange(self.h))
            ma, mb = self.edge_images(a), self.edge_images(b)
            used = set(self.kelp) | set(self.portal)
            # every image distinct: no self-mirrored portal ends, no overlap between the two ends
            if len(set(ma) | set(mb)) != len(ma) + len(mb) or any(e in used for e in ma + mb):
                continue
            for ea, eb in zip(ma, mb):
                self.portal[ea] = pid
                self.portal[eb] = pid
                pid += 1

    def set_bed(self, x, y, lo, hi):
        for t in self.tiles_mirror(x, y):
            self.beds[t] = (lo, hi)

    def pearls(self, style):
        w, h, r = self.w, self.h, self.r
        if style == "uniform":           # Big Empty: every tile a slow bed
            hi = r.choice([300, 500, 750, 1000])
            for x in range(w):
                for y in range(h):
                    self.beds[(x, y)] = (1, hi)
        elif style == "sparse":          # Trophy / Queen of Spades / Default: few beds, long gaps
            for _ in range(r.randint(w * h // 40, w * h // 12)):
                self.set_bed(r.randrange(w), r.randrange(h), r.choice([1, 5, 10]), r.choice([30, 50, 100, 200]))
        elif style == "fountains":       # Devil / Stronghold: fountain clusters + background
            for _ in range(r.randint(2, 6)):
                cx, cy = r.randrange(w), r.randrange(h)
                for _ in range(r.randint(2, 5)):
                    self.set_bed((cx + r.randint(-2, 2)) % w, (cy + r.randint(-2, 2)) % h, 1, 1)
            for _ in range(r.randint(w * h // 20, w * h // 6)):
                self.set_bed(r.randrange(w), r.randrange(h), r.choice([5, 10, 20]), r.choice([30, 60, 100]))
        else:                            # medium: Schooltime
            for _ in range(r.randint(w * h // 12, w * h // 5)):
                lo = r.choice([10, 20, 30])
                self.set_bed(r.randrange(w), r.randrange(h), lo, lo + r.choice([30, 60, 180]))

    def spawn(self, per_team):
        occupied = set()
        for _ in range(per_team):
            for _ in range(200):
                x, y, d = self.r.randrange(self.w), self.r.randrange(self.h), self.r.randrange(4)
                body = [(x, y)]
                ok = True
                for _ in range(2):   # length 3, straight, tail behind the head
                    bx, by = body[-1]
                    if self.side(bx, by, (d + 2) % 4) in self.kelp or self.side(bx, by, (d + 2) % 4) in self.portal:
                        ok = False
                        break
                    body.append(self.step(bx, by, (d + 2) % 4))
                mirror = [self.team_b(*p) for p in body]
                cells = set(body) | set(mirror)
                if not ok or len(cells) != 6 or cells & occupied:
                    continue
                # keep the two teams apart at the start
                if min(abs(a[0] - b[0]) + abs(a[1] - b[1]) for a in body for b in mirror) < 4:
                    continue
                occupied |= cells
                self.dragons.append((0, body))
                self.dragons.append((1, mirror))
                break

    # ---- output ---------------------------------------------------------------
    def edge_index(self, e):
        k, x, y = e
        stride = self.w + 1
        return (2 * y) * stride + x if k == "h" else (2 * y + 1) * stride + x

    def text(self, name, b_first=False):
        lines = [f"MAP {self.w} {self.h}", f"SYMMETRY {self.sym}", f"MAP_NAME {name}",
                 f"TILE_COUNT {self.w * self.h}"]
        for y in range(self.h):
            for x in range(self.w):
                lo, hi = self.beds.get((x, y), (0, 0))
                lines.append(f"TILE {x} {y} {lo} {hi}")
        edges = [(self.edge_index(e), 1, -1) for e in self.kelp]
        edges += [(self.edge_index(e), 2, p) for e, p in self.portal.items()]
        edges.sort()
        lines.append(f"EDGE_COUNT {len(edges)}")
        lines += [f"EDGE {i} {k} {p}" for i, k, p in edges]
        # the engine requires the teams to alternate; spawn() appends each A dragon followed by its mirror, so the queens
        # (ids 0 / 1) are a mirrored pair
        order = list(self.dragons)
        if b_first:
            order = [d for i in range(0, len(order), 2) for d in (order[i + 1], order[i])]
        lines.append(f"DRAGON_COUNT {len(order)}")
        for team, body in order:
            lines.append(f"DRAGON {team} {len(body)} " + " ".join(f"{x} {y}" for x, y in body))
        return "\n".join(lines) + "\n"


def make(seed, name, b_first=False):
    r = random.Random(seed)
    w, h = r.choice(SIZES)
    sym = r.choice(["x", "y", "xy"])
    if sym == "x" and h % 2:
        sym = "y" if w % 2 == 0 else "xy"
    g = Gen(r, w, h, sym)
    g.walls(r.choice([0.0, 0.05, 0.1, 0.15, 0.25]))
    g.portals(r.choice([0, 0, 1, 1, 2, 3]))
    g.pearls(r.choice(["uniform", "sparse", "sparse", "fountains", "medium", "medium"]))
    g.spawn(r.choice([2, 2, 3, 4]))
    return g.text(name, b_first)


def check(path, bot, exe=None):
    """one quick native game of `bot` against itself: the engine rejects a malformed map"""
    p = subprocess.run([unswbc(exe), "run", path, bot, bot, "--no-replay"], capture_output=True, text=True,
                       timeout=600, cwd=os.path.dirname(bot))
    ok = p.returncode == 0 and ("wins" in p.stdout + p.stderr or "draw" in p.stdout + p.stderr)
    return path, ok, (p.stdout + p.stderr)[-300:]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="maps/gen", help="output folder (default maps/gen)")
    ap.add_argument("--count", type=int, default=600)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--prefix", default="g")
    ap.add_argument("--check", metavar="DIR", help="play every map of DIR once; a rejected map is renamed *.map.bad")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--check-bot", default="", help="bot folder for --check (default <bots>/starter)")
    ap.add_argument("--unswbc", help="the unswbc executable")
    ap.add_argument("--b-first", action="store_true", help="write team B's dragon before its mirror (id 0 = B's queen)")
    args = ap.parse_args(argv)
    if args.check:
        bot = os.path.abspath(args.check_bot) if args.check_bot else os.path.join(bots_dir(), "starter")
        maps = sorted(os.path.join(os.path.abspath(args.check), f) for f in os.listdir(args.check) if f.endswith(".map"))
        bad = 0
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for path, ok, tail in pool.map(lambda m: check(m, bot, args.unswbc), maps):
                if not ok:
                    bad += 1
                    print("BAD", path, tail.replace("\n", " | "))
                    os.rename(path, path + ".bad")
        print(f"{len(maps) - bad}/{len(maps)} maps ok")
        return 0 if bad == 0 else 1
    os.makedirs(args.out, exist_ok=True)
    for i in range(args.count):
        name = f"{args.prefix}{args.seed + i:06d}"
        with open(os.path.join(args.out, name + ".map"), "w") as fh:
            fh.write(make(args.seed + i, name, args.b_first))
    print(f"wrote {args.count} maps to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
