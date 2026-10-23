"""Unseen-map safety check: a candidate's games on maps it has never seen must show 0 timeouts, 0 crashes and clean queen
checks.

Tournament maps are unseen, so before a release play the candidate on a set of generated maps (abyss_tools.maps.mapgen and
mapgen_cls, e.g. 25 random maps + 4-5 per map class), both sides, in the sandbox (abyss_tools.run.arena --sandbox
--maps-dir DIR), then run this check over the replays. Per replay:
  - timeouts: dragonAction.tle of the candidate's side; the highest instruction count of one of its turns (the 95M line;
    only in replays whose engine recorded the metering);
  - crashes: candidate dragons dead of noValidAction (the default action is suicide: a crash, a missing action or a
    deliberate cull - the count is reported, a non-zero one is read in the replays) and engine log lines about the
    candidate's dragons;
  - queen checks (abyss_tools.replay.rules): the parser's final standings equal the replay's result ("check") and the winner
    predicted by the round-500 rule (longer queen, then longest, then total) equals the engine's ("rule_ok").
The candidate's side is the bot named CAND, CAND_L or CAND_R (the arena copies the folders under side-specific names).
Exits 0 when the check passes.

  python -m abyss_tools.run.safety_line CAND DIR [DIR ...] [--json OUT] [-j 8]
  abyss-safety-line ...
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import sys

from ..replay.io import replay_files

CPU_LINE = 95_000_000


def one(args):
    path, cand = args
    from ..replay import rules
    from ..replay.stats import summarise
    out = {"file": os.path.basename(path)}
    try:
        s = summarise(path)
    except Exception as e:  # noqa: BLE001 - an unreadable replay is reported, not fatal
        return {**out, "error": "replay: " + repr(e)[:120]}
    side = next((t for t in "AB" if str(s["bots"][t]) in (cand, cand + "_L", cand + "_R")), None)
    if side is None:
        return {**out, "error": f"candidate {cand} not in {s['bots']}"}
    out.update(map=s["map"], side=side, rounds=s["rounds"], won=s["winner"] == side,
               tle=s["tle"][side], max_cpu=s["max_cpu"][side], nva=s["deaths"].get(side, {}).get("noValidAction", 0),
               engine=sum(n for (t, _), n in s["engine"].items() if t == side),
               engine_text=[txt for (t, txt), _ in s["engine"].most_common(3) if t == side])
    try:
        m = rules.measure(path)
        out.update(check=m.get("check"), rule_ok=m.get("rule_ok"))
    except Exception as e:  # noqa: BLE001
        out.update(check=None, rule_ok=None, queen_error=repr(e)[:120])
    return out


def verdict(rows):
    """(passed, the summary numbers) of the per-replay rows"""
    ok = [r for r in rows if "error" not in r]
    v = {
        "replays": len(rows), "unreadable": len(rows) - len(ok), "won": sum(r["won"] for r in ok),
        "bad_rule": [r["file"] for r in ok if r.get("rule_ok") is False],
        "bad_check": [r["file"] for r in ok if r.get("check") not in (True, None)],
        "tle": sum(r["tle"] for r in ok),
        "over": [r["file"] for r in ok if r["max_cpu"] > CPU_LINE],
        "nva": collections.Counter({r["file"]: r["nva"] for r in ok if r["nva"]}),
        "engine": [r["file"] for r in ok if r["engine"]],
        "max_cpu": max((r["max_cpu"] for r in ok), default=0),
    }
    passed = (not v["unreadable"] and v["tle"] == 0 and not v["over"] and not v["bad_rule"] and not v["bad_check"]
              and not v["engine"])
    return passed, v


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cand", help="the candidate bot's name")
    ap.add_argument("dirs", nargs="+", help="folders of replays")
    ap.add_argument("--json", help="write the per-replay rows and the verdict here")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    paths = replay_files(a.dirs)
    if not paths:
        print(f"no replays in {' '.join(a.dirs)}")
        return 2
    with cf.ProcessPoolExecutor(max_workers=a.jobs) as pool:
        rows = list(pool.map(one, [(p, a.cand) for p in paths]))
    passed, v = verdict(rows)
    nva = v["nva"]
    print(f"{a.cand}: {v['replays']} replays ({v['unreadable']} unreadable), won {v['won']}")
    top = v["max_cpu"]
    print(f"  timeouts {v['tle']}; " + (f"max CPU {top / 1e6:.2f}M ({len(v['over'])} games over 95M)" if top
                                       else "CPU not in these replays (see abyss_tools.run.cpu_census)"))
    print(f"  noValidAction deaths {sum(nva.values())} in {len(nva)} games (read them: a cull or a crash); engine lines in "
          f"{len(v['engine'])} games")
    print(f"  queen checks: rule_ok false {len(v['bad_rule'])}, parser check false {len(v['bad_check'])}")
    for r in rows:
        if "error" in r:
            print(f"  ERROR {r['file']}: {r['error']}")
    for f in (v["bad_rule"] + v["bad_check"] + v["over"] + v["engine"])[:10]:
        print(f"  look at {f}")
    print(f"  SAFETY CHECK: {'PASS' if passed else 'FAIL'}"
          + ("" if not nva else f" (check the {sum(nva.values())} noValidAction deaths)"))
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"cand": a.cand, "passed": passed, "rows": rows}, f)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
