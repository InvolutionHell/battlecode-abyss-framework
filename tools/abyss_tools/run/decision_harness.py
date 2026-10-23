"""Why did the bot do that? Replay one team's turns of a recorded game through a native build of a bot and collect its LOG
lines: the reason for every move, given exactly the inputs the dragon had in that game.

Every dragon of the team gets its own process (as in the engine), started at its first turn with the handshake (ID, TEAM,
MAP, UNIT_LIMIT); each turn it is sent the round block the engine sent it (rebuilt by abyss_tools.replay.state) and its
output is read up to ENDTURN. Every turn from round 0 is played (each process needs the whole history); only rounds
--from .. --to are printed. Timing differs from the judge (native code, not the metered wasm), so an anytime search may
reach another depth; the reasons are what this is for.

  python -m abyss_tools.run.decision_harness GAME.replay TEAM --build BOT_DIR [--define BC_TURN_BUDGET=0.05] [--from R] [--to R]
  python -m abyss_tools.run.decision_harness GAME.replay TEAM --exe ./bot [--actions]
  abyss-decision-harness ...

--build copies BOT_DIR to a temporary folder, puts `#define BC_DEBUG 1` (unless --no-debug) and every --define at the top of
its main.cpp, and compiles it natively (c++ -std=c++20 -O2). Output (tab-separated): round, dragon id, length (the `L=`
token of the LOG line), reason (the LOG line's second token), head x,y before the move, the whole LOG line. With --actions,
one line per turn instead: round, dragon id, the recorded action, the bot's action, and at the end the share of turns where
they agree - for a deterministic bot this checks that the rebuilt inputs are exact.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from ..replay import state
from ..replay.io import load


def build(bot_dir, defines=(), debug=True, cxx=None, work=None):
    """a native build of the bot folder (the folder itself is not changed); returns the executable's path"""
    cxx = cxx or os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or "c++"
    work = work or tempfile.mkdtemp(prefix="harness-")
    src = os.path.join(work, "src")
    if os.path.exists(src):
        shutil.rmtree(src)
    shutil.copytree(bot_dir, src, ignore=shutil.ignore_patterns(".unswbc-build"))
    main_cpp = os.path.join(src, "main.cpp")
    with open(main_cpp) as fh:
        text = fh.read()
    head = ("#define BC_DEBUG 1\n" if debug else "") + "".join(f"#define {k} {v}\n" for k, v in defines)
    with open(main_cpp, "w") as fh:
        fh.write(head + text)
    exe = os.path.join(work, "bot")
    subprocess.run([cxx, "-std=c++20", "-O2", "-w", "-I", src, main_cpp, "-o", exe], check=True)
    return exe


def normalise(action):
    """'MOVE N E' / 'MOVE NE' -> 'MOVE NE'; 'SPLIT 3'; None -> 'NONE'"""
    if not action:
        return "NONE"
    p = action.split()
    if p[0] == "MOVE":
        return "MOVE " + "".join(p[1:])
    return " ".join(p)


class Dragon:
    """one dragon's process"""

    def __init__(self, exe, did, team, board):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, bufsize=1)
        self.alive = True
        self.send(f"ID {did}\nTEAM {team}\nMAP {board.w} {board.h}\nUNIT_LIMIT {board.unit_limit}\n")

    def send(self, text):
        try:
            self.p.stdin.write(text)
            self.p.stdin.flush()
        except (BrokenPipeError, OSError):
            self.alive = False

    def turn(self, block):
        """(LOG lines, action line or None) of one turn"""
        self.send("\n".join(block) + "\n")
        logs, action = [], None
        while self.alive:
            line = self.p.stdout.readline()
            if not line:
                self.alive = False
                break
            line = line.strip()
            if line.startswith("LOG "):
                logs.append(line[4:])
            elif line.startswith(("MOVE", "SPLIT")):
                action = line
            elif line == "ENDTURN":
                break
        return logs, action

    def close(self):
        try:
            self.p.stdin.close()
        except OSError:
            pass
        self.p.kill()
        self.p.wait()


def run(replay_path, team, exe, r0=0, r1=10 ** 9, actions=False, out=sys.stdout):
    """replay the team's turns through exe; returns (turns compared, turns that agree)"""
    rep = load(replay_path)
    board = state.Board(rep.map)
    procs = {}
    n = agree = 0
    try:
        for me, block, label in state.turns(rep, only_team=team):
            rnd = next((int(x.split()[1]) for x in block if x.startswith("ROUND ")), None)
            if me.id not in procs:
                procs[me.id] = Dragon(exe, me.id, team, board)
            d = procs[me.id]
            if not d.alive:
                continue
            logs, action = d.turn(block)
            if rnd is not None and not (r0 <= rnd <= r1):
                continue
            if actions:
                mine = normalise(action)
                n += 1
                agree += mine == label
                out.write(f"{rnd}\t{me.id}\t{label}\t{mine}\n")
                continue
            head = me.body[0] if me.body else ("?", "?")
            for lg in logs:
                parts = lg.split()
                why = parts[1] if len(parts) > 1 else "?"
                length = next((x[2:] for x in parts if x.startswith("L=")), "?")
                out.write(f"{rnd}\t{me.id}\t{length}\t{why}\t{head[0]},{head[1]}\t{lg}\n")
    finally:
        for d in procs.values():
            d.close()
    return n, agree


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("replay")
    ap.add_argument("team", choices=["A", "B"])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--exe", help="a native build of the bot")
    g.add_argument("--build", metavar="BOT_DIR", help="build this bot folder natively first")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VAL", help="with --build: an extra #define")
    ap.add_argument("--no-debug", action="store_true", help="with --build: do not define BC_DEBUG")
    ap.add_argument("--cxx", help="with --build: the C++ compiler (default: $CXX, clang++, g++)")
    ap.add_argument("--from", dest="r0", type=int, default=0)
    ap.add_argument("--to", dest="r1", type=int, default=10 ** 9)
    ap.add_argument("--actions", action="store_true", help="compare the bot's actions with the recorded ones")
    a = ap.parse_args(argv)
    exe = a.exe
    work = None
    if a.build:
        work = tempfile.mkdtemp(prefix="harness-")
        defines = [tuple(d.split("=", 1)) if "=" in d else (d, "1") for d in a.define]
        exe = build(a.build, defines, not a.no_debug, a.cxx, work)
    try:
        n, agree = run(a.replay, a.team, os.path.abspath(exe), a.r0, a.r1, a.actions)
    finally:
        if work:
            shutil.rmtree(work, ignore_errors=True)
    if a.actions:
        print(f"{agree} / {n} turns agree", file=sys.stderr)
        return 0 if n and agree == n else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
