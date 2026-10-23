"""abyss_tools.run.sweep: everything but the games."""
import os
import shutil

import pytest

from abyss_tools.paths import purge_wasm
from abyss_tools.run import sweep as sw


def test_parse_macro():
    assert sw.parse_macro("BC_MN_VALUE_W=0.15,0.5,1.0") == ("BC_MN_VALUE_W", ["0.15", "0.5", "1.0"])
    for bad in ("BC_X", "=1,2", "BC_X="):
        with pytest.raises(ValueError):
            sw.parse_macro(bad)


def test_arms_of():
    m = [("A", ["0", "1"]), ("B", ["x", "y", "z"])]
    assert sw.arms_of(m) == [{"A": "0"}, {"A": "1"}, {"B": "x"}, {"B": "y"}, {"B": "z"}]
    g = sw.arms_of(m, grid=True)
    assert len(g) == 6 and g[0] == {"A": "0", "B": "x"} and g[-1] == {"A": "1", "B": "z"}


def test_define_lines():
    s = sw.define_lines({"BC_X": "2", "BC_Y": "0.5"}, "kw1_c")
    assert s.splitlines() == ["// sweep arm kw1_c (abyss_tools.run.sweep)", "#undef BC_X", "#define BC_X 2", "#undef BC_Y",
                              "#define BC_Y 0.5"]


def test_build_and_purge(tmp_path):
    bots = tmp_path / "bots"
    car = bots / "carrier"
    (car / ".unswbc-build").mkdir(parents=True)
    (car / "ablation.hpp").write_text("#define BC_X 1\n")
    (car / "main.cpp").write_text("int main() {}\n")
    cache = tmp_path / "wasm"
    cache.mkdir()
    arms = sw.build("carrier", [{"BC_X": "0"}, {"BC_X": "3"}], "t", bots=str(bots), cache=str(cache))
    assert arms == [("tbase_c", {}), ("t0_c", {"BC_X": "0"}), ("t1_c", {"BC_X": "3"})]
    assert (bots / "tbase_c" / "ablation.hpp").read_text() == "#define BC_X 1\n"          # the baseline untouched
    assert (bots / "t1_c" / "ablation.hpp").read_text().endswith("#undef BC_X\n#define BC_X 3\n")
    assert not (bots / "t0_c" / ".unswbc-build").exists()                                # no stale native build
    with open(bots / "t0_c" / "ablation.hpp", "a") as f:
        f.write("// edited\n")
    sw.build("carrier", [{"BC_X": "0"}], "t", bots=str(bots), cache=str(cache))         # a rebuild starts clean
    assert "edited" not in (bots / "t0_c" / "ablation.hpp").read_text()
    for f in ("t0_c-abc.wasm", "t0_c_L-abc.wasm", "t0_c_R-x", "t10_c-abc.wasm", "other-1"):
        (cache / f).write_bytes(b"")
    assert purge_wasm("t0_c", str(cache)) == 3 and sorted(os.listdir(cache)) == ["other-1", "t10_c-abc.wasm"]
    with pytest.raises(SystemExit):
        sw.build("nope", [], "t", bots=str(bots), cache=str(cache))


def test_game_key():
    assert sw.game_key("tower_defense__kw0_c_vs_opp.replay", "kw0_c") == ("tower_defense", "A", 0)
    assert sw.game_key("tower_defense__opp_vs_kw0_c_r1.replay", "kw0_c") == ("tower_defense", "B", 1)
    assert sw.game_key("tower_defense__opp_vs_kw0_c_r1.replay.zst", "kw0_c") == ("tower_defense", "B", 1)
    assert sw.game_key("tower_defense__opp_vs_kw1_c.replay", "kw0_c") is None
    assert sw.game_key("summary.txt", "kw0_c") is None


def test_bootstrap_and_pairs():
    assert sw.bootstrap([]) == (0.0, 0.0)
    lo, hi = sw.bootstrap([1.0] * 10)
    assert lo == hi == 10.0
    lo, hi = sw.bootstrap([1.0, -1.0] * 20)
    assert lo < 0 < hi
    base = {("m", "A", 0): (1.0, None), ("m", "B", 0): (0.0, 40), ("m", "A", 1): (0.0, 200), ("x", "A", 0): (1.0, None)}
    arm = {("m", "A", 0): (1.0, 90), ("m", "B", 0): (1.0, None), ("m", "A", 1): (0.5, 300)}
    r = sw.pair_rows(base, arm)                                        # ("x", "A", 0) has no arm game: left out
    assert r["games"] == 3 and r["wins"] == 2.5 and r["base_wins"] == 1.0 and r["diff"] == 1.5
    assert (r["q100"], r["base_q100"], r["q500"], r["base_q500"]) == (1, 1, 2, 2)


SUMMARY = ("kw0_c vs opp: 2W 2L 0D 1E over 5 games\n"
           "  tower_defense.map kw0_c vs opp: win r500 (longer queen, 3 to 0)\n"
           "  tower_defense.map opp vs kw0_c: loss r332 (by elimination)\n"
           "  weakhold.map kw0_c vs opp: error r0\n"
           "  tower_defense.map kw0_c vs opp: loss r400\n"
           "  tower_defense.map opp vs kw0_c: win r500\n")


def test_summary_rows(tmp_path):
    (tmp_path / "summary.txt").write_text(SUMMARY)
    o = sw.outcomes(str(tmp_path), "kw0_c")                              # no replays: the summary, outcomes the candidate's
    assert o == {("tower_defense", "A", 0): (1.0, None), ("tower_defense", "B", 0): (0.0, None),
                 ("tower_defense", "A", 1): (0.0, None), ("tower_defense", "B", 1): (1.0, None)}
    res = tmp_path / "results"
    (res / "kw0_c_vs_opp").mkdir(parents=True)
    shutil.move(str(tmp_path / "summary.txt"), str(res / "kw0_c_vs_opp" / "summary.txt"))
    man = {"opp": "opp"}
    assert sw.arm_dir("x", "kw0_c", man, str(res)) == str(res / "kw0_c_vs_opp")
    assert len(sw.outcomes(sw.arm_dir("x", "kw0_c", man, str(res)), "kw0_c")) == 4
    assert sw.outcomes(str(tmp_path), "kw0_c") == {}


def test_outcomes_from_replays(replays, tmp_path):
    rdir = tmp_path / "abyss" / "replays"
    rdir.mkdir(parents=True)
    shutil.copy(replays["vs_starter"], rdir / "arena__abyss_vs_starter.replay")
    o = sw.outcomes(str(tmp_path / "abyss"), "abyss")
    assert o == {("arena", "A", 0): (1.0, 33)}                         # won; the candidate's queen died at r33


def test_report_only(tmp_path, capsys):
    out = tmp_path / "sw"
    for arm, summary in (("kwbase_c", SUMMARY.replace("kw0_c", "kwbase_c").replace("win r500 (longer", "loss r500 (longer")),
                         ("kw0_c", SUMMARY)):
        (out / arm).mkdir(parents=True)
        (out / arm / "summary.txt").write_text(summary)
    import json
    (out / "manifest.json").write_text(json.dumps({"carrier": "c", "opp": "opp", "prefix": "kw", "maps": [], "repeat": 2,
                                                   "arms": [["kwbase_c", {}], ["kw0_c", {"BC_X": "1"}]]}))
    assert sw.main(["--carrier", "c", "--opp", "opp", "--prefix", "kw", "--out", str(out), "--report-only", "--no-extra"]) == 0
    text = capsys.readouterr().out
    assert "| kw0_c | BC_X=1 | 4 | 2 | 1 | +1 [" in text
