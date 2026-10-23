#!/usr/bin/env python3
"""Make an experiment arm of the released bot: BOTS/<name> = a copy of BOTS/<base> (default bots/abyss) with compile-time
switches appended to its ablation.hpp and, optionally, other network weights. The arm is a normal unswbc bot folder, so
it can be played and compared like any other (infra/lab, abyss_tools.run.arena, rl/selfplay/selfplay.py).

  python3 rl/integration/make_arm.py abyss_w05 --flags BC_R4_W=0.05                 # a heavier move prior
  python3 rl/integration/make_arm.py abyss_mynet --r4-weights RUN/f1_weights.hpp     # another move-prior net
  python3 rl/integration/make_arm.py abyss_myprior --rl-weights RUN/rl_weights.hpp   # another opening-prior net
  python3 rl/integration/make_arm.py abyss_nonet --no-nets                           # both nets off, no weight files
  python3 rl/integration/make_arm.py abyss_base                                      # a renamed exact copy: the baseline

The switches are the BC_* macros documented in rl/README.md ("Integration") and in the bot's ablation.hpp. A flag is
written as `#undef K` / `#define K V` at the end of ablation.hpp, so it overrides the released configuration.
--no-nets is the build with -DBC_RL_PRIOR=0 -DBC_R4NET=0 (written into ablation.hpp like any flag: unswbc passes no
compiler flags, and ablation.hpp sets BC_R4NET itself) and deletes rl_weights.hpp and r4_weights.hpp: the build then
proves that no network code or third-party-derived weight is compiled in (the bot plays its search alone).

Compare an arm with a renamed exact copy of its base played on the same seeds (paired games), never with the base's
absolute numbers: two identical bots do not score exactly 50% on a fixed set of seeds.
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BOTS = os.environ.get("ABYSS_BOTS") or os.path.normpath(os.path.join(HERE, "..", "..", "bots"))
NO_NETS = ["BC_RL_PRIOR=0", "BC_R4NET=0"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("name", help="the new bot folder's name")
    ap.add_argument("--base", default="abyss", help="the bot folder to copy (default abyss)")
    ap.add_argument("--bots", default=DEFAULT_BOTS, help="the unswbc workspace holding the bot folders "
                    "(default: $ABYSS_BOTS or this checkout's bots/)")
    ap.add_argument("--out-bots", default="", help="write the arm into this workspace instead (default: --bots)")
    ap.add_argument("--rl-weights", default="", help="an export_bot.py rl_weights.hpp to install (opening prior)")
    ap.add_argument("--r4-weights", default="", help="a train_f1.py f1_weights.hpp to install as r4_weights.hpp (move prior)")
    ap.add_argument("--flags", nargs="*", default=[], help="NAME=VALUE macros appended to ablation.hpp")
    ap.add_argument("--no-nets", action="store_true", help="BC_RL_PRIOR 0, BC_R4NET 0, weight files removed")
    a = ap.parse_args()
    src = os.path.join(a.bots, a.base)
    dst = os.path.join(a.out_bots or a.bots, a.name)
    if not os.path.isdir(src):
        sys.exit(f"missing {src}")
    if os.path.abspath(src) == os.path.abspath(dst):
        sys.exit("the arm needs a new name")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".unswbc-build"))
    if a.rl_weights:
        shutil.copy(a.rl_weights, os.path.join(dst, "rl_weights.hpp"))
    if a.r4_weights:
        shutil.copy(a.r4_weights, os.path.join(dst, "r4_weights.hpp"))
    flags = list(a.flags)
    if a.no_nets:
        flags = NO_NETS + flags
        for f in ("rl_weights.hpp", "r4_weights.hpp"):
            if os.path.exists(os.path.join(dst, f)):
                os.remove(os.path.join(dst, f))
    if flags:
        with open(os.path.join(dst, "ablation.hpp"), "a") as f:
            f.write(f"\n// arm {a.name} (rl/integration/make_arm.py)\n")
            for fl in flags:
                k, v = fl.split("=", 1)
                f.write(f"#undef {k}\n#define {k} {v}\n")
    with open(os.path.join(dst, "POLICY"), "w") as f:
        parts = [f"copy of {a.base}"]
        if a.rl_weights:
            parts.append(f"opening prior {os.path.basename(a.rl_weights)}")
        if a.r4_weights:
            parts.append(f"move prior {os.path.basename(a.r4_weights)}")
        if flags:
            parts.append("flags " + " ".join(flags))
        f.write(f"{a.name}\nexperiment arm: " + ", ".join(parts) + "\n")
    print(f"built {dst}: base {a.base}, flags {' '.join(flags) or '-'}")


if __name__ == "__main__":
    main()
