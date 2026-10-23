"""Compile-time macro sweep: arms of a carrier bot that differ in a few #define values, played against an opponent next to a
same-seed baseline (a renamed copy of the carrier), and one markdown report.

  build    <bots>/<prefix><k>_c per arm = the carrier + `#undef / #define NAME VALUE` lines appended to its ablation.hpp
           (the bot's switch header, included by every unit), and <bots>/<prefix>base_c = the carrier as it is (the
           baseline). Every arm's cached sandbox builds are cleared (unswbc hashes only .c / .cc / .cpp files, not headers).
           One arm per macro value (one factor at a time, the others at the carrier's values), or the full grid with --grid.
           A manifest (<out>/manifest.json) keeps arm -> defines.
  --local  play every arm (the baseline first) against --opp with abyss_tools.run.arena --sandbox on this machine, paired
           seeds, into <out>/<arm>/.
  report   per arm against the baseline, paired by (map, side, repeat): wins and the win difference with a bootstrap 95%
           interval, the candidate queen's deaths by r100 / by r500, the share of fresh pearl spawns nearer to it in
           r0-199 (abyss_tools.kpi.spawn_share) and the lead index delta (abyss_tools.kpi.lead_curve --vs);
           --report-only re-reads <out>; --results-dir reads results gathered elsewhere (<dir>/<arm>_vs_<opp>/ with
           replays/ or summary.txt).

Why a renamed copy and not 50%: on a fixed set of paired seeds the seed set itself carries a bias (maps and sides), so even
an identical bot does not score exactly 50% there. Every arm is judged by its difference to the baseline on the same seeds.

  python -m abyss_tools.run.sweep --carrier abyss --opp starter --macro BC_MN_VALUE_W=0.15,0.5,1.0 \\
      --maps tower_defense weakhold --repeat 2 --prefix kw --out sweeps/kw --local -j 3
  python -m abyss_tools.run.sweep --carrier abyss --opp starter --prefix kw --out sweeps/kw --report-only
  abyss-sweep ...
"""
import argparse
import collections
import itertools
import json
import os
import random
import re
import shutil
import subprocess
import sys

from ..paths import bots_dir, purge_wasm, wasm_cache


def parse_macro(s):
    """'NAME=v1,v2' -> ('NAME', ['v1', 'v2'])"""
    name, _, vals = s.partition("=")
    if not name or not vals:
        raise ValueError(f"--macro NAME=v1,v2,...: {s!r}")
    return name, [v for v in vals.split(",") if v != ""]


def arms_of(macros, grid=False):
    """[{NAME: value, ...}] - one factor at a time (in the order given), or the cartesian grid"""
    if grid:
        names = [n for n, _ in macros]
        return [dict(zip(names, combo)) for combo in itertools.product(*[v for _, v in macros])]
    return [{n: v} for n, vals in macros for v in vals]


def define_lines(defs, tag):
    out = [f"// sweep arm {tag} (abyss_tools.run.sweep)"]
    for n, v in defs.items():
        out += [f"#undef {n}", f"#define {n} {v}"]
    return "\n".join(out) + "\n"


def build(carrier, arms, prefix, bots=None, cache=None):
    """the arm folders; returns [(name, defines)] with the baseline first"""
    bots = bots or bots_dir()
    src = os.path.join(bots, carrier)
    if not os.path.exists(os.path.join(src, "ablation.hpp")):
        raise SystemExit(f"{src}/ablation.hpp missing: the carrier must have the switch header ablation.hpp")
    out = [(f"{prefix}base_c", {})] + [(f"{prefix}{k}_c", d) for k, d in enumerate(arms)]
    for name, defs in out:
        dst = os.path.join(bots, name)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".unswbc-build"))
        if defs:
            with open(os.path.join(dst, "ablation.hpp"), "a") as f:
                f.write(define_lines(defs, name))
        purge_wasm(name, cache or wasm_cache())
    return out


GAME = re.compile(r"^(?P<map>.+?)__(?P<first>.+?)_vs_(?P<second>.+?)(?:_r(?P<rep>\d+))?\.replay(?:\.zst|\.gz)?$")


def game_key(fname, cand):
    """(map, side of cand, repeat) of an arena replay name, None if cand did not play"""
    m = GAME.match(fname)
    if not m:
        return None
    side = "A" if m["first"] == cand else "B" if m["second"] == cand else None
    return None if side is None else (m["map"], side, int(m["rep"] or 0))


def bootstrap(diffs, n=2000, seed=1):
    """95% interval of the sum of paired differences"""
    if not diffs:
        return (0.0, 0.0)
    rng = random.Random(seed)
    sums = sorted(sum(rng.choice(diffs) for _ in diffs) for _ in range(n))
    return (sums[int(0.025 * n)], sums[int(0.975 * n) - 1])


ROW = re.compile(r"^\s+(?P<map>\S+?)(?:\.map)? (?P<first>\S+) vs (?P<second>\S+): (?P<res>win|loss|draw|error)\b")


def outcomes_from_summary(path, cand):
    """{(map, side, rep): (score, None)} from an arena summary.txt: its game rows are in job order - repeat, map, the
    candidate as A then as B - and each outcome is the candidate's"""
    out, seen = {}, collections.Counter()
    with open(path) as fh:
        for line in fh:
            m = ROW.match(line)
            if not m or cand not in (m["first"], m["second"]):
                continue
            side = "A" if m["first"] == cand else "B"
            rep = seen[(m["map"], side)]
            seen[(m["map"], side)] += 1
            if m["res"] != "error":
                out[(m["map"], side, rep)] = ({"win": 1.0, "loss": 0.0, "draw": 0.5}[m["res"]], None)
    return out


def outcomes(folder, cand):
    """{(map, side, rep): (score 1 / 0 / .5, the candidate queen's death round or None)} from the arm's replays; without
    replays, from its summary.txt (no queen column then)"""
    rdir = os.path.join(folder, "replays")
    if not (os.path.isdir(rdir) and any(game_key(f, cand) for f in os.listdir(rdir))):
        sp = os.path.join(folder, "summary.txt")
        return outcomes_from_summary(sp, cand) if os.path.exists(sp) else {}
    from ..replay.io import initial_teams, load
    out = {}
    for f in sorted(os.listdir(rdir)):
        k = game_key(f, cand)
        if k is None:
            continue
        rep = load(os.path.join(rdir, f))
        me = k[1]
        res = rep.result
        score = 0.5 if res.which() == "noWinner" else (1.0 if str(res.winner).upper() == me else 0.0)
        teams = initial_teams(rep.map)        # the queens are ids 0 / 1, one per team, by the map's DRAGON lines
        qid = next((i for i in (0, 1) if teams.get(i) == me), 0 if me == "A" else 1)
        rnd, death = 0, None
        for e in rep.events:
            w = e.which()
            if w == "roundStart":
                rnd = e.roundStart.round
            elif w == "dragonDeath" and e.dragonDeath.id == qid and death is None:
                death = rnd
        out[k] = (score, death)
    return out


def pair_rows(base, arm):
    """the paired summary of an arm against the baseline over their common games"""
    keys = sorted(set(base) & set(arm))
    diffs = [arm[k][0] - base[k][0] for k in keys]
    lo, hi = bootstrap(diffs)
    q = lambda d, r: sum(1 for k in keys if d[k][1] is not None and d[k][1] <= r)  # noqa: E731 (0 without replays)
    return {"games": len(keys), "wins": sum(arm[k][0] for k in keys), "base_wins": sum(base[k][0] for k in keys),
            "diff": sum(diffs), "ci": (lo, hi), "q100": q(arm, 100), "base_q100": q(base, 100), "q500": q(arm, 500),
            "base_q500": q(base, 500)}


def side_metrics(arm_dir_, base_dir, arm, base, jobs=4):
    """(spawn share r0-199 arm / base, lead index delta) from the KPI tools; '-' where a tool fails"""
    share, lead = "-", "-"
    try:
        from ..kpi import spawn_share
        rows = spawn_share.collect([os.path.join(arm_dir_, "replays"), os.path.join(base_dir, "replays")], jobs)
        tot = collections.defaultdict(lambda: [0, 0])
        for _, bot, per in rows:
            tot[bot][0] += per["r0-199"][0]
            tot[bot][1] += per["r0-199"][1]

        def s(name, folder):
            c = tot.get(f"{name} [{os.path.basename(os.path.normpath(folder))}]")
            return f"{c[0] / c[1]:.3f}" if c and c[1] else "-"
        share = f"{s(arm, arm_dir_)} / {s(base, base_dir)}"
    except Exception:  # noqa: BLE001 - a report column, not a gate
        pass
    try:
        cmd = [sys.executable, "-m", "abyss_tools.kpi.lead_curve", os.path.join(arm_dir_, "replays"), "--side", arm,
               "--vs", os.path.join(base_dir, "replays"), "--vs-side", base]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=3600).stdout
        m = re.search(r"lead index delta: \*\*([^*]+)\*\*", out)
        if m:
            lead = m[1]
    except Exception:  # noqa: BLE001
        pass
    return share, lead


def arm_dir(out_dir, name, manifest, results_dir=None):
    """an arm's results: <out>/<arm>, or <results_dir>/<arm>_vs_<opp> for results gathered elsewhere"""
    return os.path.join(results_dir, f"{name}_vs_{manifest['opp']}") if results_dir else os.path.join(out_dir, name)


def report(out_dir, manifest, extra=True, results_dir=None):
    arms = manifest["arms"]
    base_name = arms[0][0]
    base = outcomes(arm_dir(out_dir, base_name, manifest, results_dir), base_name)
    lines = [f"## sweep {manifest['prefix']} on {manifest['carrier']} vs {manifest['opp']}",
             "", f"maps {' '.join(manifest['maps']) or '(all)'}, repeat {manifest['repeat']}; baseline {base_name} "
             f"({len(base)} games)", "",
             "| arm | defines | games | wins | baseline | diff [95%] | queen dead by r100 | by r500 | "
             "spawns nearer r0-199 (arm / base) | lead index delta |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for name, defs in arms[1:]:
        d = outcomes(arm_dir(out_dir, name, manifest, results_dir), name)
        r = pair_rows(base, d)
        share, lead = (side_metrics(arm_dir(out_dir, name, manifest, results_dir),
                                    arm_dir(out_dir, base_name, manifest, results_dir), name, base_name)
                       if extra else ("-", "-"))
        lines.append(f"| {name} | {' '.join(f'{k}={v}' for k, v in defs.items())} | {r['games']} | {r['wins']:g} | "
                     f"{r['base_wins']:g} | {r['diff']:+g} [{r['ci'][0]:+g}, {r['ci'][1]:+g}] | {r['q100']} / {r['base_q100']} | "
                     f"{r['q500']} / {r['base_q500']} | {share} | {lead} |")
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--carrier", required=True, help="the bot folder the arms are built from")
    ap.add_argument("--opp", required=True, help="the opponent bot folder")
    ap.add_argument("--macro", action="append", default=[], help="NAME=v1,v2,... (repeatable)")
    ap.add_argument("--grid", action="store_true", help="the cartesian grid of the macros' values, not one factor at a time")
    ap.add_argument("--maps", nargs="*", default=[], help="map file stems (default: every map of the maps folder)")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--prefix", required=True, help="arm names: <prefix><k>_c, the baseline <prefix>base_c")
    ap.add_argument("--out", required=True)
    ap.add_argument("--bots", help="the unswbc workspace with the bot folders (default: <repo>/bots or $ABYSS_BOTS)")
    ap.add_argument("--maps-dir", help="the folder of .map files (default: <bots>/maps)")
    ap.add_argument("--local", action="store_true", help="play the arms on this machine")
    ap.add_argument("-j", "--jobs", type=int, default=3)
    ap.add_argument("--seed-base", type=int, help="the arena's seed base (a new set of paired games)")
    ap.add_argument("--fork", action="store_true", help="both sides of a (map, repeat) share one seed")
    ap.add_argument("--no-sandbox", action="store_true", help="native games (faster; the bots' random numbers unpaired)")
    ap.add_argument("--report-only", action="store_true")
    ap.add_argument("--no-extra", action="store_true", help="skip the spawn-share and lead-curve columns")
    ap.add_argument("--results-dir", help="with --report-only: results gathered elsewhere, <dir>/<arm>_vs_<opp>/")
    a = ap.parse_args(argv)
    out_dir = os.path.abspath(os.path.expanduser(a.out))
    mpath = os.path.join(out_dir, "manifest.json")
    if a.report_only:
        with open(mpath) as fh:
            manifest = json.load(fh)
        print("\n".join(report(out_dir, manifest, not a.no_extra and not a.results_dir, a.results_dir)))
        return 0
    if not a.macro:
        raise SystemExit("--macro NAME=v1,v2,... is required")
    bots = bots_dir(a.bots)
    arms = build(a.carrier, arms_of([parse_macro(s) for s in a.macro], a.grid), a.prefix, bots=bots)
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"carrier": a.carrier, "opp": a.opp, "prefix": a.prefix, "maps": a.maps, "repeat": a.repeat, "arms": arms}
    with open(mpath, "w") as fh:
        json.dump(manifest, fh, indent=1)
    print("arms: " + ", ".join(f"{n} {d or '(baseline)'}" for n, d in arms))
    if not a.local:
        print("built; play them with --local, or elsewhere and report with --report-only --results-dir DIR")
        return 0
    from . import arena
    for name, _ in arms:
        argv_ = [name, a.opp, "--bots", bots, "--repeat", str(a.repeat), "-j", str(a.jobs), "-o", os.path.join(out_dir, name)]
        argv_ += [] if a.no_sandbox else ["--sandbox"]
        argv_ += ["--maps", *a.maps] if a.maps else []
        argv_ += ["--maps-dir", a.maps_dir] if a.maps_dir else []
        argv_ += ["--seed-base", str(a.seed_base)] if a.seed_base is not None else []
        argv_ += ["--fork"] if a.fork else []
        print("arena " + " ".join(argv_), flush=True)
        arena.main(argv_)
    print("\n".join(report(out_dir, manifest, not a.no_extra)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
