"""Play bot A against bot B on every map, both sides, in parallel, with paired seeds, and tabulate.

  python -m abyss_tools.run.arena abyss starter                    # every map in <bots>/maps
  python -m abyss_tools.run.arena abyss starter -j 8 --sandbox     # the judge's CPU metering
  python -m abyss_tools.run.arena cand base --maps arena default --repeat 2 -o runs/cand_vs_base
  abyss-arena ...

Each game runs in its own temporary copy of the two bot folders (named <bot>_L and <bot>_R: one folder name twice would be
one project), so parallel builds never collide. Replays go to <out>/replays as MAP__A_vs_B[_rN].replay, a summary table to
stdout and <out>/summary.txt. The default <out> is <bots>/arena-out/<A>_vs_<B>.

Paired seeds (unswbc >= 1.2.2, `unswbc run --seed`): the seed of a game is a hash of its slot - (map file, which side the
first bot plays, repeat) - plus --seed-base, so the pearl respawns and the bots' random numbers of a slot are the same for
every candidate played against the same opponent: two candidates' results differ by their play, not by the draw. With
--fork both sides of a (map, repeat) share one seed. --random-seeds turns pairing off. Without --sandbox the bots run natively
and the seed fixes the pearls but not the bots' own random numbers.
"""
import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zlib

from ..paths import bots_dir, maps_dir, purge_wasm, unswbc

RESULT = re.compile(r"team ([AB]) wins after (\d+) rounds|draw after (\d+) rounds|(\d+) rounds.*draw", re.I)
SEED_BASE = 1000003        # any fixed value works; changing it draws a new set of paired games
GAME_TIMEOUT = 5400        # seconds per game: sandbox games of two heavy bots with full armies can take a long time

# a job that builds a bot folder removes and remakes its .unswbc-build while another copies it: never copy build folders
NO_BUILD = shutil.ignore_patterns(".unswbc-build")


def seed_of(game_map, side, rep, base=SEED_BASE):
    """the paired seed of a slot: side is 'cand-A' / 'cand-B' (or '' with --fork)"""
    slot = f"{os.path.basename(game_map)}|{side}|{rep}"
    return (zlib.crc32(slot.encode()) + base) & 0xFFFFFFFFFFFF


def replay_name(game_map, first, second, rep):
    return f"{os.path.splitext(os.path.basename(game_map))[0]}__{first}_vs_{second}{'_r%d' % rep if rep else ''}.replay"


def parse_result(text, first, second, a):
    """(outcome for bot a: win / loss / draw / error, rounds, why) from `unswbc run`'s output"""
    m = RESULT.search(text)
    if not m:
        return "error", 0, ""
    if m.group(1):
        winner = first if m.group(1) == "A" else second
        outcome = "win" if winner == a else "loss"
        rounds = int(m.group(2))
    else:
        outcome, rounds = "draw", int(m.group(3) or m.group(4))
    # a full game prints why it was won: "(longer queen, 12 to 5)", "(longest dragon, ...)", "(total length, ...)"
    line = text[m.start():].split("\n", 1)[0]
    why = re.search(r"\(([^)]*)\)", line)
    return outcome, rounds, why.group(1) if why else ""


def warm(bots, name, game_map, exe):
    """build once so every worker copies a ready build"""
    subprocess.run([exe, "run", game_map, name, name, "--no-replay"], cwd=bots, capture_output=True, text=True, timeout=900)


def play(job):
    bots, exe, game_map, first, second, a, out_dir, sandbox, rep, seed_mode, seed_base, keep_logs = job
    work = tempfile.mkdtemp(prefix="arena-")
    try:
        left, right = f"{first}_L", f"{second}_R"
        shutil.copytree(os.path.join(bots, first), os.path.join(work, left), ignore=NO_BUILD)
        shutil.copytree(os.path.join(bots, second), os.path.join(work, right), ignore=NO_BUILD)
        replay = os.path.join(out_dir, "replays", replay_name(game_map, first, second, rep))
        cmd = [exe, "run", game_map, left, right, "-o", replay]
        if not keep_logs:
            cmd += ["--no-logs", "--no-draw", "--no-indicator"]
        if sandbox:
            cmd.append("--sandbox")
        if seed_mode != "random":
            side = "" if seed_mode == "fork" else ("cand-A" if first == a else "cand-B")
            cmd += ["--seed", str(seed_of(game_map, side, rep, seed_base))]
        t0 = time.time()
        try:
            p = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=GAME_TIMEOUT)
        except subprocess.TimeoutExpired:
            # one over-long game must not lose the summary of the finished ones
            return (game_map, first, second, "error", 0, time.time() - t0, f"timed out after {GAME_TIMEOUT}s", "")
        text = p.stdout + p.stderr
        outcome, rounds, why = parse_result(text, first, second, a)
        err = text[-400:] if outcome == "error" else ""
        return (game_map, first, second, outcome, rounds, time.time() - t0, err, why)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", help="the candidate bot folder")
    ap.add_argument("b", help="the opponent bot folder")
    ap.add_argument("--bots", help="the unswbc workspace with the bot folders (default: <repo>/bots or $ABYSS_BOTS)")
    ap.add_argument("--maps-dir", help="the folder of .map files (default: <bots>/maps or $ABYSS_MAPS)")
    ap.add_argument("--maps", nargs="*", help="map names (file stems, may include a sub-folder: held/h900000); default: every map")
    ap.add_argument("--map-dir", help="play the maps of this sub-folder of the maps folder")
    ap.add_argument("--map-count", type=int, default=20, help="with --map-dir: how many maps")
    ap.add_argument("--map-skip", type=int, default=0, help="with --map-dir: skip the first N maps")
    ap.add_argument("--repeat", type=int, default=1, help="play every (map, side) this many times (a new seed each)")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("-o", "--out", help="output folder (default: <bots>/arena-out/<A>_vs_<B>)")
    ap.add_argument("--sandbox", action="store_true", help="run as the judge does (wasm, CPU metering)")
    ap.add_argument("--seed-base", type=int, default=int(os.environ.get("ARENA_SEED_BASE", SEED_BASE)))
    ap.add_argument("--fork", action="store_true", help="both sides of a (map, repeat) share one seed")
    ap.add_argument("--random-seeds", action="store_true", help="no paired seeds")
    ap.add_argument("--logs", action="store_true", help="keep the bots' LOG / DOT / LINE output in the replays")
    ap.add_argument("--unswbc", help="the unswbc executable")
    args = ap.parse_args(argv)
    if args.a == args.b:
        sys.exit("pick two different bots (copy one if you want a mirror match)")
    bots = bots_dir(args.bots)
    mdir = maps_dir(args.maps_dir, bots)
    exe = unswbc(args.unswbc)
    names = args.maps or sorted(os.path.splitext(f)[0] for f in os.listdir(mdir) if f.endswith(".map"))
    if args.map_dir:
        sub = os.path.join(mdir, args.map_dir)
        names = [f"{args.map_dir}/{f[:-4]}" for f in sorted(os.listdir(sub)) if f.endswith(".map")]
        names = names[args.map_skip:args.map_skip + args.map_count]
    maps = [os.path.join(mdir, n + ".map") for n in names]
    if not maps:
        sys.exit(f"no maps in {mdir}")
    # absolute: every game runs `unswbc run ... -o REPLAY` inside its own temporary folder
    out = os.path.abspath(os.path.expanduser(args.out)) if args.out else os.path.join(bots, "arena-out", f"{args.a}_vs_{args.b}")
    os.makedirs(os.path.join(out, "replays"), exist_ok=True)
    seed_mode = "random" if args.random_seeds else "fork" if args.fork else "paired"

    for bot in (args.a, args.b):
        purge_wasm(bot)
        warm(bots, bot, maps[0], exe)
    jobs = []
    for rep in range(args.repeat):
        for m in maps:
            jobs.append((bots, exe, m, args.a, args.b, args.a, out, args.sandbox, rep, seed_mode, args.seed_base, args.logs))
            jobs.append((bots, exe, m, args.b, args.a, args.a, out, args.sandbox, rep, seed_mode, args.seed_base, args.logs))

    rows = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(play, jobs):
            rows.append(r)
            gm, first, second, outcome, rounds, secs, err, why = r
            print(f"{os.path.basename(gm):<24} {first:>10} vs {second:<10} {outcome:<5} r{rounds:<4} {secs:5.1f}s"
                  + (f"  ({why})" if why else ""), flush=True)
            if err:
                print("   " + err.replace("\n", "\n   "))

    wins = sum(r[3] == "win" for r in rows)
    losses = sum(r[3] == "loss" for r in rows)
    draws = sum(r[3] == "draw" for r in rows)
    errors = sum(r[3] == "error" for r in rows)
    summary = f"{args.a} vs {args.b}: {wins}W {losses}L {draws}D {errors}E over {len(rows)} games"
    print(summary)
    with open(os.path.join(out, "summary.txt"), "a") as f:
        f.write(summary + "\n")
        for r in rows:
            f.write(f"  {os.path.basename(r[0])} {r[1]} vs {r[2]}: {r[3]} r{r[4]}" + (f" ({r[7]})" if r[7] else "") + "\n")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
