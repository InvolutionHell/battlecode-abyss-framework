#!/usr/bin/env python3
"""Check that the training featurizer is the bot's featurizer.

The nets only work if every training record is computed exactly as the bot computes its input in play. Training reads
rl/featurize/rl_features.hpp (through featurize_*.cpp); the bot reads bots/abyss/rl_features.hpp. This script compares:

  1. rl_features.hpp of both folders, token for token with comments removed (must match; byte identity is reported);
  2. the protocol structs and parser the featurizer depends on (VisTile, Part, EdgeObs, Init, Turn, Reader, parseEdge,
     readInit, readTurn) in rl/featurize/io.hpp and the bot's io.hpp, with comments and the bot's `#if ...` blocks
     (CPU-clock bookkeeping that does not touch the parsed values) removed.

  python3 rl/featurize/check_features.py [--bot bots/abyss]

Exit status 0 when both match, 1 otherwise (the first differing tokens are printed).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BLOCKS = ["struct VisTile", "struct Part", "struct EdgeObs", "struct Init", "struct Turn", "class Reader",
          "inline EdgeObs parseEdge", "inline bool readInit", "inline bool readTurn"]


def strip_comments(src):
    """C++ source without comments; string and char literals are kept intact."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_if_blocks(src):
    """Drop every #if / #ifdef / #ifndef ... #endif region (nesting aware); used on function bodies only."""
    out, depth = [], 0
    for line in src.splitlines():
        s = line.strip()
        if re.match(r"#\s*if", s):
            depth += 1
            continue
        if re.match(r"#\s*endif", s) and depth:
            depth -= 1
            continue
        if not depth:
            out.append(line)
    return "\n".join(out)


def tokens(src):
    return re.findall(r"\w+|[^\s\w]", src)


def block(src, head):
    """The text of the declaration starting with `head` up to its matching closing brace (and a trailing ';')."""
    i = src.find(head)
    if i < 0:
        return None
    j = src.find("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                end = k + 1
                if src[end:end + 1] == ";":
                    end += 1
                return src[i:end]
    return None


def first_diff(a, b):
    for k, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return k
    return min(len(a), len(b))


def report(name, ta, tb):
    if ta == tb:
        return True
    k = first_diff(ta, tb)
    print(f"MISMATCH {name}: token {k}: training ... {' '.join(ta[max(0, k - 8):k + 8])} ...")
    print(f"{' ' * (len(name) + 10)}bot      ... {' '.join(tb[max(0, k - 8):k + 8])} ...")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bot", default=os.path.join(REPO, "bots", "abyss"), help="the bot folder (default bots/abyss)")
    ap.add_argument("--featurize", default=HERE, help="the training featurizer folder (default rl/featurize)")
    a = ap.parse_args()
    ok = True
    fa, fb = os.path.join(a.featurize, "rl_features.hpp"), os.path.join(a.bot, "rl_features.hpp")
    for p in (fa, fb):
        if not os.path.exists(p):
            sys.exit(f"missing {p}")
    sa, sb = open(fa).read(), open(fb).read()
    same = report("rl_features.hpp", tokens(strip_comments(sa)), tokens(strip_comments(sb)))
    ok &= same
    if same:
        print("rl_features.hpp: " + ("byte-identical" if sa == sb else "identical code (comments differ)"))
    ia = strip_comments(open(os.path.join(a.featurize, "io.hpp")).read())
    ib = strip_comments(open(os.path.join(a.bot, "io.hpp")).read())
    io_ok = True
    for head in BLOCKS:
        ba, bb = block(ia, head), block(ib, head)
        if ba is None or bb is None:
            print(f"MISSING {head} in {'the training io.hpp' if ba is None else 'the bot io.hpp'}")
            io_ok = False
            continue
        io_ok &= report(head, tokens(strip_if_blocks(ba)), tokens(strip_if_blocks(bb)))
    print("io.hpp protocol structs and parser: " + ("identical" if io_ok else "see the mismatches above"))
    ok &= io_ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
