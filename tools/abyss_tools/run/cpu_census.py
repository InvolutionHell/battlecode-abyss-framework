"""Worst-case CPU census: the release check of a bot - no dragon-turn over 95M points (the hard line under the judge's
100M limit) and no timeout.

Every game runs `unswbc run MAP A B --sandbox -v --seed S` (the judge's metering) and the verbose stream is parsed:
`round R: bot ID (team T) points N memory M` per dragon-turn, `... exceeded CPU limit` (a timeout: the dragon dies), deaths,
and the bots' stdout, searched for the fallback marker (default `fb95`: a module that falls back at the 95M line prints
`LOG fb95 <module>` in its trace build). Per bot and scenario: dragon-turns, p50 / p99 / max points, turns over 85M and 95M,
timeouts, fallback markers, memory (max / p99 MiB of the process; the sandbox caps it at 768 pages = 48 MiB), first turns
(r0 starters and newborns: the process start and its setup count), the peak army, the real inbox (exact, from the replay's
sonar events: senders with a higher id in the previous round + lower ids this round) and the synthetic one. Every turn over
95M is listed.

Scenarios:
  big       the large official maps (--big-maps, from the maps folder); the bot against a copy of itself (both sides are
            measured), 500 rounds, a full army on both sides
  worst64   a generated 64x64 map: the bot's 64 units (length 3, the unit limit: no split) in a checkerboard with 64
            flooders (census_bots/flooder: four random u64 rays a dragon-turn, all die at r60), and every turn of the bot
            reads a --flood value inbox (512 = 128 senders x 4 rays, the most one dragon can receive) through flood_shim.hpp
  newborn   the same map with 32 units of length 8 against 32 flooders: the bot splits up to 64 under the flood, so the
            newborns' first turns (process start + setup + a flooded inbox) are measured
  selftest  census_bots/cpuhog against itself: known costs (95.5M + a fallback marker every 10 rounds, one 101M turn) that
            the census must report exactly - run it after any change to this tool

The flooded copy of the bot is built in a scratch workspace (--work): the copy's `std::fgets(` calls go through
floodshim::fgets, which rewrites NUM_MSGS and serves the extra values inside the turn's metered points. The bot folder itself
is never modified. Games wait for a free slot (at most --max-games `unswbc run` processes on the machine).

  python -m abyss_tools.run.cpu_census [--bot abyss] [--bots DIR] [--maps-dir DIR] [--scenarios big worst64 newborn selftest]
         [--seed 7] [--flood 512] [--jobs 2] [--max-games 3] [--out DIR] [--unswbc PATH] [--marker REGEX] [--define NAME=VAL]
  python -m abyss_tools.run.cpu_census --report DIR [DIR2 ..]     (the markdown table again from DIR/census.json)
  abyss-cpu-census ...
"""
import argparse
import collections
import concurrent.futures as cf
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from ..paths import bots_dir, maps_dir, purge_wasm, unswbc

HERE = os.path.dirname(os.path.abspath(__file__))
CENSUS_BOTS = os.path.join(HERE, "census_bots")
FLOOD_SHIM = os.path.join(HERE, "flood_shim.hpp")

LINE = 95_000_000
SOFT = 85_000_000
LIMIT = 100_000_000
MEM_LIMIT = 768 * 65536                         # the sandbox's MAX_MEMORY_PAGES: 48 MiB a dragon process
MIB = 1 << 20
BIG_MAPS = ("islands", "australia", "unsw", "schooltime")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
WHO = re.compile(r"^round (\d+): bot (\d+) \(team ([AB?])\) (.*)$")
POINTS = re.compile(r"^points (\d+) memory (\d+)")
NO_BUILD = shutil.ignore_patterns(".unswbc-build")
SLOT = threading.Lock()                         # one census thread at a time checks for a free slot and starts its game


# ------------------------------------------------------------------------------------------------------------------ maps

def make_map(w, h, dragons, beds=(), name="census"):
    """dragons: [(team 0/1, [(x, y) head first])] in id order, teams alternating"""
    bd = {(x, y): (lo, hi) for x, y, lo, hi in beds}
    lines = [f"MAP {w} {h}", "SYMMETRY xy", f"MAP_NAME {name}", f"TILE_COUNT {w * h}"]
    for y in range(h):
        for x in range(w):
            lo, hi = bd.get((x, y), (0, 0))
            lines.append(f"TILE {x} {y} {lo} {hi}")
    lines.append("EDGE_COUNT 0")
    lines.append(f"DRAGON_COUNT {len(dragons)}")
    for team, body in dragons:
        lines.append(f"DRAGON {team} {len(body)} " + " ".join(f"{x} {y}" for x, y in body))
    return "\n".join(lines) + "\nEND\n"


def checkerboard(w, h, n_a, len_a, n_b, len_b, sx, sy, x0=1, y0=1):
    """n_a dragons of length len_a and n_b of length len_b on a sx x sy lattice, teams on a checkerboard, bodies hanging
    south of the head (facing north); returned in id order with the teams alternating A, B, A, B, ..."""
    slots = {0: [], 1: []}
    for j, y in enumerate(range(y0, h - max(len_a, len_b) + 1, sy)):
        for i, x in enumerate(range(x0, w, sx)):
            slots[(i + j) % 2].append((x, y))
    if len(slots[0]) < n_a or len(slots[1]) < n_b:
        raise ValueError("map too small for the army")
    a = [[(x, y + k) for k in range(len_a)] for x, y in slots[0][:n_a]]
    b = [[(x, y + k) for k in range(len_b)] for x, y in slots[1][:n_b]]
    out = []
    for k in range(max(n_a, n_b)):
        if k < n_a:
            out.append((0, a[k]))
        if k < n_b:
            out.append((1, b[k]))
    if n_a != n_b:
        raise ValueError("the engine needs alternating teams: give both sides the same count")
    return out


def lattice_beds(w, h, period=10):
    """beds (TILE x y 1 PERIOD) where x and y are 3 or 4 mod 8: point-symmetric (the engine checks the beds against
    SYMMETRY xy) and never under a checkerboard start (x = 1 mod 4)"""
    keep = lambda v, n: v % 8 in (3, 4) and (n - 1 - v) % 8 in (3, 4)  # noqa: E731
    return [(x, y, 1, period) for y in range(h) for x in range(w) if keep(x, w) and keep(y, h)]


# ------------------------------------------------------------------------------------------------------------- workspace

def copy_bot(src, dest_name, work, defines=()):
    """the bot folder `src` copied as WORK/DEST_NAME; a tag line in every .cpp gives the copy its own wasm cache key (unswbc
    keys its sandbox build cache on the .c / .cc / .cpp contents only), so a copy never runs a stale build"""
    dst = os.path.join(work, dest_name)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=NO_BUILD)
    for f in os.listdir(dst):
        if f.endswith((".cpp", ".cc", ".c")):
            p = os.path.join(dst, f)
            with open(p) as fh:
                text = fh.read()
            head = "".join(f"#define {k} {v}\n" for k, v in defines)   # --define: compile flags of this copy only
            with open(p, "w") as fh:
                fh.write(f"// cpu census copy {dest_name}\n" + head + text)
    return dst


def flooded_copy(src, dest, work, flood, mode=2, defines=()):
    """the bot folder `src` copied as DEST, its std::fgets( calls routed through floodshim::fgets (the copy only)"""
    dst = copy_bot(src, dest, work, defines)
    with open(FLOOD_SHIM) as fh:
        hdr = fh.read()
    hdr = hdr.replace("#pragma once", f"#pragma once\n#define FLOOD_N {int(flood)}\n#define FLOOD_MODE {int(mode)}", 1)
    with open(os.path.join(dst, "flood_shim.hpp"), "w") as fh:
        fh.write(hdr)
    n = 0
    for f in sorted(os.listdir(dst)):
        if not f.endswith((".hpp", ".cpp", ".h", ".cc")) or f == "flood_shim.hpp":
            continue
        p = os.path.join(dst, f)
        with open(p) as fh:
            text = fh.read()
        k = text.count("std::fgets(")
        if k:
            with open(p, "w") as fh:
                fh.write('#include "flood_shim.hpp"\n' + text.replace("std::fgets(", "floodshim::fgets("))
            n += k
    if n == 0:
        raise SystemExit(f"{src}: no std::fgets( to route through the flood shim (only std::fgets readers are supported)")
    return dest


def wait_slot(max_games):
    while True:
        try:
            n = len(subprocess.run(["pgrep", "-f", "unswbc run"], capture_output=True, text=True).stdout.split())
        except FileNotFoundError:
            return
        if n < max_games:
            return
        time.sleep(5)


# ------------------------------------------------------------------------------------------------------------- one game

def parse_stream(lines, marker, forced=None):
    """the per-turn records of an `unswbc run --sandbox -v` stream"""
    mk = re.compile(marker)
    fc = re.compile(forced) if forced else None
    forced_turns = []                            # --forced: (round, id, team, line) of the turns forced past the line
    turns = []                                   # (round, id, team, points, first turn, memory bytes)
    errors, deaths, markers = [], [], []
    seen = set()
    result = ""
    block = None                                 # (round, id, team) while reading a stdout / stderr block
    for raw in lines:
        line = ANSI.sub("", raw.rstrip("\n")).rstrip("\r")
        if "\r" in line:
            line = line.split("\r")[-1]
        m = WHO.match(line)
        if block is not None and not m:
            if line == "":
                block = None
            else:
                if mk.search(line):
                    markers.append(block + (line,))
                if fc and fc.search(line):
                    forced_turns.append(block + (line,))
            continue
        block = None
        if m:
            r, i, t, rest = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
            if rest in ("stdout:", "stderr:"):
                block = (r, i, t)
                continue
            pm = POINTS.match(rest)
            if pm:
                first = i not in seen
                seen.add(i)
                turns.append((r, i, t, int(pm.group(1)), first, int(pm.group(2))))
                continue
            if rest.startswith("died:"):
                deaths.append((r, i, t, rest[5:].strip()))
                continue
            errors.append((r, i, t, rest))
            continue
        if line.startswith("team ") and " wins after " in line or line.startswith("draw after"):
            result = line
        elif line.startswith("error:"):
            result = line                        # the engine refused the game (a map error, a build error)
    return {"turns": turns, "errors": errors, "deaths": deaths, "markers": markers, "forced": forced_turns, "result": result}


def replay_facts(replay):
    """from the replay: the most values a dragon of each team read in one turn (senders with a higher id cast in the
    previous round, lower ids in this round) and the peak number of living dragons per team"""
    from ..replay.io import initial_teams, load
    rep = load(replay)
    team = initial_teams(rep.map)
    alive = collections.Counter(team.values())
    peak = dict(alive)
    hits = collections.defaultdict(list)         # (round, receiver) -> sender ids
    rnd = 0
    for e in rep.events:
        k = e.which()
        if k == "roundStart":
            rnd = e.roundStart.round
        elif k == "dragonSplit":
            t = "A" if str(e.dragonSplit.team) == "a" else "B"
            team[e.dragonSplit.childId] = t
            alive[t] += 1
            peak[t] = max(peak.get(t, 0), alive[t])
        elif k == "dragonDeath":
            t = team.get(e.dragonDeath.id)
            if t:
                alive[t] -= 1
        elif k == "sonarPing":
            p = e.sonarPing
            if p.which() == "hitId":
                hits[(rnd, p.hitId)].append(p.senderId)
    best = {"A": 0, "B": 0}
    recv = {d for _, d in hits}
    rounds = {r for r, _ in hits}
    for d in recv:
        t = team.get(d)
        if t is None:
            continue
        for r in rounds | {x + 1 for x in rounds}:
            n = sum(1 for s in hits.get((r - 1, d), ()) if s >= d) + sum(1 for s in hits.get((r, d), ()) if s < d)
            if n > best[t]:
                best[t] = n
    return {"inbox": best, "peak": peak}


def play(job):
    """one census game: build, run, parse; returns the game record"""
    work, exe, max_games, marker = job["work"], job["unswbc"], job["max_games"], job["marker"]
    forced = job.get("forced")
    os.makedirs(job["outdir"], exist_ok=True)
    stem = job["name"]
    replay = os.path.join(job["outdir"], stem + ".replay")
    log = os.path.join(job["outdir"], stem + ".log.gz")
    for b in (job["a"], job["b"]):
        purge_wasm(b)
    cmd = [exe, "run", job["map"], job["a"], job["b"], "--sandbox", "-v", "--seed", str(job["seed"]), "-o", replay,
           "--no-draw", "--no-indicator"]
    with SLOT:
        wait_slot(max_games)
        t0 = time.time()
        p = subprocess.Popen(cmd, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             bufsize=1 << 16)
        time.sleep(3)                            # let pgrep see this game before the next thread looks
    with gzip.open(log, "wt") as fo:
        def tee():
            for line in p.stdout:
                fo.write(line)
                yield line
        parsed = parse_stream(tee(), marker, forced)
    p.wait()
    secs = time.time() - t0
    rec = {"name": stem, "scenario": job["scenario"], "bot": job.get("bot", ""), "map": os.path.basename(job["map"]),
           "seed": job["seed"], "a": job["a"], "b": job["b"], "measure": job["measure"], "flood": job.get("flood", 0),
           "secs": round(secs, 1), "exit": p.returncode, "result": parsed["result"], "turns": parsed["turns"],
           "errors": parsed["errors"], "deaths": len(parsed["deaths"]), "markers": parsed["markers"],
           "forced": parsed["forced"], "log": log, "replay": replay, "defines": job.get("defines", [])}
    add_replay_facts(rec)
    return rec


def add_replay_facts(rec):
    try:
        f = replay_facts(rec["replay"])
        rec["inbox_real"], rec["peak_alive"] = f["inbox"], f["peak"]
    except Exception as e:  # noqa: BLE001 - a column of the report, not a reason to lose the game
        rec["inbox_real"], rec["peak_alive"] = {"error": str(e)[:200]}, {}


# --------------------------------------------------------------------------------------------------------------- summary

def pct(v, q):
    if not v:
        return float("nan")
    v = sorted(v)
    k = (len(v) - 1) * q / 100
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def side_rows(rec):
    """(label, team) rows of a game: the measured sides"""
    return [(rec["a"] if t == "A" else rec["b"], t) for t in rec["measure"]]


def summarise(rec, team, starters):
    T = [t for t in rec["turns"] if t[2] == team]
    pts = [t[3] for t in T]
    first0 = [t[3] for t in T if t[4] and t[1] < starters]
    firstn = [t[3] for t in T if t[4] and t[1] >= starters]
    units = collections.Counter(t[0] for t in T)
    tle = [e for e in rec["errors"] if e[2] == team and "exceeded" in e[3]]
    other = [e for e in rec["errors"] if e[2] == team and "exceeded" not in e[3]]
    mk = [m for m in rec["markers"] if m[2] == team]
    over = sorted([t for t in T if t[3] > LINE], key=lambda t: -t[3])
    mem = [t[5] for t in T if len(t) > 5]        # records written without the memory column have 5-tuples
    return {"turns": len(T), "p50": pct(pts, 50), "p99": pct(pts, 99), "max": max(pts, default=0),
            "mem_max": max(mem) if mem else float("nan"), "mem_p99": pct(mem, 99),
            "over85": sum(1 for x in pts if x > SOFT), "over95": len(over), "timeouts": len(tle), "errors": len(other),
            "markers": len(mk), "first_r0_n": len(first0), "first_r0_max": max(first0, default=0),
            "newborn_n": len(firstn), "newborn_p99": pct(firstn, 99), "newborn_max": max(firstn, default=0),
            "procs_round": max(units.values(), default=0), "over": over[:20], "tle": tle[:10], "other": other[:10]}


def m(x):
    return "-" if x != x else f"{x / 1e6:.1f}M"


def mib(x):
    return "-" if x != x else f"{x / MIB:.1f}"


def report(records, P):
    P("| scenario | map | seed | side (bot) | dragon-turns | peak units alive | inbox real / synthetic | p50 | p99 | max | >85M | "
      ">95M | timeouts | fallback markers | memory MiB: max / p99 | r0 first turns: n / max | newborn first turns: n / p99 / max "
      "| game |")
    P("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    gate = collections.defaultdict(lambda: [0, 0, 0, 0, float("nan")])  # bot -> [turns, over95, timeouts, max, max memory]
    lists = []
    failed = [r for r in records if not r["turns"]]
    records = [r for r in records if r["turns"]]
    for rec in records:
        starters = rec.get("starters", 0)
        if "peak_alive" not in rec and os.path.exists(rec.get("replay", "")):
            add_replay_facts(rec)
        for label, team in side_rows(rec):
            s = summarise(rec, team, starters)
            ib = rec.get("inbox_real", {})
            real = ib.get(team, "?") if isinstance(ib, dict) else "?"
            syn = rec["flood"] if (team == "A" and rec["flood"]) else "-"
            peak = rec.get("peak_alive", {}).get(team, "?")
            P(f"| {rec['scenario']} | {rec['map']} | {rec['seed']} | {team} ({label}) | {s['turns']} | {peak} | {real} / {syn} | "
              f"{m(s['p50'])} | {m(s['p99'])} | {m(s['max'])} | {s['over85']} | {s['over95']} | {s['timeouts']} | {s['markers']} | "
              f"{mib(s['mem_max'])} / {mib(s['mem_p99'])} | "
              f"{s['first_r0_n']} / {m(s['first_r0_max'])} | {s['newborn_n']} / {m(s['newborn_p99'])} / {m(s['newborn_max'])} | "
              f"{rec['result'] or 'exit ' + str(rec['exit'])} ({rec['secs']:.0f} s) |")
            if rec["scenario"] == "selftest":
                continue
            g = gate[rec.get("bot") or label]
            g[0] += s["turns"]
            g[1] += s["over95"]
            g[2] += s["timeouts"]
            g[3] = max(g[3], s["max"])
            if s["mem_max"] == s["mem_max"]:
                g[4] = s["mem_max"] if g[4] != g[4] else max(g[4], s["mem_max"])
            for t in s["over"]:
                lists.append(f"{rec['scenario']} {rec['map']} {label}: round {t[0]} bot {t[1]} {t[3] / 1e6:.2f}M"
                             + (" (first turn)" if t[4] else ""))
            for e in s["tle"] + s["other"]:
                lists.append(f"{rec['scenario']} {rec['map']} {label}: round {e[0]} bot {e[1]}: {e[3]}")
    P("")
    P("Release check (no timeout and no turn over 95M on the large maps at full unit count; memory under the sandbox's "
      f"{MEM_LIMIT // MIB} MiB a process):")
    for bot, (n, o, tl, mx, mm) in gate.items():
        mem_ok = mm != mm or mm < MEM_LIMIT
        verdict = "PASS" if (o == 0 and tl == 0 and mem_ok) else "FAIL"
        P(f"- **{bot}**: {n} dragon-turns, max {mx / 1e6:.2f}M, {o} over 95M, {tl} timeouts, max memory "
          f"{'not recorded' if mm != mm else f'{mm / MIB:.1f} MiB'} -> **{verdict}**")
    checked = [r for r in records if r.get("forced") is not None and r.get("forced") != [] or r.get("defines")]
    if any(r.get("forced") for r in records):
        P("")
        P("Forced-fallback check (--forced: every forced turn prints the fallback marker, and no timeout):")
        for rec in checked:
            for label, team in side_rows(rec):
                f = {(x[0], x[1]) for x in rec.get("forced", []) if x[2] == team}
                mk = {(x[0], x[1]) for x in rec["markers"] if x[2] == team}
                tl = sum(1 for e in rec["errors"] if e[2] == team and "exceeded" in e[3])
                pts = {(t[0], t[1]): t[3] for t in rec["turns"] if t[2] == team}
                worst = max((pts.get(k, 0) for k in f), default=0)
                ok = bool(f) and f <= mk and tl == 0
                P(f"- {rec['scenario']} {rec['map']} seed {rec['seed']} {team} ({label}): forced turns {len(f)}, with a "
                  f"marker {len(f & mk)}, missing {len(f - mk)}, timeouts {tl}, worst forced turn {worst / 1e6:.2f}M -> "
                  f"**{'PASS' if ok else 'FAIL'}**")
    for rec in failed:
        lists.append(f"{rec['scenario']} {rec['map']}: no dragon-turn measured ({rec['result'] or 'exit ' + str(rec['exit'])})")
    if lists:
        P("")
        P("Turns over 95M, timeouts, other sandbox complaints and failed games:")
        for x in lists[:80]:
            P(f"- {x}")


def selftest_check(rec, P):
    """census_bots/cpuhog's known costs: the census must find them"""
    end = 30
    ok = True
    for label, team in side_rows(rec):
        s = summarise(rec, team, rec.get("starters", 0))
        T = [t for t in rec["turns"] if t[2] == team]
        alive_ids = sorted({t[1] for t in T})
        exp_over = sum(1 for t in T if t[0] % 10 == 5 and t[0] < end)
        hot = [t for t in T if t[0] % 10 == 5 and t[0] < end]
        cool = [t for t in T if t[0] % 10 != 5 and t[0] < end and not (t[0] == 7 and t[1] == 1)]
        checks = [
            ("every r%10==5 turn over 95M and under 100M", all(LINE < t[3] < LIMIT for t in hot) and len(hot) > 0),
            ("the others between 50M and 85M", all(50_000_000 <= t[3] < SOFT for t in cool)),
            ("over-95M count = the hot turns", s["over95"] == exp_over),
            ("a fallback marker per hot turn", s["markers"] == exp_over),
            ("one timeout (bot 1, round 7) on team B, none on A", s["timeouts"] == (1 if team == "B" else 0)),
        ]
        for name, good in checks:
            ok &= bool(good)
            P(f"- selftest {team} ({label}, ids {alive_ids}): {'ok  ' if good else 'FAIL'} {name}")
    return ok


# ------------------------------------------------------------------------------------------------------------------ main

def build_jobs(args, work):
    jobs = []
    out = args.out
    bot_src = os.path.join(args.bots, args.bot)
    if not os.path.isdir(bot_src) and any(s != "selftest" for s in args.scenarios):
        raise SystemExit(f"no bot folder {bot_src} (see --bots / --bot)")
    base = dict(work=work, unswbc=args.unswbc, max_games=args.max_games, marker=args.marker, forced=args.forced,
                defines=[f"{k}={v}" for k, v in args.define])
    if "selftest" in args.scenarios:
        hog = os.path.join(CENSUS_BOTS, "cpuhog")
        copy_bot(hog, "census_cpuhog_A", work)
        copy_bot(hog, "census_cpuhog_B", work)
        mp = os.path.join(work, "census_selftest.map")
        with open(mp, "w") as fh:
            fh.write(make_map(16, 16, checkerboard(16, 16, 2, 2, 2, 2, 4, 4), name="census selftest"))
        jobs.append(dict(base, name="selftest", scenario="selftest", map=mp, a="census_cpuhog_A", b="census_cpuhog_B",
                         seed=args.seed, measure="AB", outdir=out, starters=4))
    for sc, n, L, sy in (("worst64", 64, 3, 8), ("newborn", 32, 8, 16)):
        if sc not in args.scenarios:
            continue
        mine = f"{args.bot}_{sc}_fl{args.flood}"
        if args.flood:
            flooded_copy(bot_src, mine, work, args.flood, defines=args.define)
        else:
            copy_bot(bot_src, mine, work, args.define)
        opp = f"census_flooder_{sc}"
        copy_bot(os.path.join(CENSUS_BOTS, "flooder"), opp, work)
        mp = os.path.join(work, f"census_{sc}.map")
        with open(mp, "w") as fh:
            fh.write(make_map(64, 64, checkerboard(64, 64, n, L, n, 2, 4, sy), lattice_beds(64, 64), name=f"census {sc}"))
        jobs.append(dict(base, name=sc, scenario=sc, map=mp, a=mine, b=opp, seed=args.seed, measure="A", bot=args.bot,
                         outdir=out, flood=args.flood, starters=2 * n))
    if "big" in args.scenarios:
        for mpn in args.big_maps:
            mp = os.path.join(args.maps_dir, mpn + ".map")
            if not os.path.exists(mp):
                raise SystemExit(f"no map {mp} (see --maps-dir / --big-maps)")
            a, b = f"{args.bot}_{mpn}_A", f"{args.bot}_{mpn}_B"
            copy_bot(bot_src, a, work, args.define)
            copy_bot(bot_src, b, work, args.define)
            with open(mp) as fh:
                n = sum(1 for line in fh if line.startswith("DRAGON "))
            jobs.append(dict(base, name=f"big_{mpn}", scenario="big", map=mp, a=a, b=b, bot=args.bot,
                             seed=args.seed, measure="AB", outdir=out, starters=n))
    return jobs


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bot", default="abyss", help="the bot folder under --bots (default abyss)")
    ap.add_argument("--bots", help="the unswbc workspace with the bot folders (default: <repo>/bots or $ABYSS_BOTS)")
    ap.add_argument("--maps-dir", help="the folder of the official .map files (default: <bots>/maps or $ABYSS_MAPS)")
    ap.add_argument("--scenarios", nargs="+", default=["selftest", "worst64", "newborn", "big"],
                    choices=("selftest", "worst64", "newborn", "big"))
    ap.add_argument("--big-maps", nargs="+", default=list(BIG_MAPS), help="map file stems of the big scenario")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--flood", type=int, default=512, help="synthetic inbox size for worst64 / newborn (0 = none)")
    ap.add_argument("--jobs", "-j", type=int, default=2, help="census games at a time")
    ap.add_argument("--max-games", type=int, default=3, help="wait while this many `unswbc run` run on the machine")
    ap.add_argument("--marker", default=r"\bfb95\b", help="regex of the fallback marker in the bots' stdout")
    ap.add_argument("--forced", help="regex of a line a test build prints on a turn it forced past the line: every such "
                    "turn must also print the fallback marker")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VAL",
                    help="a #define for the copies of --bot only (repeatable), e.g. BC_FALLBACK95=1")
    ap.add_argument("--unswbc", help="the unswbc executable (default: $UNSWBC or unswbc on PATH)")
    ap.add_argument("--out", help="output folder (default: <bots>/arena-out/cpu_census)")
    ap.add_argument("--work", help="scratch workspace for the bot copies (default: a temp dir)")
    ap.add_argument("--report", nargs="+", help="print the table of earlier runs (DIRs with census.json)")
    args = ap.parse_args(argv)
    args.define = [tuple(d.split("=", 1)) if "=" in d else (d, "1") for d in args.define]
    args.bots = bots_dir(args.bots)
    args.maps_dir = maps_dir(args.maps_dir, args.bots)
    args.unswbc = unswbc(args.unswbc)
    args.out = os.path.abspath(os.path.expanduser(args.out or os.path.join(args.bots, "arena-out", "cpu_census")))
    return args


def main(argv=None):
    args = parse_args(argv)
    lines = []

    def P(s=""):
        print(s, flush=True)
        lines.append(s)

    if args.report:
        records = []
        for d in args.report:
            with open(os.path.join(d, "census.json")) as fh:
                records += json.load(fh)
        for rec in records:                      # a record without the memory column: its -v log still has the memory
            if rec["turns"] and len(rec["turns"][0]) < 6 and os.path.exists(rec.get("log", "")):
                with gzip.open(rec["log"], "rt", errors="replace") as fh:
                    parsed = parse_stream(fh, args.marker)
                rec.update(turns=parsed["turns"], markers=parsed["markers"], errors=parsed["errors"])
        report(records, P)
        for rec in records:
            if rec["scenario"] == "selftest" and rec["turns"]:
                P("")
                P(f"- selftest: **{'PASS' if selftest_check(rec, P) else 'FAIL'}**")
        return 0
    os.makedirs(args.out, exist_ok=True)
    work = args.work or tempfile.mkdtemp(prefix="cpu-census-")
    os.makedirs(work, exist_ok=True)
    jobs = build_jobs(args, work)
    ver = subprocess.run([args.unswbc, "--version"], capture_output=True, text=True).stdout.strip().splitlines()
    P(f"## CPU census: {args.bot}, {len(jobs)} games ({', '.join(j['name'] for j in jobs)}), {ver[0] if ver else 'unswbc'}, "
      f"seed {args.seed}, synthetic inbox {args.flood}")
    P("")
    records = []
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = {ex.submit(play, j): j for j in jobs}
        for f in cf.as_completed(futs):
            j = futs[f]
            rec = f.result()
            rec["starters"] = j.get("starters", 0)
            records.append(rec)
            print(f"  done {rec['name']}: {rec['result'] or rec['exit']} in {rec['secs']:.0f} s, {len(rec['turns'])} "
                  "dragon-turns", file=sys.stderr, flush=True)
    order = {j["name"]: k for k, j in enumerate(jobs)}
    records.sort(key=lambda r: order[r["name"]])
    with open(os.path.join(args.out, "census.json"), "w") as fh:
        json.dump(records, fh)
    report(records, P)
    ok = True
    for rec in records:
        if rec["scenario"] == "selftest":
            P("")
            ok = selftest_check(rec, P)
            P(f"- selftest: **{'PASS' if ok else 'FAIL'}**")
    with open(os.path.join(args.out, "census.md"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    if not args.work:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
