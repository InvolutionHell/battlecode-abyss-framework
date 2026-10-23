"""Pearl-timing variants of existing maps: the same map (size, symmetry, kelp and portal edges, starting dragons) with
different pearl timings.

Contest maps may come in variants that look the same but have completely different pearl timings, so a bot must read the
beds from what it sees, not from the map's look. Everything but the beds is kept; the beds (TILE x y lo hi: a pearl again
lo..hi rounds after it is taken, hi 0 = no bed) are redrawn, identically on symmetric tiles, so both teams keep the same
economy.

Modes (a variant draws one at random unless --mode is given):
  shuffle  the map's own timings on other tiles: its fountains and fast beds move, clusters kept as 3x3 blocks
  family   the timings of another given map, drawn per 3x3 block, scaled to this map's bed density
  density  the map's own timings with the number of beds halved or doubled (slow beds only)

  python -m abyss_tools.maps.pearl_variants --out maps/pv --per-map 5 --seed 1 bots/maps/devil.map bots/maps/trauma.map ...
  abyss-pearl-variants ...
"""
import argparse
import os
import random


def load(path):
    with open(path) as fh:
        lines = fh.read().splitlines()
    w = h = 0
    sym = "none"
    tiles = {}
    for i, line in enumerate(lines):
        p = line.split()
        if not p:
            continue
        if p[0] == "MAP":
            w, h = int(p[1]), int(p[2])
        elif p[0] == "SYMMETRY":
            sym = p[1]
        elif p[0] == "TILE" and len(p) >= 5:
            tiles[(int(p[1]), int(p[2]))] = (i, int(p[3]), int(p[4]))
    return lines, w, h, sym, tiles


def mirror(x, y, w, h, sym):
    if sym == "xy":
        return w - 1 - x, h - 1 - y
    if sym == "y":
        return w - 1 - x, y
    if sym == "x":
        return x, h - 1 - y
    return x, y


def orbits(tiles, w, h, sym):
    """Groups of tiles that must share a timing (a tile and its mirror image)."""
    seen, out = set(), []
    for t in sorted(tiles):
        if t in seen:
            continue
        m = mirror(*t, w, h, sym)
        group = [t] + ([m] if m != t and m in tiles else [])
        seen.update(group)
        out.append(group)
    return out


def blocks(orbs, w, h):
    """Orbits grouped by the 3x3 block of their first tile: the unit clusters move in."""
    by = {}
    for g in orbs:
        x, y = g[0]
        by.setdefault((x // 3, y // 3), []).append(g)
    return list(by.values())


def variant(path, donors, rng, mode):
    lines, w, h, sym, tiles = load(path)
    orbs = orbits(tiles, w, h, sym)
    own = [tiles[g[0]][1:] for g in orbs]           # (lo, hi) per orbit, in orbit order
    beds = sum(1 for v in own if v[1] > 0)
    if mode == "shuffle":
        # move whole 3x3 blocks of timings to other blocks (fountain clusters stay clusters, elsewhere)
        # (the timings are laid out block by block in a shuffled block order and dealt back in the original order:
        # the same multiset, runs of a cluster stay together)
        bl = blocks(orbs, w, h)
        order = list(range(len(bl)))
        rng.shuffle(order)
        flat = [tiles[g[0]][1:] for i in order for g in bl[i]]
        new = {}
        k = 0
        for b in bl:
            for g in b:
                new[tuple(g)] = flat[k]
                k += 1
    elif mode == "family":
        donor = rng.choice([d for d in donors if os.path.basename(d) != os.path.basename(path)])
        _, dw, dh, dsym, dtiles = load(donor)
        dorbs = orbits(dtiles, dw, dh, dsym)
        dbl = [[dtiles[g[0]][1:] for g in b] for b in blocks(dorbs, dw, dh)]
        # keep this map's bed density: thin or thicken the donor's beds to the same share of tiles
        dshare = sum(1 for b in dbl for v in b if v[1] > 0) / max(1, sum(len(b) for b in dbl))
        share = beds / max(1, len(orbs))
        keep = min(1.0, share / dshare) if dshare > 0 else 0.0
        new = {}
        for b in blocks(orbs, w, h):
            pat = rng.choice(dbl)
            for j, g in enumerate(b):
                v = pat[j % len(pat)]
                if v[1] > 0 and rng.random() > keep:
                    v = (0, 0)
                new[tuple(g)] = v
        mode = f"family:{os.path.basename(donor)[:-4]}"
    else:  # density
        factor = rng.choice([0.5, 2.0])
        slow = [v for v in own if v[1] > 30]
        new = {}
        for g, v in zip(orbs, own):
            if factor < 1 and v[1] > 30 and rng.random() < 0.5:
                v = (0, 0)
            elif factor > 1 and v[1] == 0 and slow and rng.random() < beds / max(1, len(orbs)):
                v = rng.choice(slow)
            new[tuple(g)] = v
        mode = f"density:{factor}"
    out = list(lines)
    for g, (lo, hi) in new.items():
        for t in g:
            i = tiles[t][0]
            out[i] = f"TILE {t[0]} {t[1]} {lo} {hi}"
    return out, mode


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("maps", nargs="+", help="the .map files (every one also serves as a family donor)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--per-map", type=int, default=5)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--mode", choices=["shuffle", "family", "density"], default=None)
    ap.add_argument("--only", default="", help="comma list of map names to make variants of; every given map still "
                    "serves as a family donor (adds variants for a new map without redrawing the others)")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    rng = random.Random(a.seed)
    only = {x for x in a.only.split(",") if x}
    for path in a.maps:
        base = os.path.basename(path)[:-4]
        if only and base not in only:
            continue
        if only:
            rng = random.Random(f"{a.seed}:{base}")
        for k in range(a.per_map):
            mode = a.mode or rng.choice(["shuffle", "family", "density"])
            if mode == "family" and len(a.maps) < 2:
                mode = "shuffle"                     # a family variant needs another map as the donor
            lines, what = variant(path, a.maps, rng, mode)
            name = f"{base}_pv{k}"
            lines = [f"MAP_NAME {name}" if line.startswith("MAP_NAME") else line for line in lines]
            with open(os.path.join(a.out, name + ".map"), "w") as fh:
                fh.write("\n".join(lines) + "\n")
            print(f"{name}: {what}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
