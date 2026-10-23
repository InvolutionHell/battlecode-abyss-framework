#!/usr/bin/env python3
"""Crown a champion for the tuning league: the bot every island plays in every generation.

  python3 infra/tuning/champion.py EVO_ROOT BOT_DIR NAME        # crown BOT_DIR as NAME
  python3 infra/tuning/champion.py EVO_ROOT --show              # the current champion and the history

Copies the bot to <EVO_ROOT>/champion/c<n>_<NAME>, makes it the champion in current.json (cmaes_run.py's league adds it
as kind "champion"; with --league champions earlier champions stay drawable as "champion-history") and appends the
event to history.jsonl.

The policy we found worth keeping: the champion is the version you last released after it passed its census, not the
best score a tuning validator measured against a few fixed references. Such scores saturate and their noise is as
large as the differences they rank, while a released version has already beaten its predecessor head to head.
"""
import json
import os
import shutil
import sys
import time


def show(evo):
    champ = os.path.join(evo, "champion")
    cur = os.path.join(champ, "current.json")
    if not os.path.exists(cur):
        print("no champion yet")
        return
    with open(cur) as f:
        print(json.dumps(json.load(f), indent=1))
    hist = os.path.join(champ, "history.jsonl")
    if os.path.exists(hist):
        with open(hist) as f:
            for line in f:
                e = json.loads(line)
                print(f"  c{e.get('n')} {e.get('name')} (was {e.get('previous')})")


def crown(evo, src, name):
    if not os.path.isfile(os.path.join(src, "bot.toml")):
        raise SystemExit(f"{src} is not a bot folder (no bot.toml)")
    champ = os.path.join(evo, "champion")
    os.makedirs(champ, exist_ok=True)
    cur_path = os.path.join(champ, "current.json")
    cur = {"n": -1}
    if os.path.exists(cur_path):
        with open(cur_path) as f:
            cur = json.load(f)
    if cur.get("name") == name:
        print(f"champion is already {name}")
        return
    n = cur.get("n", -1) + 1
    dest = os.path.join(champ, f"c{n}_{name}")
    if not os.path.exists(dest):
        shutil.copytree(src, dest, ignore=shutil.ignore_patterns(".unswbc-build", "READY", "RETIRED"))
    new = {"name": name, "dir": os.path.basename(dest), "n": n, "at": time.strftime("%Y-%m-%d %H:%M:%S"),
           "previous": cur.get("name"), "source": os.path.abspath(src)}
    tmp = cur_path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(new, f, indent=1)
    os.replace(tmp, cur_path)
    with open(os.path.join(champ, "history.jsonl"), "a") as f:
        f.write(json.dumps(dict(new, event="crowned")) + "\n")
    print(f"champion: {name} (was {cur.get('name')})")


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return
    if len(sys.argv) == 3 and sys.argv[2] == "--show":
        show(sys.argv[1])
    elif len(sys.argv) == 4:
        crown(sys.argv[1], sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
