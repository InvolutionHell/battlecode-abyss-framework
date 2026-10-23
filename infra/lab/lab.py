#!/usr/bin/env python3
"""Batch evaluation over a pool of ssh worker nodes ("the lab").

A census is a set of pairs (candidate vs opponent) submitted under one TAG. Each pair is one run of
infra/lab/pair.sh on one node: the candidate plays the opponent on a map list x both sides x REPEAT plus PV generated
variant maps x both sides, all in the judge's sandbox, with paired seeds (infra/lab/arena.py). A local registry
(JSON, file-locked) records every pair; a dispatcher starts pending pairs on nodes with free slots and collects the
finished ones into $LAB_DATA/TAG/CAND_vs_OPP/.

  lab.py push [NODE ...] [--bots B ...]       rsync the code ($LAB_PUSH, default "bots infra") to the nodes; with
                                              --bots, of bots/ only maps/, starter/ and those bot folders
  lab.py submit TAG "cand ..." "opp ..." [--set NAME | --maps m1 m2 ...] [--repeat 2] [--pv N] [--prio 5]
                [--node N] [--fork] [--seed-base N] [--pv-dir DIR]
  lab.py run [--dry-run]                      one dispatch pass: start pending pairs on nodes with free slots
                                              (--dry-run: print the launches it would make, touch nothing)
  lab.py collect                              fetch finished pairs, print their W-L, mark them done
  lab.py daemon [--every 60]                  collect + run in a loop (nohup it; log in $LAB_DATA/lab.log)
  lab.py status [TAG]                         the registry
  lab.py report TAG                           W-L per pair (orig / pv / all, 95% interval), CPU failures, max points
  lab.py gauntlet TAG                         candidates x opponents table: W-L per cell, mean and worst opponent
  lab.py nodes                                load and running pairs per node
  lab.py cancel TAG [PAIR]                    drop pending pairs; running ones are killed on their node
  lab.py prio TAG "PAIR ..." N                re-prioritise pending pairs (lower number = sooner)
  lab.py replays TAG PAIR [--dest DIR]        copy a finished pair's replays from its node

Configuration (infra/lab/labconf.py): $LAB_ENV (KEY=VALUE file, default infra/config/lab.env), $LAB_NODES (node pool
TOML), $LAB_MAPSETS (map sets TOML), $LAB_DATA, $LAB_REMOTE_ROOT, $LAB_SSH_OPTS, $LAB_PUSH, $LAB_REMOTE_ENV,
$LAB_POSTPROCESS, $LAB_REPORT_CMD. A file $LAB_DATA/PAUSE stops new launches (node maintenance).
"""
import argparse
import concurrent.futures as cf
import contextlib
import datetime
import fcntl
import json
import math
import os
import re
import shlex
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labconf  # noqa: E402

labconf.load_env()
ROOT = labconf.repo_root()
DATA = labconf.data_dir()
REG = os.path.join(DATA, "registry.json")
RR = labconf.remote_root()
SSH = ["ssh"] + labconf.ssh_opts()
_POOL = None


def pool():
    """({node: slots}, scheduler settings), read once per process."""
    global _POOL
    if _POOL is None:
        _POOL = labconf.load_pool()
    return _POOL


def now():
    return datetime.datetime.now().strftime("%m-%d %H:%M")


def log(msg):
    line = f"[{now()}] {msg}"
    print(line, flush=True)
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, "lab.log"), "a") as f:
        f.write(line + "\n")


@contextlib.contextmanager
def locked():
    """Serialises every load-modify-save of the registry between the daemon and the CLI (without it, a submit made
    between the daemon's load and save is lost)."""
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, "registry.lock"), "w") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)


def load():
    if os.path.exists(REG):
        with open(REG) as f:
            return json.load(f)
    return {"jobs": []}


def save(reg):
    os.makedirs(DATA, exist_ok=True)
    tmp = REG + ".tmp"
    with open(tmp, "w") as f:
        json.dump(reg, f, indent=1)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, REG)


def remote_cmd(cmd):
    """Prefix $LAB_REMOTE_ENV (e.g. PATH=$HOME/tools/bin:$PATH) to a remote shell command."""
    env = os.environ.get("LAB_REMOTE_ENV", "").strip()
    return f"export {env}; {cmd}" if env else cmd


def ssh(node, cmd, timeout=120):
    p = subprocess.run(SSH + [node, remote_cmd(cmd)], capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def rsync_e():
    return " ".join(SSH)


def rpath(path):
    """A remote path for rsync: a leading ~/ is dropped (rsync's remote paths are relative to the login directory),
    because not every rsync lets the remote shell expand the tilde."""
    return path[2:] if path.startswith("~/") else path


def job_games(j):
    return 2 * len(j["maps"].split()) * j["repeat"] + 2 * j["pv"]


def pair_name(j):
    return f"{j['cand']}_vs_{j['opp']}"


def pair_dir(j):
    return f"{RR}/lab/{j['tag']}/{pair_name(j)}"


# ---------------------------------------------------------------- code
EXCLUDES = [".unswbc-build", "replays", ".git", "data", "*.replay", "*.zst", "__pycache__", "arena-out", "._*"]


def push(nodes, bots=None, workers=8):
    """rsync the code ($LAB_PUSH, default "bots infra") to the nodes, into the remote checkout. With `bots`, the
    non-bot paths go as usual but of bots/ only maps/, starter/ and the named folders, without --delete on bots/
    itself: a workspace full of old arm folders is large, and parallel full pushes saturate a slow uplink. The bot
    workspace is assumed to be <repo>/bots on both sides."""
    excl = [x for e in EXCLUDES for x in ("--exclude", e)]
    paths = [p for p in labconf.push_paths() if os.path.exists(os.path.join(ROOT, p))]
    if "infra" not in paths:
        paths.append("infra")       # pair.sh and arena.py run from the node's copy of infra/
    other = [p for p in paths if p.rstrip("/") != "bots"]

    def rsync(srcs, dest):
        cmd = ["rsync", "-az", "--delete", "-e", rsync_e()] + excl + srcs + [dest]
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=1800)
        return p.returncode, (p.stderr or "")[-200:]

    def one(n):
        try:
            subprocess.run(SSH + [n, f"mkdir -p {RR}/bots"], capture_output=True, timeout=120)
            if bots is None:
                rc, err = rsync(paths, f"{n}:{rpath(RR)}/")
                return n, rc, err
            rc, err = rsync(other, f"{n}:{rpath(RR)}/")
            if rc == 0:
                folders = [f"bots/{b}" for b in dict.fromkeys(["maps", "starter"] + list(bots))
                           if os.path.isdir(os.path.join(ROOT, "bots", b))]
                rc, err = rsync(folders, f"{n}:{rpath(RR)}/bots/")
            return n, rc, err
        except subprocess.TimeoutExpired:
            return n, 1, "timeout"
    with cf.ThreadPoolExecutor(max_workers=max(1, workers)) as ex:
        results = list(ex.map(one, nodes))
    for n, rc, err in results:
        log(f"push {n}: {'ok' if rc == 0 else 'FAILED ' + err}")
    return all(rc == 0 for _, rc, _ in results)


# ---------------------------------------------------------------- registry
def submit(tag, cands, opps, repeat, pv, prio, maps, node, fork=False, seed_base=None, pv_dir=None):
    with locked():
        reg = load()
        have = {(j["tag"], j["cand"], j["opp"]) for j in reg["jobs"]}
        added = 0
        for c in cands:
            for o in opps:
                if c == o or (tag, c, o) in have:
                    continue
                reg["jobs"].append({"id": f"{tag}/{c}_vs_{o}", "tag": tag, "cand": c, "opp": o, "repeat": repeat,
                                    "pv": pv, "maps": maps, "prio": prio, "state": "pending", "node": node, "pin": node,
                                    "slots": 0, "submitted": now(), "fork": bool(fork),
                                    "seed_base": seed_base, "pv_dir": pv_dir})
                added += 1
        save(reg)
    log(f"submit {tag}: {added} pairs (prio {prio}, {2 * len(maps.split()) * repeat} orig + {2 * pv} pv games each"
        f"{', fork seeds' if fork else ''}{f', seed base {seed_base}' if seed_base is not None else ''})")
    return added


def remote_state(node):
    """(running pair dirs, done pair dirs) on a node, from the STATUS / DONE markers."""
    rc, out, _ = ssh(node, f"cd {RR}/lab 2>/dev/null || exit 0; for s in */*/STATUS; do [ -e \"$s\" ] || continue; "
                           "d=${s%/STATUS}; if [ -e $d/DONE ]; then echo D $d; else echo R $d; fi; done")
    running, done = set(), set()
    for line in out.splitlines():
        if " " not in line:
            continue
        k, d = line.split(" ", 1)
        (done if k == "D" else running).add(d)
    return running, done


def launch_cmd(j, slots):
    """The detached remote launch of one pair. setsid (where the node has it) + nohup + no stdin, or ssh waits for the
    job; ';' rather than '&&' before nohup, or the whole list becomes a background subshell holding ssh's stdout."""
    hook = os.environ.get("LAB_POSTPROCESS", "").strip()
    env = f"LAB_POSTPROCESS={shlex.quote(hook)} " if hook else ""
    args = " ".join(shlex.quote(str(a)) for a in (j["tag"], j["cand"], j["opp"], slots, j["repeat"], j["maps"], j["pv"],
                                                    1 if j.get("fork") else 0,
                                                    "-" if j.get("seed_base") is None else j["seed_base"],
                                                    j.get("pv_dir") or "pv"))
    return (f"mkdir -p {RR}/lab/{j['tag']}; {env}nohup $(command -v setsid) bash {RR}/infra/lab/pair.sh {args} "
            f"> {pair_dir(j)}.launch.log 2>&1 < /dev/null & disown; echo launched")


def plan(reg, nodes, sched, paused=False):
    """Pure scheduling decision for one pass: [(job, node, slots)] in launch order, without side effects.

    Small pairs (<= big_games games) need min(min_slots, games) free slots and fill the leftovers. Big pairs wait for at
    least big_min free slots: started on a small leftover they run for hours. A big pair that cannot start reserves the
    node with the most free slots against lower-priority pairs, or small pairs keep taking the leftovers there and the
    big pair never sees big_min free slots."""
    if paused:
        return []
    used = {n: 0 for n in nodes}
    for j in reg["jobs"]:
        if j["state"] == "running" and j.get("node") in used:
            used[j["node"]] += j["slots"]
    pending = sorted((j for j in reg["jobs"] if j["state"] == "pending"), key=lambda j: (j["prio"], j["submitted"]))
    reserved = {}   # node -> priority of the big pair waiting for it
    out = []
    for j in pending:
        games = job_games(j)
        big = games > sched["big_games"]
        if j.get("pin") and j["pin"] not in nodes:
            continue
        choices = [j["pin"]] if j.get("pin") else sorted(nodes, key=lambda n: -(nodes[n] - used[n]))
        need = min(games, sched["big_min"]) if big else min(sched["min_slots"], games)
        placed = False
        for n in choices:
            free = nodes[n] - used[n]
            if n in reserved and reserved[n] < j["prio"]:
                continue
            if free < need or free <= 0:
                continue
            slots = min(free, games)
            used[n] += slots
            out.append((j, n, slots))
            placed = True
            break
        if not placed and big and not j.get("pin"):
            for n in sorted(nodes, key=lambda n: -(nodes[n] - used[n])):
                if n not in reserved:
                    reserved[n] = j["prio"]
                    break
    return out


def run_pass(reg):
    """Start pending pairs on nodes with free slots (none while $LAB_DATA/PAUSE exists)."""
    nodes, sched = pool()
    for j, n, slots in plan(reg, nodes, sched, os.path.exists(os.path.join(DATA, "PAUSE"))):
        # the registry is saved before the launch, so a crash here never leaves an unrecorded run (two copies of one
        # pair on one node otherwise)
        j.update({"state": "running", "node": n, "slots": slots, "started": now()})
        save(reg)
        try:
            rc, out, err = ssh(n, launch_cmd(j, slots))
        except subprocess.TimeoutExpired:
            rc, out, err = 1, "", "ssh timeout"
        if rc != 0 or "launched" not in out:
            try:
                _, out2, _ = ssh(n, f"ls {pair_dir(j)}/STATUS 2>/dev/null")
            except subprocess.TimeoutExpired:
                out2 = ""
            if not out2.strip():
                j.update({"state": "pending", "node": j.get("pin"), "slots": 0})
                save(reg)
                log(f"start {j['id']} on {n} FAILED: {err[-200:]}")
                continue
        log(f"start {j['id']} on {n} with {slots} slots ({job_games(j)} games)")
    save(reg)


def dry_run():
    """What one dispatch pass would start now (registry and nodes untouched)."""
    nodes, sched = pool()
    reg = load()
    paused = os.path.exists(os.path.join(DATA, "PAUSE"))
    launches = plan(reg, nodes, sched, paused)
    pending = sum(j["state"] == "pending" for j in reg["jobs"])
    print(f"{len(nodes)} nodes, {sum(nodes.values())} slots; {pending} pending pairs"
          f"{' (PAUSE file present: nothing starts)' if paused else ''}")
    for j, n, slots in launches:
        print(f"would start {j['id']} on {n} with {slots} slots ({job_games(j)} games)")
    if not launches:
        print("nothing to start")


SUMMARY = re.compile(r"(\S+) vs (\S+): (\d+)W (\d+)L (\d+)D (\d+)E over (\d+) games")


def parse_summary(path):
    """(W, L, D, E) of the last summary line of an arena summary.txt, or None."""
    m = None
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                m = SUMMARY.match(line) or m
    return (int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))) if m else None


def cpu_stats(path):
    """(candidate's CPU / wall-clock failures, candidate's max points per turn in M) from an arena cpu.txt."""
    tle, top = 0, None
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                t = re.search(r"\btle=(\d+)", line)
                if t:
                    tle += int(t.group(1))
                c = re.search(r"cand_max=([0-9.]+)M", line)
                if c:
                    top = max(top or 0.0, float(c.group(1)))
    return tle, top


def collect_pass(reg):
    by_node = {}
    for j in reg["jobs"]:
        if j["state"] == "running":
            by_node.setdefault(j["node"], []).append(j)
    for n, jobs in by_node.items():
        try:
            r_dirs, d_dirs = remote_state(n)
        except subprocess.TimeoutExpired:
            log(f"collect {n}: ssh timeout")
            continue
        for j in jobs:
            rel = f"{j['tag']}/{pair_name(j)}"
            if rel in d_dirs:
                fetch(j)
            elif rel not in r_dirs:
                # started but no STATUS yet (first seconds) or the launch failed: give it two more passes
                j["miss"] = j.get("miss", 0) + 1
                if j["miss"] >= 3:
                    j["state"] = "error"
                    log(f"{j['id']}: no STATUS on {n} after 3 passes -> error")
    save(reg)


def fetch(j):
    dest = os.path.join(DATA, j["tag"], pair_name(j))
    os.makedirs(dest, exist_ok=True)
    src = f"{j['node']}:{rpath(pair_dir(j))}"
    subprocess.run(["rsync", "-az", "-e", rsync_e(), "--include", "*/", "--include", "*.txt", "--include", "*.json",
                    "--include", "*.log", "--include", "STATUS", "--exclude", "*", src + "/", dest + "/"],
                   capture_output=True, text=True, timeout=600)
    subprocess.run(["rsync", "-az", "-e", rsync_e(), src + ".json", dest + ".json"], capture_output=True, text=True,
                   timeout=600)  # the optional LAB_POSTPROCESS output
    j["state"] = "done"
    j["finished"] = now()
    j["orig"] = parse_summary(os.path.join(dest, "orig", "summary.txt"))
    j["pvres"] = parse_summary(os.path.join(dest, "pv", "summary.txt")) if j["pv"] else None
    t1, c1 = cpu_stats(os.path.join(dest, "orig", "cpu.txt"))
    t2, c2 = cpu_stats(os.path.join(dest, "pv", "cpu.txt"))
    j["tle"] = t1 + t2
    j["maxcpu"] = max([c for c in (c1, c2) if c is not None], default=None)
    log(f"DONE {j['id']} on {j['node']}: {fmt(j)}")


def wilson(w, n, z=1.96):
    """95% Wilson interval of a win rate (draws count half before this is called)."""
    if n <= 0:
        return 0.0, 1.0
    p = w / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return max(0.0, c - h), min(1.0, c + h)


def fmt(j):
    def wl(r):
        return "-" if not r else f"{r[0]}-{r[1]}" + (f"-{r[2]}d" if r[2] else "") + (f" ({r[3]}E)" if r[3] else "")
    tot = [0, 0, 0]
    for r in (j.get("orig"), j.get("pvres")):
        if r:
            tot[0] += r[0]
            tot[1] += r[1]
            tot[2] += r[2]
    n = sum(tot)
    score = tot[0] + 0.5 * tot[2]
    lo, hi = wilson(score, n)
    cpu = f"{j['maxcpu']:.1f}M" if j.get("maxcpu") is not None else "-"
    return (f"orig {wl(j.get('orig'))}  pv {wl(j.get('pvres'))}  all {tot[0]}-{tot[1]} "
            f"({100 * score / max(1, n):.0f}% [{100 * lo:.0f}-{100 * hi:.0f}])  tle {j.get('tle', '?')}  max {cpu}")


def status(tag):
    reg = load()
    if not reg["jobs"]:
        print(f"registry {REG}: no pairs")
        return
    for j in reg["jobs"]:
        if tag and j["tag"] != tag:
            continue
        extra = fmt(j) if j["state"] == "done" else f"{j.get('node') or '-'} slots {j.get('slots', 0)} since {j.get('started', '-')}"
        print(f"{j['state']:<9} p{j['prio']} {j['id']:<45} {extra}")


def report(tag):
    """Per pair W-L with a 95% interval, then per candidate over all its opponents; $LAB_REPORT_CMD (if set) is run
    as `$LAB_REPORT_CMD TAG DATA_DIR` for KPI tables built from the LAB_POSTPROCESS outputs."""
    reg = load()
    jobs = [j for j in reg["jobs"] if j["tag"] == tag]
    print(f"## {tag}: {sum(j['state'] == 'done' for j in jobs)} of {len(jobs)} pairs done\n")
    print("| pair | orig | pv | all | score [95%] | CPU failures | max points/turn |")
    print("|---|---|---|---|---|---|---|")
    per = {}
    for j in jobs:
        if j["state"] != "done":
            print(f"| {pair_name(j)} | {j['state']} | | | | | |")
            continue
        r = [0, 0, 0]
        for x in (j.get("orig"), j.get("pvres")):
            if x:
                r = [r[0] + x[0], r[1] + x[1], r[2] + x[2]]
        n, s = sum(r), r[0] + 0.5 * r[2]
        lo, hi = wilson(s, n)
        c = per.setdefault(j["cand"], [0.0, 0, 0])
        c[0] += s
        c[1] += n
        c[2] += j.get("tle") or 0
        wl = lambda x: f"{x[0]}-{x[1]}" + (f" ({x[2]}d)" if x[2] else "") if x else "-"  # noqa: E731
        cpu = f"{j['maxcpu']:.1f}M" if j.get("maxcpu") is not None else "-"
        print(f"| {pair_name(j)} | {wl(j.get('orig'))} | {wl(j.get('pvres'))} | {wl(r)} | "
              f"{100 * s / max(1, n):.1f}% [{100 * lo:.0f}, {100 * hi:.0f}] | {j.get('tle', '?')} | {cpu} |")
    if per:
        print("\n| candidate | games | score [95%] | CPU failures |\n|---|---|---|---|")
        for c, (s, n, t) in sorted(per.items()):
            lo, hi = wilson(s, n)
            print(f"| {c} | {n} | {100 * s / max(1, n):.1f}% [{100 * lo:.0f}, {100 * hi:.0f}] | {t} |")
    cmd = os.environ.get("LAB_REPORT_CMD", "").strip()
    if cmd:
        print()
        subprocess.run(shlex.split(cmd) + [tag, DATA])


def gauntlet(tag):
    """Cross table: per candidate, W-L against every opponent of the tag (original maps), the mean win rate over
    opponents and the worst opponent. A candidate that gains against one reference must not lose ground here."""
    reg = load()
    by = {}
    for j in reg["jobs"]:
        if j["tag"] == tag and j["state"] == "done" and j.get("orig"):
            by.setdefault(j["cand"], {})[j["opp"]] = (j["orig"][0], j["orig"][1])
    opps = sorted({o for d in by.values() for o in d})
    print(f"## Gauntlet {tag}: " + " | ".join(opps))
    for c, d in sorted(by.items()):
        rates = [100 * w / max(1, w + l) for w, l in d.values()]
        worst = min(d.items(), key=lambda kv: kv[1][0] / max(1, sum(kv[1])))
        cells = " | ".join(f"{d[o][0]}-{d[o][1]}" if o in d else "-" for o in opps)
        print(f"| {c} | {cells} | mean {sum(rates) / len(rates):.0f}% | worst {worst[0]} {worst[1][0]}-{worst[1][1]} |")


def nodes_cmd():
    nodes, _ = pool()

    def one(n):
        try:
            _, out, err = ssh(n, f"echo load $(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null || uptime | sed 's/.*load averages*: //'), "
                                 f"$(nproc 2>/dev/null || sysctl -n hw.ncpu) cores; cd {RR}/lab 2>/dev/null && "
                                 "for s in */*/STATUS; do [ -e \"$s\" ] || continue; d=${s%/STATUS}; [ -e $d/DONE ] || echo \"  running $d\"; done", 60)
            return n, (out.strip() or err.strip()[-100:])
        except subprocess.TimeoutExpired:
            return n, "timeout"
    with cf.ThreadPoolExecutor(max_workers=max(1, len(nodes))) as ex:
        for n, out in ex.map(one, nodes):
            print(f"{n:<24} slots {nodes[n]:<4} {out}")


def cancel(tag, pair):
    with locked():
        reg = load()
        for j in reg["jobs"]:
            if j["tag"] != tag or (pair and pair_name(j) != pair) or j["state"] not in ("pending", "running"):
                continue
            if j["state"] == "running":
                # the [h] / [y] / [c] brackets keep the patterns from matching the shell running pkill itself; only this
                # pair's games are killed: every game's command line carries the pair's output path (a pattern on a
                # bot name alone also kills other pairs' games of that bot on the same node)
                pdir = f"/lab/{tag}/{pair_name(j)}/"
                ssh(j["node"], f"pkill -f 'pair.s[h] {tag} {j['cand']} {j['opp']} '; "
                               f"pkill -f 'arena.p[y] {j['cand']} {j['opp']} .*{pdir}'; sleep 1; "
                               f"pkill -f 'unswb[c] run .*{pdir}'; true")
            j["state"] = "cancelled"
            log(f"cancelled {j['id']}")
        save(reg)


def replays(tag, pair, dest):
    reg = load()
    j = next((j for j in reg["jobs"] if j["tag"] == tag and pair_name(j) == pair), None)
    if not j or not j.get("node"):
        raise SystemExit(f"{tag}/{pair}: not in the registry or never started")
    dest = dest or os.path.join(DATA, "replays", tag, pair)
    n = 0
    for part in ("orig", "pv"):
        os.makedirs(os.path.join(dest, part), exist_ok=True)
        subprocess.run(["rsync", "-a", "-e", rsync_e(), f"{j['node']}:{rpath(pair_dir(j))}/{part}/replays/",
                        os.path.join(dest, part) + "/"], capture_output=True, text=True, timeout=1800)
        n += sum(1 for f in os.listdir(os.path.join(dest, part)) if ".replay" in f)
    print(f"{tag}/{pair} from {j['node']}: {n} replays -> {dest}")


def daemon(every):
    log(f"daemon start (every {every}s)")
    while True:
        try:
            with locked():
                collect_pass(load())
            with locked():
                run_pass(load())
        except Exception as e:  # noqa: BLE001 - an ssh hiccup must not stop the loop
            log(f"daemon error: {e}")
        time.sleep(every)


def resolve_maps(a):
    """(maps string, pv count, pv dir) from --set / --maps / --pv / --pv-dir."""
    sets, default = labconf.load_mapsets()
    name = a.set or default
    if a.maps:
        smaps, spv, spvdir = " ".join(a.maps), 0, "pv"
    elif name in sets:
        smaps, spv, spvdir = sets[name]
    else:
        raise SystemExit(f"map set {name!r} not in {labconf.mapsets_path()} (sets: {', '.join(sorted(sets)) or 'none'})")
    return smaps, spv if a.pv is None else a.pv, a.pv_dir or spvdir


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("push", help="rsync the code to the nodes")
    p.add_argument("nodes", nargs="*")
    p.add_argument("--bots", nargs="*", help="of bots/, only maps/, starter/ and these bot folders")
    p.add_argument("--workers", type=int, default=None, help="parallel pushes")
    p = sub.add_parser("submit", help="queue candidates x opponents under a tag")
    p.add_argument("tag")
    p.add_argument("cands", help="space-separated candidate bot folders")
    p.add_argument("opps", help="space-separated opponent bot folders")
    p.add_argument("--set", default=None, help="map set from the map sets file (default: its default_set)")
    p.add_argument("--maps", nargs="*", help="explicit map list (overrides --set; pv defaults to 0)")
    p.add_argument("--repeat", type=int, default=2, help="games per (map, side)")
    p.add_argument("--pv", type=int, default=None, help="generated variant maps per pair (x2 sides)")
    p.add_argument("--pv-dir", default=None, help="maps/<dir> for the variant maps (default from the set, else pv)")
    p.add_argument("--prio", type=int, default=5, help="lower = sooner")
    p.add_argument("--node", default=None, help="pin the pairs to one node")
    p.add_argument("--fork", action="store_true", help="both sides of a (map, repeat) share one seed")
    p.add_argument("--seed-base", type=int, default=None, help="arena seed base (fresh seeds = another base)")
    p = sub.add_parser("run", help="one dispatch pass")
    p.add_argument("--dry-run", action="store_true", help="print the planned launches; no ssh, registry unchanged")
    sub.add_parser("collect", help="one collect pass")
    sub.add_parser("nodes", help="load and running pairs per node")
    p = sub.add_parser("daemon", help="collect + run in a loop")
    p.add_argument("--every", type=int, default=60)
    p = sub.add_parser("status", help="the registry")
    p.add_argument("tag", nargs="?")
    p = sub.add_parser("report", help="W-L table of a tag")
    p.add_argument("tag")
    p = sub.add_parser("gauntlet", help="candidates x opponents table of a tag")
    p.add_argument("tag")
    p = sub.add_parser("cancel", help="cancel a tag or one pair")
    p.add_argument("tag")
    p.add_argument("pair", nargs="?")
    p = sub.add_parser("prio", help="re-prioritise pending pairs")
    p.add_argument("tag")
    p.add_argument("pairs", help="space-separated CAND_vs_OPP names")
    p.add_argument("prio", type=int)
    p = sub.add_parser("replays", help="copy a finished pair's replays from its node")
    p.add_argument("tag")
    p.add_argument("pair")
    p.add_argument("--dest", default=None)
    a = ap.parse_args()
    if a.cmd == "push":
        nodes, sched = pool()
        push(a.nodes or list(nodes), a.bots, a.workers or sched["push_workers"])
    elif a.cmd == "submit":
        smaps, spv, spvdir = resolve_maps(a)
        submit(a.tag, a.cands.split(), a.opps.split(), a.repeat, spv, a.prio, smaps, a.node, a.fork, a.seed_base, spvdir)
    elif a.cmd == "run":
        if a.dry_run:
            dry_run()
        else:
            with locked():
                run_pass(load())
    elif a.cmd == "collect":
        with locked():
            collect_pass(load())
    elif a.cmd == "daemon":
        daemon(a.every)
    elif a.cmd == "status":
        status(a.tag)
    elif a.cmd == "report":
        report(a.tag)
    elif a.cmd == "gauntlet":
        gauntlet(a.tag)
    elif a.cmd == "nodes":
        nodes_cmd()
    elif a.cmd == "cancel":
        cancel(a.tag, a.pair)
    elif a.cmd == "prio":
        with locked():
            reg = load()
            for j in reg["jobs"]:
                if j["tag"] == a.tag and pair_name(j) in a.pairs.split() and j["state"] == "pending":
                    j["prio"] = a.prio
                    log(f"prio {j['id']} -> {a.prio}")
            save(reg)
    elif a.cmd == "replays":
        replays(a.tag, a.pair, a.dest)


if __name__ == "__main__":
    main()
