#!/usr/bin/env python3
"""Compile-time macro arms of a carrier bot, a same-seed baseline, and their paired report.

  build   bots/<prefix><k>_c per arm = the carrier + `#undef NAME` / `#define NAME VALUE` lines appended to its
          ablation.hpp, and bots/<prefix>base_c = the carrier unchanged under another name (the same-seed baseline).
          Every arm's cached sandbox builds are cleared (unswbc's wasm cache hashes only .c / .cpp files, so a header-only
          change would otherwise reuse the old build). One arm per macro value (one factor at a time, the other macros at
          the carrier's values), or the full grid with --grid. <out>/manifest.json keeps arm -> defines.
  local   play every arm (baseline first) against --opp with infra/lab/arena.py on this machine, into <out>/<arm>/.
  lab     print (or with --submit run) the `lab.py submit` line for the arms.
  report  per arm against the baseline, paired by (map, side, repeat): wins, the win difference and a bootstrap 95%
          interval of the summed paired differences.

  python3 infra/sweep/arms.py build --carrier abyss --macro BC_R4_QRING=1,3 --prefix qr --opp starter --out sweeps/qr
  python3 infra/sweep/arms.py local --out sweeps/qr --maps default arena --repeat 2 -j 4
  python3 infra/sweep/arms.py lab --out sweeps/qr --tag qr1 --set quick --seed-base 7 [--submit]
  python3 infra/sweep/arms.py report --out sweeps/qr [--lab-dir data/lab/qr1]

Why a renamed copy and not 50%: on a fixed set of paired seeds the seed set itself carries a bias (map and side
draws), so even an identical bot does not score exactly 50% there. Every arm is judged by its difference to the
baseline on the same seeds.
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

HERE = os.path.dirname(os.path.abspath(__file__))
INFRA = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(INFRA, "lab"))
import labconf  # noqa: E402

labconf.load_env()
LAB = os.path.join(INFRA, "lab", "lab.py")
ARENA = os.path.join(INFRA, "lab", "arena.py")


def parse_macro(s):
    """'NAME=v1,v2' -> ('NAME', ['v1', 'v2'])"""
    name, _, vals = s.partition("=")
    if not name or not vals:
        raise ValueError(f"--macro NAME=v1,v2,...: {s!r}")
    return name, [v for v in vals.split(",") if v != ""]


def arms_of(macros, grid=False):
    """[{NAME: value, ...}]: one factor at a time (in the order given), or the cartesian grid."""
    if grid:
        names = [n for n, _ in macros]
        return [dict(zip(names, combo)) for combo in itertools.product(*[v for _, v in macros])]
    return [{n: v} for n, vals in macros for v in vals]


def spec_items(spec):
    """[(key, defines)] from a sweep spec's "arms": a one-factor entry [MACRO, [v1, v2]] gives one arm per value; a
    combination entry {"defs": {MACRO: v, ...}} gives one arm with all of them."""
    out = []
    for e in spec:
        if isinstance(e, dict):
            d = {k: str(v) for k, v in e["defs"].items()}
            out.append(("+".join(f"{k}={v}" for k, v in sorted(d.items())), d))
        else:
            m, vals = e
            out += [(f"{m}={v}", {m: str(v)}) for v in vals]
    return out


def define_lines(defs, tag):
    out = [f"// sweep arm {tag} (infra/sweep)"]
    for n, v in defs.items():
        out += [f"#undef {n}", f"#define {n} {v}"]
    return "\n".join(out) + "\n"


def purge_wasm(name):
    home = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    cache = os.path.join(home, "unswbc", "wasmbots")
    if not os.path.isdir(cache):
        return 0
    n = 0
    for f in os.listdir(cache):
        if f.split("-")[0] in (name, name + "_L", name + "_R"):
            try:
                os.remove(os.path.join(cache, f))
                n += 1
            except FileNotFoundError:
                pass
    return n


def build(carrier, arms, prefix, bots=None, baseline=True, names=None):
    """The arm folders; returns [(name, defines)] with the baseline ({} defines) first when `baseline`.
    names: explicit arm names (default <prefix><k>_c)."""
    bots = bots or labconf.bots_dir()
    src = os.path.join(bots, carrier)
    if not os.path.exists(os.path.join(src, "ablation.hpp")):
        raise SystemExit(f"{src}/ablation.hpp missing: the carrier must have an ablation.hpp included by its code")
    names = names or [f"{prefix}{k}_c" for k in range(len(arms))]
    out = ([(f"{prefix}base_c", {})] if baseline else []) + list(zip(names, arms))
    for name, defs in out:
        dst = os.path.join(bots, name)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".unswbc-build"))
        if defs:
            with open(os.path.join(dst, "ablation.hpp"), "a") as f:
                f.write(define_lines(defs, name))
        purge_wasm(name)
    return out


# ---------------------------------------------------------------- paired report
def bootstrap(diffs, n=2000, seed=1):
    """95% interval of the sum of paired differences."""
    if not diffs:
        return (0.0, 0.0)
    rng = random.Random(seed)
    sums = sorted(sum(rng.choice(diffs) for _ in diffs) for _ in range(n))
    return (sums[int(0.025 * n)], sums[int(0.975 * n) - 1])


ROW = re.compile(r"^\s+(?P<map>\S+?)(?:\.map)? (?P<first>\S+) vs (?P<second>\S+): (?P<res>win|loss|draw|error)\b")


def outcomes_from_summary(path, cand):
    """{(map, side, rep): score} from an arena summary.txt. Its game rows are in job order (repeat, map, the candidate
    as A then as B) and each outcome is the candidate's, so the k-th row of a (map, side) is repeat k."""
    out, seen = {}, collections.Counter()
    with open(path) as f:
        for line in f:
            m = ROW.match(line)
            if not m or cand not in (m["first"], m["second"]):
                continue
            side = "A" if m["first"] == cand else "B"
            rep = seen[(m["map"], side)]
            seen[(m["map"], side)] += 1
            if m["res"] != "error":
                out[(m["map"], side, rep)] = {"win": 1.0, "loss": 0.0, "draw": 0.5}[m["res"]]
    return out


def outcomes(folder, cand):
    for sp in (os.path.join(folder, "summary.txt"), os.path.join(folder, "orig", "summary.txt")):
        if os.path.exists(sp):
            return outcomes_from_summary(sp, cand)
    return {}


def pair_rows(base, arm):
    """The paired summary of an arm against the baseline over their common games."""
    keys = sorted(set(base) & set(arm))
    diffs = [arm[k] - base[k] for k in keys]
    lo, hi = bootstrap(diffs)
    return {"games": len(keys), "wins": sum(arm[k] for k in keys), "base_wins": sum(base[k] for k in keys),
            "diff": sum(diffs), "changed": sum(1 for d in diffs if d), "ci": (lo, hi)}


def arm_dir(out_dir, name, manifest, lab_dir=None):
    """An arm's results: <out>/<arm>, or lab.py's <lab_dir>/<arm>_vs_<opp>."""
    return os.path.join(lab_dir, f"{name}_vs_{manifest['opp']}") if lab_dir else os.path.join(out_dir, name)


def report(out_dir, manifest, lab_dir=None):
    arms = manifest["arms"]
    base_name = arms[0][0]
    base = outcomes(arm_dir(out_dir, base_name, manifest, lab_dir), base_name)
    lines = [f"## sweep {manifest['prefix']} on {manifest['carrier']} vs {manifest['opp']}", "",
             f"baseline {base_name} ({len(base)} games)", "",
             "| arm | defines | games | wins | baseline | diff [95%] | games changed |", "|---|---|---|---|---|---|---|"]
    for name, defs in arms[1:]:
        r = pair_rows(base, outcomes(arm_dir(out_dir, name, manifest, lab_dir), name))
        lines.append(f"| {name} | {' '.join(f'{k}={v}' for k, v in defs.items())} | {r['games']} | {r['wins']:g} | "
                     f"{r['base_wins']:g} | {r['diff']:+g} [{r['ci'][0]:+g}, {r['ci'][1]:+g}] | {r['changed']} |")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build")
    p.add_argument("--carrier", required=True, help="bot folder under bots/ the arms are built from")
    p.add_argument("--opp", required=True, help="opponent bot folder")
    p.add_argument("--macro", action="append", default=[], help="NAME=v1,v2,... (repeatable)")
    p.add_argument("--grid", action="store_true", help="the cartesian grid of the macros instead of one factor at a time")
    p.add_argument("--prefix", required=True, help="arm names: <prefix><k>_c, the baseline <prefix>base_c")
    p.add_argument("--out", required=True)
    p = sub.add_parser("local")
    p.add_argument("--out", required=True)
    p.add_argument("--maps", nargs="*", default=[])
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("-j", type=int, default=3)
    p.add_argument("--seed-base", type=int)
    p.add_argument("--fork", action="store_true")
    p.add_argument("--no-sandbox", action="store_true", help="native games (fast, but not judge-exact)")
    p = sub.add_parser("lab")
    p.add_argument("--out", required=True)
    p.add_argument("--tag", required=True)
    p.add_argument("--set", default=None)
    p.add_argument("--maps", nargs="*", default=[])
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--seed-base", type=int)
    p.add_argument("--fork", action="store_true")
    p.add_argument("--submit", action="store_true", help="run the line instead of printing it")
    p = sub.add_parser("report")
    p.add_argument("--out", required=True)
    p.add_argument("--lab-dir", help="lab.py's results folder of the tag ($LAB_DATA/TAG)")
    a = ap.parse_args()
    out_dir = os.path.abspath(os.path.expanduser(a.out))
    mpath = os.path.join(out_dir, "manifest.json")
    if a.cmd == "build":
        if not a.macro:
            raise SystemExit("--macro NAME=v1,v2,... is required")
        arms = build(a.carrier, arms_of([parse_macro(s) for s in a.macro], a.grid), a.prefix)
        os.makedirs(out_dir, exist_ok=True)
        manifest = {"carrier": a.carrier, "opp": a.opp, "prefix": a.prefix, "arms": arms}
        with open(mpath, "w") as f:
            json.dump(manifest, f, indent=1)
        print("arms: " + ", ".join(f"{n} {d or '(baseline)'}" for n, d in arms))
        return
    with open(mpath) as f:
        manifest = json.load(f)
    names = [n for n, _ in manifest["arms"]]
    if a.cmd == "report":
        print("\n".join(report(out_dir, manifest, a.lab_dir)))
    elif a.cmd == "lab":
        cmd = [sys.executable, LAB, "submit", a.tag, " ".join(names), manifest["opp"], "--repeat", str(a.repeat), "--pv", "0"]
        cmd += ["--maps", *a.maps] if a.maps else (["--set", a.set] if a.set else [])
        cmd += ["--seed-base", str(a.seed_base)] if a.seed_base is not None else []
        cmd += ["--fork"] if a.fork else []
        if a.submit:
            subprocess.run(cmd, check=True)
        else:
            print(" ".join(f'"{c}"' if " " in c else c for c in cmd))
    elif a.cmd == "local":
        for name in names:
            cmd = [sys.executable, ARENA, name, manifest["opp"], "--repeat", str(a.repeat), "-j", str(a.j),
                   "-o", os.path.join(out_dir, name)] + ([] if a.no_sandbox else ["--sandbox"])
            cmd += ["--maps", *a.maps] if a.maps else []
            cmd += ["--seed-base", str(a.seed_base)] if a.seed_base is not None else []
            cmd += ["--fork"] if a.fork else []
            print(" ".join(cmd), flush=True)
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        print("\n".join(report(out_dir, manifest)))


if __name__ == "__main__":
    main()
