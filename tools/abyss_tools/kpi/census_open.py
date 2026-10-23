"""Opening KPIs of census arms against their same-seed baseline.

An opening change is judged on mechanism KPIs first - above all the r100 unit difference to the opponent - because win
rates against a fixed opponent move slowly. Input: macro_measure rows (one JSON object per replay) of arena runs that share
paired seeds (abyss_tools.run.arena: every arm and the baseline play the same opponent on the same maps, sides and repeats).

Per arm, per map, over the games the arm and the baseline share - (map file stem, opponent, the candidate's side, repeat),
one seed - wins, losses by wipe and the median wipe round, the means of units at r50 / r100, the r100 unit difference to the
opponent, and pearls eaten by r100 (arm - baseline, +- standard error), the median first round at a `best` / `fast` bed
(macro_measure's t1; 200 = never; only for replays whose map text carries the bed gaps) and the early foraging yield (pearls
per 100 dragon-turns in rounds 12-59). A second table per arm: our dragons' deaths per game by cause and the queen alive at
the end.

  python -m abyss_tools.kpi.census_open collect ROWS.jsonl RUN_DIR [RUN_DIR ...] [-j 8]   (macro_measure over the replays)
  python -m abyss_tools.kpi.census_open report ROWS.jsonl --base BASELINE [--arms A B] [--group NAME=MAP1,MAP2] [--md OUT]
  abyss-census-open ...

The map key is the replay file's map stem, so generated maps that share a MAP_NAME stay apart. --group adds a pooled row for
a named set of maps (by MAP_NAME), e.g. --group mazes="Tower Defense,Stripes".
"""
import argparse
import collections
import json
import math
import re
import statistics

NAME = re.compile(r"^(?P<map>.+?)__(?P<a>.+?)_vs_(?P<b>.+?)(?:_r(?P<r>\d+))?\.replay(?:\.zst|\.gz)?$")


def side_kpis(row, side):
    o = "B" if side == "A" else "A"
    u, e = row["units"], row["eat_at"]
    won = row["winner"] == side
    wiped = row["how"] == "wipe" and row["winner"] == o
    t1 = (row.get("t1") or {}).get(side) or {}       # first round a head is within 1 of a best / fast bed; never = 200
    ym = (row.get("ymarks") or {}).get(side) or {}   # [pearls, dragon-turns] before r12 / r60: the early yield
    y = None
    if "12" in ym and "60" in ym and ym["60"][1] > ym["12"][1]:
        y = 100.0 * (ym["60"][0] - ym["12"][0]) / (ym["60"][1] - ym["12"][1])
    fin = (row.get("final") or {}).get(side)
    return {"deaths": dict((row.get("deaths") or {}).get(side) or {}), "qalive": bool(fin and fin[3] > 0),
            "y": y, "t1best": t1.get("best") if t1.get("best") is not None else 200,
            "t1fast": t1.get("fast") if t1.get("fast") is not None else 200,
            "won": won, "wiped": wiped, "wround": row["rounds"] if wiped else None,
            "u50": u[side].get("50", 0), "u100": u[side].get("100", 0),
            "du100": u[side].get("100", 0) - u[o].get("100", 0), "eat100": e[side].get("100", 0)}


def index(rows, cand):
    """{(map stem, opponent, side, repeat): kpis} of the games `cand` played"""
    out = {}
    for row in rows:
        if "error" in row:
            continue
        m = NAME.match(row["file"])
        if not m:
            continue
        a, b = m["a"], m["b"]
        side = "A" if a == cand else "B" if b == cand else None
        if side is None:
            continue
        opp = b if side == "A" else a
        k = side_kpis(row, side)
        k["name"] = row["map"]
        out[(m["map"], opp, side, int(m["r"] or 0))] = k
    return out


def mse(xs):
    """(mean, standard error)"""
    if not xs:
        return 0.0, 0.0
    m = sum(xs) / len(xs)
    if len(xs) < 2:
        return m, 0.0
    v = sum((x - m) ** 2 for x in xs) / (len(xs) - 1)
    return m, math.sqrt(v / len(xs))


def yld(a, b):
    """mean r12-59 yield of arm / base over the pairs where both have it, and the paired difference"""
    pr = [(x["y"], y["y"]) for x, y in zip(a, b) if x.get("y") is not None and y.get("y") is not None]
    if not pr:
        return "-"
    m, s = mse([x - y for x, y in pr])
    return f"{sum(x for x, _ in pr) / len(pr):.2f} / {sum(y for _, y in pr) / len(pr):.2f} ({m:+.2f} ± {s:.2f})"


def table(arm, base, A, B, groups_of=None):
    """the opening table of `arm` against `base`; groups_of: {group name: set of MAP_NAMEs} for extra pooled rows"""
    keys = sorted(set(A) & set(B))
    groups = collections.defaultdict(list)
    extra = list((groups_of or {}).keys())
    for k in keys:
        groups[k[0]].append(k)
        groups["all"].append(k)
        for g, names in (groups_of or {}).items():
            if A[k]["name"] in names:
                groups[g].append(k)
    lines = [f"**{arm}** - {base} (paired games: {len(keys)})", "",
             "| map | games | wins arm / base | wiped arm / base | wipe round (median) arm / base | units r50 diff | units r100 diff | "
             "r100 unit diff to opp: arm / base | pearls by r100 diff | first best bed (round) arm / base | first fast bed arm / base | "
             "pearls / 100 dragon-turns r12-59: arm / base (diff) |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    order = sorted(g for g in groups if g != "all" and g not in extra) + [g for g in extra + ["all"] if g in groups]
    for g in order:
        ks = groups[g]
        a, b = [A[k] for k in ks], [B[k] for k in ks]

        def d(f):
            m, s = mse([x[f] - y[f] for x, y in zip(a, b)])
            return f"{m:+.1f} ± {s:.1f}"

        wr = [x["wround"] for x in a if x["wround"]], [y["wround"] for y in b if y["wround"]]
        wr_s = " / ".join(f"{statistics.median(w):.0f}" if w else "-" for w in wr)
        lines.append(f"| {g} | {len(ks)} | {sum(x['won'] for x in a)} / {sum(y['won'] for y in b)} | "
                     f"{sum(x['wiped'] for x in a)} / {sum(y['wiped'] for y in b)} | {wr_s} | {d('u50')} | {d('u100')} | "
                     f"{mse([x['du100'] for x in a])[0]:+.1f} / {mse([y['du100'] for y in b])[0]:+.1f} | {d('eat100')} | "
                     f"{statistics.median(x['t1best'] for x in a):.0f} / {statistics.median(y['t1best'] for y in b):.0f} | "
                     f"{statistics.median(x['t1fast'] for x in a):.0f} / {statistics.median(y['t1fast'] for y in b):.0f} | {yld(a, b)} |")
    return "\n".join(lines)


def table_deaths(arm, base, A, B):
    """our dragons' deaths per game by cause, and the queen alive at the end (arm / base): an arm that eats more but wins less
    often pays for it in deaths"""
    keys = sorted(set(A) & set(B))
    groups = collections.defaultdict(list)
    for k in keys:
        groups[k[0]].append(k)
        groups["all"].append(k)
    causes = collections.Counter()
    for k in keys:
        causes.update(A[k]["deaths"])
        causes.update(B[k]["deaths"])
    top = [c for c, _ in causes.most_common(4)]
    lines = [f"**{arm}** - {base}: deaths a game by cause and the queen alive at the end (arm / base)", "",
             "| map | games | deaths a game | " + " | ".join(top) + " | queen alive at the end |",
             "|---|---|---|" + "---|" * len(top) + "---|"]
    order = sorted(g for g in groups if g != "all") + (["all"] if "all" in groups else [])
    for g in order:
        ks = groups[g]
        n = len(ks)

        def m(side, c=None):
            src = A if side == "a" else B
            return sum((sum(src[k]["deaths"].values()) if c is None else src[k]["deaths"].get(c, 0)) for k in ks) / n
        cells = [f"{m('a'):.1f} / {m('b'):.1f}"] + [f"{m('a', c):.1f} / {m('b', c):.1f}" for c in top]
        qa, qb = sum(A[k]["qalive"] for k in ks), sum(B[k]["qalive"] for k in ks)
        lines.append(f"| {g} | {n} | " + " | ".join(cells) + f" | {qa} / {qb} |")
    return "\n".join(lines)


def parse_groups(specs):
    out = {}
    for s in specs or []:
        name, _, maps = s.partition("=")
        if not name or not maps:
            raise ValueError(f"--group NAME=MAP1,MAP2: {s!r}")
        out[name] = {m.strip() for m in maps.split(",") if m.strip()}
    return out


def report(rows, base, arms=None, groups_of=None):
    cands = set()
    for row in rows:
        m = NAME.match(row.get("file", ""))
        if m:
            cands.add(m["a"])
    arms = arms or sorted(x for x in cands if x != base)
    B = index(rows, base)
    out = []
    for arm in arms:
        A = index(rows, arm)
        if not set(A) & set(B):
            out.append(f"**{arm}** - {base}: no paired games (the arm and the baseline must play the same opponent on the "
                       "same maps, sides and repeats)")
            continue
        out.append(table(arm, base, A, B, groups_of) + "\n\n" + table_deaths(arm, base, A, B))
    return "\n\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect", help="macro_measure rows of every replay under the run folders")
    c.add_argument("rows")
    c.add_argument("dirs", nargs="+")
    c.add_argument("-j", "--jobs", type=int, default=8)
    r = sub.add_parser("report")
    r.add_argument("rows")
    r.add_argument("--base", required=True, help="the baseline bot (a renamed copy of the carrier, same seeds)")
    r.add_argument("--arms", nargs="*")
    r.add_argument("--group", action="append", default=[], metavar="NAME=MAP1,MAP2", help="a pooled row over these MAP_NAMEs")
    r.add_argument("--md")
    a = ap.parse_args(argv)
    if a.cmd == "collect":
        from ..replay.io import replay_files_recursive
        from . import macro_measure
        paths = [f for d in a.dirs for f in replay_files_recursive(d)]
        macro_measure.run(a.rows, paths, a.jobs)
        return
    with open(a.rows) as f:
        rows = [json.loads(line) for line in f if line.strip()]
    out = report(rows, a.base, a.arms, parse_groups(a.group))
    print(out)
    if a.md:
        with open(a.md, "w") as f:
            f.write(out + "\n")


if __name__ == "__main__":
    main()
