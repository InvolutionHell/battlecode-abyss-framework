#!/usr/bin/env python3
"""How often the move prior of the non-queen dragons (BC_R4NET) changes the search's best first step, and what the
net-gated free steps do, from the trace tags of a trace build of bots/abyss (BC_DEBUG 1 and BC_ECON_TRACE 1 appended to
ablation.hpp, e.g. with rl/integration/make_arm.py --flags; games played with logs kept in the replays):

  " r4="        the prior kept the search's best step        " r4!<n>"   it changed it; n / 100 = search value given up
  " r4p<pct><why>"  the net's P(multi-step | the step) on turns where free steps were possible, and the step's kind
  " r4s<steps>"     a free-step move was played               " r4x<code>"  why the free-step module refused
  " r4cull"         a cull (BC_R4_CULL, off in the released bot)

  python3 rl/eval/prior_flips.py REPLAY [REPLAY ...]
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data"))
from replay_src import load  # noqa: E402

TAG = re.compile(r"^r(\d+) .* r4([=!])(\d*)")
SPR = re.compile(r"^r(\d+) .* r4s(\d+)")
PRB = re.compile(r" r4p(\d+)(\w{3})")
REF = re.compile(r" r4x(\d+)")
REFUSALS = ("1 free steps < 2, 2 split / no destination / already a sprint / eats, 3 the step's kind, 4 body unknown, "
            "5 portal edge, 6 no legal second step, 7 landing margin, 8 room, 9 the queen's ring / last exit, 10 the clock")


def main(paths):
    by = collections.defaultdict(lambda: [0, 0])
    loss, spr, prob, why, ref = (collections.Counter() for _ in range(5))
    for path in paths:
        rep = load(path)
        n = 0
        for e in rep.events:
            if e.which() != "dragonLog":
                continue
            n += 1
            text = e.dragonLog.text
            mp = PRB.search(text)
            if mp:
                pr = int(mp.group(1))
                prob["p>=50" if pr >= 50 else "p30-49" if pr >= 30 else "p10-29" if pr >= 10 else "p<10"] += 1
                why[mp.group(2)] += 1
            mr = REF.search(text)
            if mr:
                ref[int(mr.group(1))] += 1
            ms = SPR.match(text)
            if ms:
                spr[(min(int(ms.group(1)) // 100, 4), int(ms.group(2)))] += 1
            m = TAG.match(text)
            if m:
                r = int(m.group(1))
                by[min(r // 100, 4)][0 if m.group(2) == "=" else 1] += 1
                if m.group(2) == "!":
                    v = int(m.group(3) or 0)
                    loss["tie (0)" if v == 0 else "<0.1" if v < 10 else "<0.3" if v < 30 else "<1" if v < 100 else ">=1"] += 1
        print(f"{os.path.basename(path)}: {n} log lines" + ("" if n else " (no logs: a trace build with logs kept?)"))
    tot = [0, 0]
    for k in sorted(by):
        same, flip = by[k]
        tot[0] += same
        tot[1] += flip
        print(f"  r{k * 100}-{k * 100 + 99}: {same + flip} prior turns, flips {flip} ({100.0 * flip / max(1, same + flip):.1f}%)")
    if prob:
        print("  net's P(multi-step | the step) on turns with free steps: " + ", ".join(f"{k} {v}" for k, v in sorted(prob.items()))
              + " | the step's kind: " + ", ".join(f"{k} {v}" for k, v in why.most_common(8)))
    if ref:
        print(f"  refusals of the free-step module ({REFUSALS}): " + ", ".join(f"{k}: {v}" for k, v in sorted(ref.items())))
    if spr:
        print("  free-step moves by round bucket: " + ", ".join(f"r{k * 100}+ {n}x{st}-step" for (k, st), n in sorted(spr.items())))
    print("  search value given up on a flip: " + ", ".join(f"{k} {v}" for k, v in sorted(loss.items())))
    print(f"  all: {sum(tot)} prior turns, flips {tot[1]} ({100.0 * tot[1] / max(1, sum(tot)):.1f}%)")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0 if len(sys.argv) > 1 else 2)
    main(sys.argv[1:])
