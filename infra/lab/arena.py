#!/usr/bin/env python3
"""Play bot A against bot B on a set of maps, both sides, in parallel, with paired seeds, and tabulate.

  python3 infra/lab/arena.py abyss starter                        # every map in bots/maps
  python3 infra/lab/arena.py abyss starter -j 8 --sandbox         # judge-exact CPU metering
  python3 infra/lab/arena.py abyss starter --maps default arena --repeat 2 -o out/run1
  python3 infra/lab/arena.py abyss starter --map-dir pv --map-count 20   # generated variants in bots/maps/pv

Each game runs in its own copy of the two bot folders (as <A>_L / <B>_R), so parallel builds never collide.
Outputs in --out:
  replays/<map>__<first>_vs_<second>[_rN].replay
  summary.txt   "<A> vs <B>: xW yL zD eE over n games" plus one row per game, outcomes from A's side
  cpu.txt       per game: A's and B's max points per turn (cand_max / opp_max, sandbox only) and the turns lost to
                the CPU limit or the wall clock (tle = A's, opp_tle = B's)

Paired seeds (unswbc >= 1.2.2 accepts --seed): the seed of a game is a hash of its slot (map, which side A plays,
repeat) plus a seed base, so every candidate played against the same opponent meets the same pearl respawns and the
same random numbers in the same slot. Two candidates' results then differ by their play, not by the draw. With
--fork both sides of a (map, repeat) share one seed. Environment equivalents: ARENA_SEED_BASE, ARENA_FORK=1,
ARENA_SEEDED=0 (random seeds), BC_BOTS_DIR, UNSWBC.
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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RESULT = re.compile(r"team ([AB]) wins after (\d+) rounds|draw after (\d+) rounds|(\d+) rounds.*draw", re.I)
CPU = re.compile(r"team ([AB]) points per turn:.*?max ([0-9.,]+)(M?)", re.I)
FAILURE = re.compile(r"\(team ([AB])\)\s+(?:exceeded CPU limit|ran out of time)", re.I)
DEFAULT_SEED_BASE = 1000003
GAME_TIMEOUT = 5400  # seconds; sandbox games of two heavy bots with many dragons can take most of an hour on a busy node

# Build folders stay behind: a job building one bot folder removes and remakes its .unswbc-build while another job
# copies it.
NO_BUILD = shutil.ignore_patterns(".unswbc-build")


def unswbc():
    return os.environ.get("UNSWBC") or shutil.which("unswbc") or os.path.expanduser("~/.local/bin/unswbc")


def wasm_cache():
    home = os.environ.get("XDG_CACHE_HOME") or os.environ.get("LOCALAPPDATA")
    return os.path.join(home or os.path.expanduser("~/.cache"), "unswbc", "wasmbots")


def purge_wasm(name):
    """unswbc's sandbox build cache is keyed on the .c/.cc/.cpp files only, not on headers, so after a header edit it
    silently reuses the old wasm. Drop every cached build of this bot (and of the _L / _R copies made here)."""
    cache = wasm_cache()
    if not os.path.isdir(cache):
        return
    for f in os.listdir(cache):
        if f.split("-")[0] in (name, name + "_L", name + "_R"):
            try:
                os.remove(os.path.join(cache, f))
            except FileNotFoundError:  # another process purged it first
                pass


def warm(bots, name):
    """Build once natively so every worker copies a ready .unswbc-build folder."""
    maps = os.path.join(bots, "maps")
    names = sorted(f for f in os.listdir(maps) if f.endswith(".map"))
    first = next((f for f in ("arena.map", "default_small.map") if f in names), names[0] if names else None)
    if first:
        first = os.path.join("maps", first)
        subprocess.run([unswbc(), "run", first, name, name, "--no-replay"], cwd=bots, capture_output=True, text=True,
                       timeout=900)


def seed_of(game_map, cand_side, rep, seed_base, fork):
    """The paired seed of one slot (48 bits)."""
    side = "" if fork else cand_side
    slot = f"{os.path.basename(game_map)}|{side}|{rep}"
    return (zlib.crc32(slot.encode()) + seed_base) & 0xFFFFFFFFFFFF


def parse_points(value, mega):
    v = float(value.replace(",", ""))
    return v if mega else v / 1e6


def parse_output(text):
    """(outcome match, {team: max points per turn in M}, {team: CPU / wall-clock failures}) from unswbc's output."""
    m = RESULT.search(text)
    cpu = {t: parse_points(v, mg) for t, v, mg in CPU.findall(text)}
    fails = {"A": 0, "B": 0}
    for t in FAILURE.findall(text):
        fails[t.upper()] += 1
    return m, cpu, fails


def play(job):
    bots, game_map, first, second, a, out_dir, opts, rep = job
    work = tempfile.mkdtemp(prefix="arena-")
    try:
        # the same folder name twice would be one project: copy under side-specific names
        left, right = f"{first}_L", f"{second}_R"
        shutil.copytree(os.path.join(bots, first), os.path.join(work, left), ignore=NO_BUILD)
        shutil.copytree(os.path.join(bots, second), os.path.join(work, right), ignore=NO_BUILD)
        replay = os.path.join(out_dir, "replays", f"{os.path.splitext(os.path.basename(game_map))[0]}__{first}_vs_{second}"
                              f"{'_r%d' % rep if rep else ''}.replay")
        cmd = [unswbc(), "run", game_map, left, right, "--no-logs", "--no-draw", "--no-indicator"]
        cmd += ["--no-replay"] if opts.no_replays else ["-o", replay]
        if opts.sandbox:
            cmd.append("--sandbox")
        if opts.seeded:
            cmd += ["--seed", str(seed_of(game_map, "cand-A" if first == a else "cand-B", rep, opts.seed_base, opts.fork))]
        t0 = time.time()
        try:
            p = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=opts.timeout)
        except subprocess.TimeoutExpired:
            # one over-long game must not lose the summary of the finished ones
            return (game_map, first, second, "error", 0, time.time() - t0, f"timed out after {opts.timeout}s", "", {},
                    {"A": 0, "B": 0})
        text = p.stdout + p.stderr
        m, cpu, fails = parse_output(text)
        if not m:
            return (game_map, first, second, "error", 0, time.time() - t0, text[-400:], "", cpu, fails)
        if m.group(1):
            winner = first if m.group(1) == "A" else second
            outcome = "win" if winner == a else "loss"
            rounds = int(m.group(2))
        else:
            outcome, rounds = "draw", int(m.group(3) or m.group(4))
        # unswbc >= 1.2.3 says why a full-length game was won: "(longer queen, 12 to 5)", "(longest dragon, ...)"
        line = text[m.start():].split("\n", 1)[0]
        why = re.search(r"\(([^)]*)\)", line)
        return (game_map, first, second, outcome, rounds, time.time() - t0, "", why.group(1) if why else "", cpu, fails)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", help="the candidate (outcomes are reported from its side)")
    ap.add_argument("b", help="the opponent")
    ap.add_argument("--bots-dir", default=os.environ.get("BC_BOTS_DIR") or os.path.join(REPO, "bots"),
                    help="unswbc workspace holding the bot folders and maps/ (default: $BC_BOTS_DIR or <repo>/bots)")
    ap.add_argument("--maps", nargs="*", help="map names relative to maps/ without .map (default: every map in maps/)")
    ap.add_argument("--map-dir", help="play the first --map-count maps of maps/<dir> instead (e.g. generated variants)")
    ap.add_argument("--map-count", type=int, default=20)
    ap.add_argument("--map-skip", type=int, default=0, help="skip the first N maps of --map-dir")
    ap.add_argument("--repeat", type=int, default=1, help="play every (map, side) this many times, each with its own seed")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("-o", "--out", default=None, help="output folder (default: <bots-dir>/arena-out)")
    ap.add_argument("--sandbox", action="store_true", help="judge-exact sandbox with CPU metering")
    ap.add_argument("--seed-base", type=int, default=int(os.environ.get("ARENA_SEED_BASE", DEFAULT_SEED_BASE)),
                    help="added to every slot hash; another base gives games distinct from earlier runs")
    ap.add_argument("--fork", action="store_true", default=os.environ.get("ARENA_FORK", "0") == "1",
                    help="both sides of a (map, repeat) share one seed")
    ap.add_argument("--unseeded", action="store_true", default=os.environ.get("ARENA_SEEDED", "1") == "0",
                    help="random seeds instead of paired ones")
    ap.add_argument("--timeout", type=int, default=GAME_TIMEOUT, help="seconds per game")
    ap.add_argument("--no-replays", action="store_true", help="do not write replays")
    ap.add_argument("--no-warm", action="store_true", help="skip the native warm-up build")
    args = ap.parse_args()
    args.seeded = not args.unseeded
    if args.a == args.b:
        sys.exit("pick two different bots (copy one under another name for a mirror match)")
    bots = os.path.abspath(args.bots_dir)
    out = os.path.abspath(args.out or os.path.join(bots, "arena-out"))

    map_dir = os.path.join(bots, "maps")
    names = args.maps or sorted(os.path.splitext(f)[0] for f in os.listdir(map_dir) if f.endswith(".map"))
    if args.map_dir:
        sub = os.path.join(map_dir, args.map_dir)
        names = [f"{args.map_dir}/{f[:-4]}" for f in sorted(os.listdir(sub)) if f.endswith(".map")]
        names = names[args.map_skip:args.map_skip + args.map_count]
    maps = [os.path.join(map_dir, n + ".map") for n in names]
    missing = [m for m in maps if not os.path.exists(m)]
    if missing:
        sys.exit(f"maps not found: {' '.join(missing)}")
    os.makedirs(os.path.join(out, "replays"), exist_ok=True)

    for bot in (args.a, args.b):
        purge_wasm(bot)
        if not args.no_warm:
            warm(bots, bot)
    jobs = []
    for rep in range(args.repeat):
        for m in maps:
            jobs.append((bots, m, args.a, args.b, args.a, out, args, rep))
            jobs.append((bots, m, args.b, args.a, args.a, out, args, rep))

    rows = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for r in pool.map(play, jobs):
            rows.append(r)
            gm, first, second, outcome, rounds, secs, err, why, _, fails = r
            nf = fails["A"] + fails["B"]
            print(f"{os.path.basename(gm):<24} {first:>10} vs {second:<10} {outcome:<5} r{rounds:<4} {secs:6.1f}s"
                  + (f"  ({why})" if why else "") + (f"  failures A={fails['A']} B={fails['B']}" if nf else ""), flush=True)
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
    with open(os.path.join(out, "cpu.txt"), "a") as f:
        for gm, first, second, outcome, rounds, secs, err, why, cpu, fails in rows:
            a_team = "A" if first == args.a else "B"
            b_team = "B" if a_team == "A" else "A"
            fmt = lambda t: f"{cpu[t]:.1f}" if t in cpu else "-"  # noqa: E731
            f.write(f"{os.path.basename(gm)} {first} vs {second}: cand_max={fmt(a_team)}M opp_max={fmt(b_team)}M "
                    f"tle={fails[a_team]} opp_tle={fails[b_team]} secs={secs:.0f}\n")


if __name__ == "__main__":
    main()
