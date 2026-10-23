"""Region graph of a map at full knowledge, built with the engine's semantics, to check the bot's room module against.

A room is a connected component of the tiles over open edges - walls (kelp) and portals cut. Per room: anchor (its lowest
tile id, y * W + x), size, beds (TILE ... lo hi with hi > 0; for a replay whose map text carries no bed gaps: the tiles a
natural spawn was seen on), exits (its portal sides) and closed (no unpaired portal side). The bot's region.hpp computes the
same rooms incrementally from what its dragons have seen; at full knowledge the two must agree.

  python -m abyss_tools.kpi.region_graph MAP_OR_REPLAY [...] [--json]
  python -m abyss_tools.kpi.region_graph --check [--include BOT_DIR] [MAPS ..] [--maps-dir DIR]
  abyss-region-graph ...

--check builds region_dump.cpp (next to this file) against the bot's region.hpp (--include, default <bots>/abyss) with
c++ -std=c++20 -O1 -DBC_MAPNET=1 -DBC_MAPNET_ROOMS=1, runs it on every map (default: every .map in --maps-dir) - `EXE --dump
MAP` prints `room <anchor> <size> <beds> <exits> <closed>` and `step <tile id> <dir index> <landing id or -1>` lines - and
compares every room and every portal landing with this script's; it exits 0 when all agree. --dump-exe EXE uses a ready
binary, --build-src SRC another dump program.
"""
import argparse
import collections
import glob
import json
import os
import subprocess
import sys
import tempfile

from ..paths import bots_dir, maps_dir
from ..replay.io import load
from ..replay.state import Board

DIRS = "NESW"
DUMP_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "region_dump.cpp")


def graph(text, seen_beds=None):
    """(rooms, steps): rooms = [(anchor, size, beds, exits, closed)] by anchor; steps = {(tile id, dir index): landing id or -1}"""
    bd = Board(text)
    W, H = bd.w, bd.h
    beds = set(bd.beds) or set(seen_beds or ())
    comp = {}
    order = []
    for y in range(H):
        for x in range(W):
            if (x, y) in comp:
                continue
            cid = len(order)
            order.append([])
            st = [(x, y)]
            comp[(x, y)] = cid
            while st:
                t = st.pop()
                order[cid].append(t)
                for d in DIRS:
                    if bd.edge(bd.side(t, d))[0] != ".":
                        continue
                    n = bd.step(t, d)
                    if n is not None and n not in comp:
                        comp[n] = cid
                        st.append(n)
    rooms = []
    for ts in order:
        exits, closed = 0, True
        for t in ts:
            for d in DIRS:
                key = bd.side(t, d)
                if bd.edge(key)[0] == "p":
                    exits += 1
                    if key not in bd.partner:
                        closed = False
        rooms.append((min(y * W + x for x, y in ts), len(ts), sum(1 for t in ts if t in beds), exits, closed))
    steps = {}
    for y in range(H):
        for x in range(W):
            for k, d in enumerate(DIRS):
                if bd.edge(bd.side((x, y), d))[0] == "p":
                    n = bd.step((x, y), d)
                    steps[(y * W + x, k)] = -1 if n is None else n[1] * W + n[0]
    return sorted(rooms), steps


def map_text(path):
    """(map text, beds seen in the game or None) of a .map file or a replay"""
    if path.endswith(".map"):
        with open(path) as f:
            return f.read(), None
    rep = load(path)
    seen = set()
    tick = False
    for e in rep.events:          # without bed gaps in the map text: the tiles a natural spawn was seen on
        k = e.which()
        if k == "roundStart":
            tick = True
        elif k == "turnStart":
            tick = False
        elif k == "tileChange" and tick and e.tileChange.hasPearl:
            seen.add((e.tileChange.tile.x, e.tileChange.tile.y))
    return rep.map, seen


def build_dump(src, include, work):
    exe = os.path.join(work, "region_dump")
    cxx = os.environ.get("CXX", "c++")
    subprocess.run([cxx, "-std=c++20", "-O1", "-w", "-DBC_MAPNET=1", "-DBC_MAPNET_ROOMS=1", "-I", include, src, "-o", exe], check=True)
    return exe


def module(exe, path):
    out = subprocess.run([exe, "--dump", path], capture_output=True, text=True, check=True).stdout.split("\n")
    rooms, steps = [], {}
    for line in out:
        p = line.split()
        if p and p[0] == "room":
            a, s, b, e, c = map(int, p[1:6])
            rooms.append((a, s, b, e, bool(c)))
        elif p and p[0] == "step":
            steps[(int(p[1]), int(p[2]))] = int(p[3])
    return sorted(rooms), steps


def check(paths, exe, P):
    ok_all = True
    P("| map | rooms | closed rooms <= 8 tiles | portal sides | rooms agree | landings agree |")
    P("|---|---|---|---|---|---|")
    for path in paths:
        text, _ = map_text(path)
        r_py, s_py = graph(text)
        r_cc, s_cc = module(exe, path)
        ok_r, ok_s = r_py == r_cc, s_py == s_cc
        ok_all &= ok_r and ok_s
        small = sum(1 for r in r_py if r[1] <= 8 and r[3] > 0)
        P(f"| {os.path.splitext(os.path.basename(path))[0]} | {len(r_py)} | {small} | {len(s_py)} | {'yes' if ok_r else 'NO'} | "
          f"{'yes' if ok_s else 'NO'} |")
        if not ok_r:
            diff = sorted(set(r_py) ^ set(r_cc))[:6]
            P(f"  differences (anchor, size, beds, exits, closed): {diff}")
    return ok_all


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dump-exe", help="--check: a ready native region dump binary")
    ap.add_argument("--build-src", default=DUMP_SRC, help="--check: build the dump binary from this C++ file (default: "
                    "the packaged region_dump.cpp)")
    ap.add_argument("--include", help="--check: the bot folder holding region.hpp (default: <bots>/abyss)")
    ap.add_argument("--maps-dir", help="--check without paths: every .map here (default: <bots>/maps)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    if args.check:
        exe = args.dump_exe
        if not exe:
            include = args.include or os.path.join(bots_dir(), "abyss")
            if not os.path.exists(os.path.join(include, "region.hpp")):
                ap.error(f"no region.hpp in {include} (see --include)")
            exe = build_dump(args.build_src, include, tempfile.mkdtemp(prefix="region-"))
        paths = args.paths or sorted(glob.glob(os.path.join(maps_dir(args.maps_dir), "*.map")))
        lines = []

        def P(s=""):
            print(s, flush=True)
            lines.append(s)

        ok = check(paths, exe, P)
        P("")
        P(f"module == script on {len(paths)} maps: {'YES' if ok else 'NO'}")
        sys.exit(0 if ok else 1)
    if not args.paths:
        ap.error("give a map or a replay")
    for path in args.paths:
        text, seen = map_text(path)
        rooms, steps = graph(text, seen)
        if args.json:
            print(json.dumps({"map": os.path.basename(path), "rooms": rooms}))
            continue
        c = collections.Counter("closed" if r[4] else "open" for r in rooms)
        print(f"{os.path.basename(path)}: {len(rooms)} rooms ({dict(c)}), {len(steps)} portal sides; the 8 largest: "
              + ", ".join(f"anchor {a} size {s} beds {b} exits {e}" for a, s, b, e, _ in sorted(rooms, key=lambda r: -r[1])[:8]))


if __name__ == "__main__":
    main()
