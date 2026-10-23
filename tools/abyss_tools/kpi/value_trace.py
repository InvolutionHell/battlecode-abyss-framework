"""VALUE-fact counts from trace replays.

The bot's map network can share VALUE facts by sonar: a dragon that knows an area with several pearl beds and none of its
team there advertises it, teammates relay it, and the policy may pull a dragon towards it. A trace build (compiled with
BC_DEBUG 1 and BC_ECON_TRACE 1, BC_MAPNET_VALUE on) appends tags to every turn's log line:

  vS        sent a VALUE fact this turn         vH<n>   heard n facts          vR<n>   relayed n facts
  vA1 / vA0 arrived at the area it was pulled to: still 2+ known beds and no teammate there (a hit) / not (a miss)
  vr<n>     n areas known; `vr<n>@x,y` when the policy has a target this turn

Per bot: traced turns, facts sent / heard / relayed, the share of turns with a target, arrivals and their hit rate. Used to
calibrate the weight of the VALUE pull (the BC_MN_VALUE_W macro) with abyss_tools.run.sweep.

  python -m abyss_tools.kpi.value_trace RUN/replays [...]
  abyss-value-trace ...
"""
import argparse
import collections
import re

from ..replay.io import base_name, initial_teams, load, replay_files

TAG = re.compile(r" (vS|vH\d+|vR\d+|vA[01]|vr\d+(?:@\S+)?)")


def count(lines):
    """one side's log lines -> tag counts"""
    c = collections.Counter()
    for text in lines:
        if not text.startswith('r'):
            continue
        tags = TAG.findall(text)
        if not any(t.startswith('vr') for t in tags):
            continue
        c['turns'] += 1
        for t in tags:
            if t == 'vS':
                c['sent'] += 1
            elif t.startswith('vH'):
                c['heard'] += int(t[2:])
            elif t.startswith('vR'):
                c['relayed'] += int(t[2:])
            elif t == 'vA1':
                c['arrivals'] += 1
                c['hits'] += 1
            elif t == 'vA0':
                c['arrivals'] += 1
            elif t.startswith('vr') and '@' in t:
                c['target'] += 1
    return c


def replay_lines(path):
    """{bot name: its dragons' log lines} of one replay"""
    r = load(path)
    team = initial_teams(r.map)
    out = {'A': [], 'B': []}
    for e in r.events:
        k = e.which()
        if k == 'dragonSplit':
            team[e.dragonSplit.childId] = 'A' if str(e.dragonSplit.team) == 'a' else 'B'
        elif k == 'dragonLog':
            s = team.get(e.dragonLog.id)
            if s in out:
                out[s].append(e.dragonLog.text)
    return {r.botA: out['A'], r.botB: out['B']}


def table(tot):
    out = ["| bot | traced turns | sent | heard | relayed | turns with a target | arrivals | hits | hit rate |",
           "|---|---|---|---|---|---|---|---|---|"]
    for bot, c in sorted(tot.items()):
        if not c['turns']:
            continue
        rate = f"{c['hits'] / c['arrivals']:.2f}" if c['arrivals'] else "-"
        out.append(f"| {bot} | {c['turns']} | {c['sent']} | {c['heard']} | {c['relayed']} | {c['target']} ({c['target'] / c['turns']:.0%}) | "
                   f"{c['arrivals']} | {c['hits']} | {rate} |")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dirs", nargs="+", help="folders of trace replays")
    a = ap.parse_args(argv)
    tot = collections.defaultdict(collections.Counter)
    for f in replay_files(a.dirs):
        for bot, lines in replay_lines(f).items():
            tot[base_name(bot)] += count(lines)
    print("\n".join(table(tot)))


if __name__ == '__main__':
    main()
