#!/usr/bin/env python3
"""Merge several record-builder indexes into one, without duplicate game sides (the first source wins).

  python3 rl/data/merge_index.py OUT_DIR SRC [SRC ...] [--teams NAME ...]

A SRC is an index file or a folder holding index.jsonl. Rows without a `path` get SRC_DIR/replays/<game>.replay; rows
whose replay file does not exist are dropped. --teams keeps only the rows of these bot folder names
(e.g. your own arms). Writes OUT_DIR/index.jsonl.
"""
import argparse
import json
import os


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", help="the folder to write index.jsonl into")
    ap.add_argument("sources", nargs="+", help="index files or folders holding index.jsonl (the first source wins)")
    ap.add_argument("--teams", nargs="*", default=[], help="only these bot folder names (e.g. your own arms)")
    a = ap.parse_args()
    rows, per, missing = {}, {}, 0
    for src in a.sources:
        f = src if os.path.isfile(src) else os.path.join(src, "index.jsonl")
        base = os.path.dirname(os.path.abspath(f))
        if not os.path.exists(f):
            print(f"merge_index: no {f}")
            continue
        for line in open(f):
            r = json.loads(line)
            if a.teams and str(r["team"]) not in a.teams:
                continue
            r.setdefault("path", os.path.join(base, "replays", f"{r['game']}.replay"))
            if not os.path.exists(r["path"]):
                missing += 1
                continue
            key = (str(r["game"]), str(r["team"]), r.get("side"))
            if key not in rows:
                rows[key] = r
                per[str(r["team"])] = per.get(str(r["team"]), 0) + 1
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "index.jsonl"), "w") as f:
        for r in rows.values():
            f.write(json.dumps(r) + "\n")
    print(f"merge_index: {len(rows)} game sides -> {a.out} ({missing} rows without a replay file dropped; "
          + " ".join(f"{k}:{v}" for k, v in sorted(per.items())) + ")")


if __name__ == "__main__":
    main()
