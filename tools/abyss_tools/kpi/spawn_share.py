"""The share of fresh pearl spawns nearer to a bot, per bot and map, from folders of replays.

A fresh spawn is a pearl appearing on a tile that held no dead body in the last 60 rounds; it is nearer to the side whose
nearest head is fewer terrain steps away (bodies ignored, BFS <= 30) - the `spawn` counters of abyss_tools.replay.rules.
Shares are pooled (spawns nearer to the bot / all spawns of its games, ties in the denominator), per phase r0-199 and
r200-499. A bot that reads the map well positions its heads where pearls will appear.

Arms that share paired seeds (abyss_tools.run.arena) are compared by passing each arm's replay folder; every bot is
labelled with its arm (the folder's name, or its parent's when it is called `replays`), so a common opponent is kept apart
per arm.

  python -m abyss_tools.kpi.spawn_share RUN_CANDIDATE/replays RUN_BASELINE/replays [-j 8]
  abyss-spawn-share ...
"""
import argparse
import collections
import concurrent.futures as cf

from ..replay.io import arm_label, base_name, load, map_stem, replay_files

PHASES = ("r0-199", "r200-499")


def rows_of(name_a, name_b, spawn, map_name):
    """one replay's `spawn` counters -> a row per side: (map, bot, {phase: (nearer to it, all)})"""
    out = []
    for side, name in (("A", name_a), ("B", name_b)):
        per = {}
        for ph in PHASES:
            c = spawn.get(ph) or {}
            per[ph] = (c.get(f"closer {side}", 0), c.get("n", 0))
        out.append((map_name, base_name(name), per))
    return out


def job(path):
    from ..replay import rules
    rep = load(path)
    m = rules.measure(path)
    return rows_of(rep.botA, rep.botB, m.get("spawn") or {}, map_stem(path))


def table(rows):
    """rows -> markdown lines: per (map, bot), then per bot over all maps"""
    by = collections.defaultdict(lambda: {"games": 0, **{ph: [0, 0] for ph in PHASES}})
    for mp, bot, per in rows:
        for key in ((mp, bot), ("all", bot)):
            t = by[key]
            t["games"] += 1
            for ph in PHASES:
                t[ph][0] += per[ph][0]
                t[ph][1] += per[ph][1]

    def share(c):
        return f"{c[0] / c[1]:.3f} ({c[0]} / {c[1]})" if c[1] else "-"

    out = ["| map | bot | games | spawns nearer to it, r0-199 | r200-499 |", "|---|---|---|---|---|"]
    keys = sorted(k for k in by if k[0] != "all") + sorted(k for k in by if k[0] == "all")
    for mp, bot in keys:
        t = by[(mp, bot)]
        label = "**all**" if mp == "all" else mp
        out.append(f"| {label} | {bot} | {t['games']} | {share(t['r0-199'])} | {share(t['r200-499'])} |")
    return out


def collect(dirs, jobs=8):
    """[(map, 'bot [arm]', per)] over the replay folders"""
    rows = []
    with cf.ProcessPoolExecutor(jobs) as ex:
        for d in dirs:
            arm = arm_label(d)
            for r in ex.map(job, replay_files(d)):
                rows.extend((mp, f"{bot} [{arm}]", per) for mp, bot, per in r)
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args(argv)
    n = sum(len(replay_files(d)) for d in a.dirs)
    rows = collect(a.dirs, a.jobs)
    print(f"{n} replays")
    print("\n".join(table(rows)))


if __name__ == "__main__":
    main()
