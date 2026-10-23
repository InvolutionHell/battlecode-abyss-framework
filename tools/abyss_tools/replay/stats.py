"""Summarise .replay files: result, deaths by reason, splits, peak army, CPU, timeouts, engine complaints.

  python -m abyss_tools.replay.stats [--logs] REPLAY [REPLAY ...]
  abyss-replay-stats ...

CPU (the highest instruction count of one dragon-turn) is only present in replays whose engine recorded the metering;
it prints as 0.0M otherwise.
"""
import argparse
import collections
import os

from .io import initial_teams, load, map_name, winner_of

REASONS = ["hitWall", "hitSelf", "hitOtherBody", "hitHeadToHead", "noValidAction"]


def summarise(path, want_logs=False):
    r = load(path)
    team = initial_teams(r.map)
    s = {
        "file": os.path.basename(path),
        "map": map_name(r.map),
        "bots": {"A": r.botA, "B": r.botB},
        "deaths": {"A": collections.Counter(), "B": collections.Counter()},
        "splits": collections.Counter(),
        "peak_units": collections.Counter(),
        "max_cpu": collections.Counter(),
        "tle": collections.Counter(),
        "engine": collections.Counter(),
        "logs": [],
        "rounds": 0,
    }
    alive = collections.Counter(team.values())
    s["peak_units"].update(alive)
    rnd = 0
    for e in r.events:
        kind = e.which()
        if kind == "roundStart":
            rnd = e.roundStart.round
        elif kind == "dragonSplit":
            sp = e.dragonSplit
            t = "A" if sp.team == "a" else "B"
            team[sp.childId] = t
            s["splits"][t] += 1
            alive[t] += 1
            s["peak_units"][t] = max(s["peak_units"][t], alive[t])
        elif kind == "dragonDeath":
            d = e.dragonDeath
            t = team.get(d.id, "?")
            s["deaths"].setdefault(t, collections.Counter())[str(d.reason)] += 1
            alive[t] -= 1
        elif kind == "dragonAction":
            a = e.dragonAction
            t = team.get(a.id, "?")
            if a.tle:
                s["tle"][t] += 1
            if a._has("instructions"):
                s["max_cpu"][t] = max(s["max_cpu"][t], a.instructions.count)
        elif kind == "engineLog":
            s["engine"][(team.get(e.engineLog.id, "?"), e.engineLog.text[:60])] += 1
        elif kind == "dragonLog" and want_logs:
            s["logs"].append((rnd, e.dragonLog.id, team.get(e.dragonLog.id, "?"), e.dragonLog.text))
    s["rounds"] = rnd + 1
    res = r.result
    s["winner"] = winner_of(r)
    s["standing"] = {
        "A": (res.teamA.dragonCount, res.teamA.longestDragon, res.teamA.totalLength),
        "B": (res.teamB.dragonCount, res.teamB.longestDragon, res.teamB.totalLength),
    }
    return s


def show(s, P=print):
    P(f"== {s['file']}  map={s['map']}  rounds={s['rounds']}  winner={s['winner']}")
    for t in "AB":
        dc, longest, total = s["standing"][t]
        deaths = ", ".join(f"{k}={v}" for k, v in sorted(s["deaths"].get(t, {}).items())) or "-"
        P(f"  {t} {s['bots'][t]:<12} alive={dc:<3} longest={longest:<3} total={total:<4} "
          f"splits={s['splits'][t]:<3} peak={s['peak_units'][t]:<3} maxcpu={s['max_cpu'][t] / 1e6:.1f}M "
          f"tle={s['tle'][t]}  deaths: {deaths}")
    for (t, text), n in s["engine"].most_common(8):
        P(f"  engine[{t}] x{n}: {text}")
    for rnd, did, t, text in s["logs"]:
        P(f"  log r{rnd} #{did}{t}: {text}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="+")
    ap.add_argument("--logs", action="store_true", help="print the bots' LOG lines too")
    args = ap.parse_args(argv)
    for path in args.replays:
        show(summarise(path, args.logs))


if __name__ == "__main__":
    main()
