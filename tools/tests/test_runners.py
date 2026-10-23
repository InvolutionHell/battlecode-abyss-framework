"""abyss_tools.run: the arena's seeds and result parsing, the safety check, the decision harness; the games themselves only
when `unswbc` (and for the harness a C++ compiler) is installed."""
import os
import shutil


from abyss_tools.replay.io import load
from abyss_tools.run import arena, decision_harness as dh, safety_line
from helpers import make_starter, need_cxx, need_unswbc, unswbc_env


# --------------------------------------------------------------------------------------------------------------- arena

def test_seed_of_is_paired():
    a = arena.seed_of("/x/maps/devil.map", "cand-A", 0)
    assert a == arena.seed_of("/other/devil.map", "cand-A", 0)          # the slot, not the path
    assert a != arena.seed_of("/x/maps/devil.map", "cand-B", 0) != arena.seed_of("/x/maps/devil.map", "cand-A", 1)
    assert arena.seed_of("/x/devil.map", "cand-A", 0, base=1) != a and 0 <= a < 2 ** 48


def test_replay_name():
    assert arena.replay_name("/m/devil.map", "a", "b", 0) == "devil__a_vs_b.replay"
    assert arena.replay_name("/m/devil.map", "a", "b", 2) == "devil__a_vs_b_r2.replay"


def test_parse_result():
    out = "seed 0x1\nteam B wins after 500 rounds (longer queen, 12 to 5) (3.1s)\n"
    assert arena.parse_result(out, "cand", "opp", "cand") == ("loss", 500, "longer queen, 12 to 5")
    assert arena.parse_result(out, "opp", "cand", "cand") == ("win", 500, "longer queen, 12 to 5")
    assert arena.parse_result("team A wins after 45 rounds (by elimination) (2.8s)", "c", "o", "c") == ("win", 45, "by elimination")
    assert arena.parse_result("draw after 500 rounds", "c", "o", "c") == ("draw", 500, "")
    assert arena.parse_result("error: bad map", "c", "o", "c")[0] == "error"


def write_arena_map(rep_path, maps):
    os.makedirs(maps, exist_ok=True)
    with open(os.path.join(maps, "arena.map"), "w") as fh:
        fh.write(load(rep_path).map)                                       # a replay stores the .map file verbatim


def test_arena_starter_games(replays, tmp_path, monkeypatch):
    need_unswbc()
    bots = tmp_path / "bots"
    bots.mkdir()
    make_starter(str(bots), "starter")
    shutil.copytree(bots / "starter", bots / "starter_b")
    write_arena_map(replays["vs_starter"], str(bots / "maps"))
    for k, v in unswbc_env().items():
        monkeypatch.setenv(k, v)
    out = tmp_path / "out"
    rc = arena.main(["starter", "starter_b", "--bots", str(bots), "--maps", "arena", "-j", "2", "-o", str(out)])
    assert rc == 0
    summary = (out / "summary.txt").read_text()
    assert "starter vs starter_b:" in summary and "over 2 games" in summary and "0E" in summary
    assert sorted(os.listdir(out / "replays")) == ["arena__starter_b_vs_starter.replay", "arena__starter_vs_starter_b.replay"]


# --------------------------------------------------------------------------------------------------------- safety_line

def test_safety_rows(replays):
    r = safety_line.one((replays["vs_starter"], "abyss"))
    assert r["side"] == "A" and r["won"] and r["tle"] == 0 and r["nva"] == 0 and r["check"] is True and r["rule_ok"] is None
    assert "error" in safety_line.one((replays["vs_starter"], "nobody"))
    passed, v = safety_line.verdict([r, safety_line.one((replays["mirror"], "abyss"))])
    assert passed and v["replays"] == 2 and v["won"] == 1 and v["tle"] == 0
    bad = dict(r, tle=1)
    assert not safety_line.verdict([bad])[0]
    assert not safety_line.verdict([dict(r, max_cpu=96_000_000)])[0]
    assert not safety_line.verdict([dict(r, rule_ok=False)])[0]
    assert not safety_line.verdict([{"file": "x", "error": "replay: broken"}])[0]


def test_safety_cli(replays, tmp_path, capsys):
    d = os.path.dirname(replays["mirror"])
    assert safety_line.main(["abyss", d, "-j", "1", "--json", str(tmp_path / "s.json")]) == 0
    assert "SAFETY CHECK: PASS" in capsys.readouterr().out
    assert safety_line.main(["starter", d, "-j", "1"]) == 1               # the starter is not in every replay
    assert safety_line.main(["abyss", str(tmp_path / "empty"), "-j", "1"]) == 2


# ---------------------------------------------------------------------------------------------------- decision_harness

def test_normalise():
    assert dh.normalise("MOVE N E") == "MOVE NE" and dh.normalise("MOVE NE") == "MOVE NE"
    assert dh.normalise("SPLIT 3") == "SPLIT 3" and dh.normalise(None) == "NONE"


def test_harness_reproduces_the_starter(replays, tmp_path):
    """the starter template is deterministic: fed the rebuilt round blocks, it must make every recorded move again - a
    check that abyss_tools.replay.state renders the engine's protocol exactly"""
    need_cxx()
    starter = make_starter(str(tmp_path), "starter")
    exe = dh.build(starter, debug=False, work=str(tmp_path / "build"))
    out = tmp_path / "actions.tsv"
    with open(out, "w") as fh:
        n, agree = dh.run(replays["vs_starter"], "B", exe, actions=True, out=fh)
    assert n == 45 and agree == n
    with open(tmp_path / "logs.tsv", "w") as fh:
        dh.run(replays["vs_starter"], "B", exe, r0=10, r1=12, out=fh)
    rows = (tmp_path / "logs.tsv").read_text().splitlines()
    assert rows and all(r.split("\t")[0] in ("10", "11", "12") and "Moving in" in r for r in rows)
