#!/usr/bin/env python3
"""Self-play data: games of one bot against a set of opponents (itself included), then an index of that bot's game
sides for the record builders (rl/data/build_*.py).

  python3 rl/selfplay/selfplay.py --bot BOT --opps BOT starter --maps default arena --repeat 2 -j 8 --out RUN_DIR
  python3 rl/data/build_f1.py --data RUN_DIR --featurize BIN/featurize_f1 --shard 0 --nshards 1 --out RUN_DIR/f1_0.bin

Games run through abyss_tools.run.arena (tools/abyss_tools: every game in its own copy of the two bot folders, both
sides of every map, in parallel). An opponent equal to --bot plays a renamed twin copy (BOT_twin); both of its sides are
the bot's own decisions and both go to the index. Seeds are random by default (data, not a paired comparison);
--seed-base S gives reproducible games. --sandbox runs the judge's WASM sandbox (slower; the CPU metering then matches
the judge), the default is native builds.

Writes RUN_DIR/<opponent>/replays/*.replay, RUN_DIR/<opponent>/summary.txt and RUN_DIR/index.jsonl (rows: game, path,
team, side, won, rank 0, wclass 0, map) with the sides played by --bot (and its twin). Environment: ABYSS_BOTS (the
unswbc workspace, default this checkout's bots/), UNSWBC (the unswbc executable), NCPUS (default -j).
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "rl", "data"))
from replay_src import load, map_name, winner  # noqa: E402


def base_name(name):
    return name[:-2] if name.endswith(("_L", "_R")) else name


def index(run_dir, names):
    rows, bad = [], 0
    paths = sorted(glob.glob(os.path.join(run_dir, "*", "replays", "*.replay*")))
    for p in paths:
        try:
            rep = load(p)
            win = winner(rep)
        except Exception:  # noqa: BLE001  (a killed game's partial file)
            bad += 1
            continue
        game = os.path.relpath(p, run_dir).split(".replay")[0].replace(os.sep, "__")
        for side, bot in zip("ab", (rep.botA, rep.botB)):
            if base_name(bot) in names:
                rows.append({"game": game, "path": os.path.abspath(p), "team": base_name(bot), "side": side,
                             "won": win == side, "rank": 0, "wclass": 0, "map": map_name(rep.map)})
    with open(os.path.join(run_dir, "index.jsonl"), "w") as f:
        f.writelines(json.dumps(r) + "\n" for r in rows)
    print(f"{len(rows)} game sides of {sorted(names)} from {len(paths) - bad} replays ({bad} unreadable), "
          f"won {sum(r['won'] for r in rows)} -> {os.path.join(run_dir, 'index.jsonl')}")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bot", required=True, help="the bot whose decisions become data")
    ap.add_argument("--opps", nargs="+", required=True, help="opponent bot folders (the bot itself: a twin copy)")
    ap.add_argument("--maps", nargs="*", default=None, help="map names (default: every map of the workspace)")
    ap.add_argument("--repeat", type=int, default=1, help="games per (map, side)")
    ap.add_argument("-j", "--jobs", type=int, default=int(os.environ.get("NCPUS", max(1, (os.cpu_count() or 2) // 2))))
    ap.add_argument("--sandbox", action="store_true")
    ap.add_argument("--seed-base", type=int, default=None, help="reproducible seeds (default: random seeds)")
    ap.add_argument("--logs", action="store_true", help="keep the bots' LOG lines in the replays (trace builds)")
    ap.add_argument("--bots", default=os.environ.get("ABYSS_BOTS") or os.path.join(ROOT, "bots"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--index-only", action="store_true", help="only (re)write RUN_DIR/index.jsonl")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    names = {a.bot, a.bot + "_twin"}
    if a.index_only:
        index(out, names)
        return 0
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.join(ROOT, "tools") + os.pathsep + env.get("PYTHONPATH", "")
    failed = 0
    for opp in a.opps:
        if opp == a.bot:  # the arena needs two folder names: play a twin copy
            opp = a.bot + "_twin"
            twin = os.path.join(a.bots, opp)
            shutil.rmtree(twin, ignore_errors=True)
            shutil.copytree(os.path.join(a.bots, a.bot), twin, ignore=shutil.ignore_patterns(".unswbc-build"))
        cmd = [sys.executable, "-m", "abyss_tools.run.arena", a.bot, opp, "--bots", a.bots, "--repeat", str(a.repeat),
               "-j", str(a.jobs), "-o", os.path.join(out, opp)]
        if a.maps:
            cmd += ["--maps"] + a.maps
        if a.sandbox:
            cmd.append("--sandbox")
        if a.logs:
            cmd.append("--logs")
        cmd += ["--seed-base", str(a.seed_base)] if a.seed_base is not None else ["--random-seeds"]
        print("+", " ".join(cmd), flush=True)
        failed += subprocess.run(cmd, env=env).returncode != 0
    index(out, names)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
