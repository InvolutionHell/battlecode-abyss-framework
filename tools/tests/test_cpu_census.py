"""abyss_tools.run.cpu_census and run/flood_shim.hpp. The end-to-end check (`--scenarios selftest`, the sandbox) runs only
with ABYSS_SLOW_TESTS=1."""
import os
import subprocess

import pytest

from abyss_tools.run import cpu_census as cc
from helpers import need_cxx, need_unswbc

RUN_DIR = os.path.dirname(os.path.abspath(cc.__file__))

STREAM = """loaded wasm vs wasm in the judge's sandbox
seed 0x0000000000000007
round 0: bot 0 (team A) stdout:
MOVE W
LOG fb95 qsk
PROTOCOL 3

round 0: bot 0 (team A) points 96100000 memory 4784128
round 0: bot 1 (team B) stdout:
MOVE E
PROTOCOL 3

round 0: bot 1 (team B) points 77714936 memory 4718592
running round 2/500 --  last: 42ms  eta: 21.3s  dragons: 1 vs 1
round 1: bot 0 (team A) stdout:
SPLIT 2
PROTOCOL 3
round 1: bot 0 (team A) points 80000000 memory 4784128
\x1b[33mround 1: bot 1 (team B) exceeded CPU limit\x1b[0m
round 1: bot 1 (team B) died: did not answer
\rround 2: bot 2 (team A) points 101000 memory 5177344
round 2: bot 0 (team A) points 50000000 memory 6291456
team A wins after 3 rounds (by elimination) (1.0s)
team A points per turn: p50 1M  p99 1M  mean 1M  max 1M  (4 turns)
"""


def test_parse_stream():
    p = cc.parse_stream(STREAM.splitlines(True), r"\bfb95\b")
    assert [(t[0], t[1], t[2], t[3], t[4]) for t in p["turns"]] == [
        (0, 0, "A", 96100000, True), (0, 1, "B", 77714936, True), (1, 0, "A", 80000000, False),
        (2, 2, "A", 101000, True), (2, 0, "A", 50000000, False)], p["turns"]
    assert p["markers"] == [(0, 0, "A", "LOG fb95 qsk")], p["markers"]
    assert p["errors"] == [(1, 1, "B", "exceeded CPU limit")], p["errors"]
    assert len(p["deaths"]) == 1 and p["result"].startswith("team A wins")


def test_parse_block_without_blank_line_keeps_the_points():
    # the stdout block of round 1 has no blank line before the points line: the points line must still count
    p = cc.parse_stream(STREAM.splitlines(True), r"\bfb95\b")
    assert any(t[0] == 1 and t[1] == 0 for t in p["turns"])


def test_summarise():
    p = cc.parse_stream(STREAM.splitlines(True), r"\bfb95\b")
    rec = dict(p, deaths=0)
    a = cc.summarise(rec, "A", starters=2)
    assert a["turns"] == 4 and a["max"] == 96100000 and a["over95"] == 1 and a["over85"] == 1
    assert a["markers"] == 1 and a["timeouts"] == 0
    assert a["first_r0_n"] == 1 and a["first_r0_max"] == 96100000
    assert a["newborn_n"] == 1 and a["newborn_max"] == 101000          # bot 2 >= 2 starters: a newborn
    assert a["procs_round"] == 2                                        # round 2: bots 2 and 0
    b = cc.summarise(rec, "B", starters=2)
    assert b["timeouts"] == 1 and b["turns"] == 1


def test_memory_column():
    p = cc.parse_stream(STREAM.splitlines(True), r"\bfb95\b")
    assert [t[5] for t in p["turns"]] == [4784128, 4718592, 4784128, 5177344, 6291456]
    a = cc.summarise(dict(p, deaths=0), "A", starters=2)
    assert a["mem_max"] == 6291456                               # 6 MiB, the largest of team A
    assert abs(a["mem_p99"] - cc.pct([4784128, 4784128, 5177344, 6291456], 99)) < 1e-6
    b = cc.summarise(dict(p, deaths=0), "B", starters=2)
    assert b["mem_max"] == 4718592


def gate_lines(records):
    out = []
    cc.report(records, out.append)
    return [line for line in out if line.startswith("- **")]


def rec_of(turns, bot="x"):
    return {"name": "g", "scenario": "big", "bot": bot, "map": "m.map", "seed": 1, "a": "x_A", "b": "x_B", "measure": "A",
            "flood": 0, "secs": 1.0, "exit": 0, "result": "r", "turns": turns, "errors": [], "deaths": 0, "markers": [],
            "replay": "", "peak_alive": {"A": 1}, "inbox_real": {"A": 0}}


def test_memory_gate_and_old_records():
    assert cc.MEM_LIMIT == 50_331_648, "the sandbox's MAX_MEMORY_PAGES = 768 pages of 64 KiB"
    ok = rec_of([(0, 0, "A", 80_000_000, True, 5 * cc.MIB)])
    assert "PASS" in gate_lines([ok])[0] and "5.0 MiB" in gate_lines([ok])[0]
    full = rec_of([(0, 0, "A", 80_000_000, True, cc.MEM_LIMIT)])
    assert "FAIL" in gate_lines([full])[0], "a process at the 768-page limit fails the check"
    old = rec_of([(0, 0, "A", 80_000_000, True)])                 # a record without the memory column
    line = gate_lines([old])[0]
    assert "PASS" in line and "not recorded" in line
    out = []
    cc.report([old], out.append)
    assert any("| - / - |" in x for x in out), "an old record shows no memory instead of failing"


FORCED = """round 3: bot 0 (team A) stdout:
LOG fb95 policy
MOVE N
LOG burn policy
PROTOCOL 3

round 3: bot 0 (team A) points 97800000 memory 1
round 3: bot 2 (team A) stdout:
LOG burn rays
MOVE E
PROTOCOL 3

round 3: bot 2 (team A) points 97700000 memory 1
"""


def test_forced_pairs_with_markers():
    p = cc.parse_stream(FORCED.splitlines(True), r"\bfb95\b", r"\bburn\b")
    assert [(x[0], x[1]) for x in p["forced"]] == [(3, 0), (3, 2)]
    assert [(x[0], x[1]) for x in p["markers"]] == [(3, 0)]
    rec = rec_of(p["turns"])
    rec.update(forced=p["forced"], markers=p["markers"], defines=["BC_BURN=1"])
    out = []
    cc.report([rec], out.append)
    line = [x for x in out if "forced turns" in x][0]
    assert "forced turns 2" in line and "missing 1" in line and "FAIL" in line, line
    rec["markers"] = rec["markers"] + [(3, 2, "A", "LOG fb95 rays")]
    out = []
    cc.report([rec], out.append)
    line = [x for x in out if "forced turns" in x][0]
    assert "missing 0" in line and "PASS" in line and "97.80M" in line, line
    assert cc.parse_stream(FORCED.splitlines(True), r"\bfb95\b")["forced"] == []   # no --forced: nothing collected


def test_defines_go_into_the_copy_only(tmp_path):
    src = tmp_path / "bots" / "fake"
    src.mkdir(parents=True)
    (src / "main.cpp").write_text("int main(){}\n")
    dst = cc.copy_bot(str(src), "fake_x", str(tmp_path), [("BC_FALLBACK95", "1"), ("BC_TRACE", "1")])
    lines = open(os.path.join(dst, "main.cpp")).read().splitlines()
    assert lines[:3] == ["// cpu census copy fake_x", "#define BC_FALLBACK95 1", "#define BC_TRACE 1"], lines[:3]
    assert (src / "main.cpp").read_text() == "int main(){}\n"


def test_checkerboard_and_map():
    d = cc.checkerboard(64, 64, 64, 3, 64, 2, 4, 8)
    assert len(d) == 128 and [t for t, b in d] == [0, 1] * 64
    cells = [c for t, b in d for c in b]
    assert len(cells) == len(set(cells)), "bodies overlap"
    assert all(len(b) == (3 if t == 0 else 2) for t, b in d)
    for t, b in d:                                   # contiguous, head first, hanging south
        assert all(b[k + 1] == (b[k][0], b[k][1] + 1) for k in range(len(b) - 1))
    beds = cc.lattice_beds(64, 64)
    bset = {(x, y) for x, y, lo, hi in beds}
    assert len(bset) == 256 and all((63 - x, 63 - y) in bset for x, y in bset), "beds must match SYMMETRY xy"
    assert not bset & set(cells), "a bed under a starting dragon"
    lines = cc.make_map(64, 64, d, beds).splitlines()
    assert lines[0] == "MAP 64 64" and sum(1 for x in lines if x.startswith("TILE ")) == 4096
    assert sum(1 for x in lines if x.startswith("DRAGON ")) == 128
    d2 = cc.checkerboard(64, 64, 32, 8, 32, 2, 4, 16)
    assert len(d2) == 64 and all(len(b) == 8 for t, b in d2 if t == 0)
    with pytest.raises(ValueError):
        cc.checkerboard(16, 16, 64, 3, 64, 2, 4, 4)  # an army too big for the map


def test_flooded_copy_patches_only_the_copy(tmp_path):
    bots = tmp_path / "bots"
    (bots / "fake").mkdir(parents=True)
    (bots / "fake" / "io.hpp").write_text("#pragma once\nchar* r(char* b){return std::fgets(b, 9, stdin);}\n")
    (bots / "fake" / "main.cpp").write_text('#include "io.hpp"\nint main(){}\n')
    (bots / "nofgets").mkdir()
    (bots / "nofgets" / "main.cpp").write_text("int main(){}\n")
    work = tmp_path / "work"
    work.mkdir()
    cc.flooded_copy(str(bots / "fake"), "fake_fl8", str(work), 8)
    io_hpp = (work / "fake_fl8" / "io.hpp").read_text()
    assert io_hpp.startswith('#include "flood_shim.hpp"') and "floodshim::fgets(b, 9, stdin)" in io_hpp
    assert "std::fgets" not in io_hpp
    assert "#define FLOOD_N 8" in (work / "fake_fl8" / "flood_shim.hpp").read_text()
    assert (work / "fake_fl8" / "main.cpp").read_text().startswith("// cpu census copy fake_fl8")
    assert "std::fgets" in (bots / "fake" / "io.hpp").read_text()      # the original is untouched
    with pytest.raises(SystemExit):
        cc.flooded_copy(str(bots / "nofgets"), "nofgets_fl8", str(work), 8)


def test_census_bots_are_packaged():
    for b in ("cpuhog", "flooder"):
        assert os.path.exists(os.path.join(cc.CENSUS_BOTS, b, "main.cpp"))
        assert os.path.exists(os.path.join(cc.CENSUS_BOTS, b, "bot.toml"))
    assert os.path.exists(cc.FLOOD_SHIM)


SHIM_TEST = r"""
#define FLOOD_N 8
#define FLOOD_MODE 2
#include "flood_shim.hpp"
#include <cstdio>
int main(int, char** argv) {
    FILE* f = std::fopen(argv[1], "r");
    char buf[512];
    while (floodshim::fgets(buf, sizeof buf, f)) std::fputs(buf, stdout);
}
"""

SHIM_IN = "ROUND 3\nDIR N\nLENGTH 2\nUNIT_COUNT 1\nNUM_MSGS 2\n11\n22\nTILE\nROUND 4\nNUM_MSGS 10\n" \
    + "".join(f"{k}\n" for k in range(10)) + "TILE\nROUND 5\nNUM_MSGS 0\nTILE\n"


def test_flood_shim(tmp_path):
    cxx = need_cxx()
    src = tmp_path / "t.cpp"
    src.write_text(SHIM_TEST)
    exe = tmp_path / "t"
    subprocess.run([cxx, "-std=c++17", "-O1", "-I", RUN_DIR, str(src), "-o", str(exe)], check=True)
    inp = tmp_path / "in.txt"
    inp.write_text(SHIM_IN)
    out = subprocess.run([str(exe), str(inp)], capture_output=True, text=True, check=True).stdout.splitlines()
    # round 3: 2 real values -> NUM_MSGS 8, 6 synthetic values first, then the real 11, 22
    i = out.index("ROUND 3")
    assert out[i + 4] == "NUM_MSGS 8", out[i:i + 6]
    syn = out[i + 5:i + 11]
    assert all(v.isdigit() for v in syn) and out[i + 11:i + 13] == ["11", "22"] and out[i + 13] == "TILE", out[i:i + 14]
    # round 4: 10 real values >= 8: nothing added
    j = out.index("ROUND 4")
    assert out[j + 1] == "NUM_MSGS 10" and out[j + 2:j + 12] == [str(k) for k in range(10)] and out[j + 12] == "TILE"
    # round 5: an empty inbox becomes 8 values, half of them replays of values seen before (11, 22, 0..9)
    k = out.index("ROUND 5")
    assert out[k + 1] == "NUM_MSGS 8" and out[k + 10] == "TILE", out[k:k + 11]
    vals = out[k + 2:k + 10]
    seen = {"11", "22"} | {str(x) for x in range(10)}
    assert sum(1 for v in vals if v in seen) == 4, vals


@pytest.mark.skipif(os.environ.get("ABYSS_SLOW_TESTS") != "1", reason="sandbox games: set ABYSS_SLOW_TESTS=1")
def test_selftest_end_to_end(tmp_path):
    need_unswbc()
    out = tmp_path / "out"
    rc = cc.main(["--scenarios", "selftest", "--bots", str(tmp_path), "--out", str(out), "--max-games", "8"])
    assert rc == 0 and "selftest: **PASS**" in (out / "census.md").read_text()
