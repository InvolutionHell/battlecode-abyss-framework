"""Parallel sandbox games for the parameter tuner, one build per bot.

Every bot of a batch is staged once under a unique folder name (unswbc's wasm cache is keyed on the folder name and
the .c/.cpp contents, while the knobs live in headers), with an optional smaller search budget written to
eval_budget.hpp so tuning games run faster. The batch has its own XDG_CACHE_HOME, deleted with the batch.

A game's result is the winner from unswbc's output plus, when the replay can be read (tools/abyss_tools and pycapnp
available), the final length margins used for the small tie-break term of the fitness.
"""
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RESULT = re.compile(r"team ([AB]) wins after (\d+) rounds|draw after (\d+) rounds", re.I)


def unswbc():
    return os.environ.get("UNSWBC") or shutil.which("unswbc") or os.path.expanduser("~/.local/bin/unswbc")


def replay_loader():
    """abyss_tools' replay reader, or None (no margins then)."""
    sys.path.insert(0, os.path.join(REPO, "tools"))
    try:
        from abyss_tools.replay.io import load
        import capnp  # noqa: F401
        return load
    except Exception:  # noqa: BLE001 - optional dependency
        return None


def run_retry(cmd, tries=6, **kw):
    """subprocess.run that waits out a toolkit upgrade (the executable briefly missing while it is replaced)."""
    for i in range(tries):
        try:
            return subprocess.run(cmd, **kw)
        except OSError:
            if i == tries - 1:
                raise
            time.sleep(10)
            cmd = [unswbc()] + list(cmd[1:])


def stage(src, dest, budget=None, tuned_header=None):
    """Copy a bot folder; write its search budget and tuned header."""
    shutil.copytree(src, dest, ignore=shutil.ignore_patterns(".unswbc-build", "READY", "RETIRED"))
    common = os.path.join(dest, "common.hpp")
    if budget and os.path.exists(common):
        text = open(common).read()
        if "BC_TURN_BUDGET" in text:   # bots without a search budget are left alone
            line = f"#define BC_TURN_BUDGET {budget}\n"
            if '__has_include("eval_budget.hpp")' in text:
                with open(os.path.join(dest, "eval_budget.hpp"), "w") as f:
                    f.write("#pragma once\n" + line)
            else:
                with open(common, "w") as f:
                    f.write(line + text)
    if tuned_header is not None:
        with open(os.path.join(dest, "tuned_params.hpp"), "w") as f:
            f.write(tuned_header)


class Batch:
    def __init__(self, bots_dir, jobs, budget=None, root=None, timeout=7200):
        self.root = tempfile.mkdtemp(prefix="tune-", dir=root)
        self.bots_dir = bots_dir
        self.maps_dir = os.path.join(bots_dir, "maps")
        self.jobs = jobs
        self.budget = budget
        self.timeout = timeout
        self.env = dict(os.environ, XDG_CACHE_HOME=os.path.join(self.root, "cache"))
        self.bots = {}
        self.load = replay_loader()

    def add_bot(self, key, src, tuned_header=None):
        name = re.sub(r"[^A-Za-z0-9_]", "_", key)
        stage(src, os.path.join(self.root, name), self.budget, tuned_header)
        self.bots[key] = name
        return name

    def build_all(self, build_map="default_small"):
        """Compile each bot once (one sandbox game against a starter copy) before the parallel games, which would
        otherwise race on building the same folder."""
        stub = os.path.join(self.root, "starter_stub")
        if not os.path.exists(stub):
            shutil.copytree(os.path.join(self.bots_dir, "starter"), stub, ignore=shutil.ignore_patterns(".unswbc-build"))
        mp = os.path.join(self.maps_dir, build_map + ".map")

        def build(name):
            run_retry([unswbc(), "run", mp, name, "starter_stub", "--sandbox", "--no-replay"], cwd=self.root,
                      env=self.env, capture_output=True, timeout=1800)
        with cf.ThreadPoolExecutor(self.jobs) as pool:
            list(pool.map(build, list(self.bots.values())))

    def play(self, a_key, b_key, map_name, seed=None):
        """(winner 'A' | 'B' | None for a draw, longest-dragon margin A - B, total-length margin A - B); winner "E"
        when the game did not finish (engine error, a killed process): callers drop those."""
        a, b = self.bots[a_key], self.bots[b_key]
        replay = os.path.join(self.root, f"{a}__{b}__{map_name.replace('/', '_')}__{seed or 'r'}.replay")
        cmd = [unswbc(), "run", os.path.join(self.maps_dir, map_name + ".map"), a, b, "--sandbox", "--no-logs",
               "--no-draw", "--no-indicator"]
        cmd += ["-o", replay] if self.load else ["--no-replay"]
        cmd += ["--seed", str(seed)] if seed is not None else []
        try:
            p = run_retry(cmd, cwd=self.root, env=self.env, capture_output=True, text=True, timeout=self.timeout)
        except (OSError, subprocess.TimeoutExpired):
            return "E", 0, 0
        m = RESULT.search(p.stdout + p.stderr)
        if not m:
            return "E", 0, 0
        winner = m.group(1) if m.group(1) else None
        longest = total = 0
        if self.load and os.path.exists(replay):
            try:
                r = self.load(replay).result
                longest = r.teamA.longestDragon - r.teamB.longestDragon
                total = r.teamA.totalLength - r.teamB.totalLength
            except Exception:  # noqa: BLE001 - margins are optional
                pass
            finally:
                os.remove(replay)
        return winner, longest, total

    def run(self, games):
        """games: [(a_key, b_key, map[, seed])] -> results in the same order"""
        with cf.ThreadPoolExecutor(self.jobs) as pool:
            return list(pool.map(lambda g: self.play(*g), games))

    def close(self):
        shutil.rmtree(self.root, ignore_errors=True)
