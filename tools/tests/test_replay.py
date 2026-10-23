"""abyss_tools.replay: reading, summaries, comparison, per-turn state and the round-500 rule, on the fixture replays."""
import gzip
import os

import pytest

from abyss_tools.replay import diff, io, rules, state, stats, terrain


def test_names_and_files(tmp_path):
    assert io.base_name("abyss_L") == "abyss" and io.base_name("abyss_R") == "abyss" and io.base_name("abyss") == "abyss"
    assert io.map_stem("/x/tower_defense__a_vs_b_r1.replay.zst") == "tower_defense"
    assert io.arm_label("/runs/cand/replays") == "cand" and io.arm_label("/runs/cand/replays/") == "cand"
    assert io.arm_label("/runs/base") == "base"
    for f in ("stripes__a.replay", "stripes__b.replay", "stripes__c.replay.zst", "trauma__x.replay", "stripes__d.out",
              "stripes__e.zst", "stripes__f.replay.gz"):
        (tmp_path / f).write_bytes(b"")
    got = [os.path.basename(f) for f in io.replay_files([str(tmp_path)])]
    assert got == ["stripes__a.replay", "stripes__b.replay", "trauma__x.replay", "stripes__c.replay.zst",
                   "stripes__f.replay.gz"]
    assert [os.path.basename(f) for f in io.replay_files(str(tmp_path), "trauma")] == ["trauma__x.replay"]
    sub = tmp_path / "deep"
    sub.mkdir()
    (sub / "m__a_vs_b.replay").write_bytes(b"")
    (sub / "m__a_vs_b.replay.zst").write_bytes(b"")
    assert [os.path.basename(f) for f in io.replay_files_recursive(str(tmp_path))
            if "deep" in f] == ["m__a_vs_b.replay.zst"]          # one file per game: the compressed copy wins


def test_initial_teams():
    text = "MAP 4 4\nDRAGON 1 1 0 0\nDRAGON 0 1 3 3\nDRAGON 1 1 1 1\nDRAGON 0 1 2 2\nEND\n"
    assert io.initial_teams(text) == {0: "B", 1: "A", 2: "B", 3: "A"}


def test_load_plain_gzip_zstd(replays, tmp_path):
    raw = open(replays["vs_starter"], "rb").read()
    gz = tmp_path / "g.replay.gz"
    gz.write_bytes(gzip.compress(raw))
    a, b = io.load(replays["vs_starter"]), io.load(str(gz))
    assert a.botA == b.botA == "abyss" and a.botB == "starter" and len(a.events) == len(b.events)
    zstandard = pytest.importorskip("zstandard")
    zs = tmp_path / "z.replay.zst"
    zs.write_bytes(zstandard.ZstdCompressor().compress(raw))
    assert len(io.load(str(zs)).events) == len(a.events)
    zs2 = tmp_path / "z2.replay.zst"                           # gzip inside zstd
    zs2.write_bytes(zstandard.ZstdCompressor().compress(gzip.compress(raw)))
    assert len(io.load(str(zs2)).events) == len(a.events)


def test_stats(replays):
    s = stats.summarise(replays["vs_starter"])
    assert s["map"] == "Arena" and s["winner"] == "A" and s["rounds"] == 45
    assert s["bots"] == {"A": "abyss", "B": "starter"}
    assert s["standing"]["B"][0] == 0 and s["standing"]["A"][0] == 6
    assert s["splits"]["A"] == 7 and s["tle"]["A"] == 0
    m = stats.summarise(replays["mirror"])
    assert m["winner"] == "B" and m["rounds"] == 179 and m["standing"]["A"] == (0, 0, 0)
    assert m["splits"]["A"] == 22 and m["splits"]["B"] == 71 and m["standing"]["B"] == (24, 4, 63)
    lines = []
    stats.show(m, lines.append)
    assert lines[0].startswith("== arena__abyss_vs_abyss_b.replay  map=Arena  rounds=179  winner=B")


def test_diff(replays, tmp_path):
    assert diff.compare(replays["vs_starter"], replays["vs_starter"])[:2] == (True, "identical")
    same, why, na, nb = diff.compare(replays["vs_starter"], replays["mirror"])
    assert not same and why.startswith("event ") and na < nb
    gz = tmp_path / "copy.replay.gz"
    gz.write_bytes(gzip.compress(open(replays["mirror"], "rb").read()))
    assert diff.compare(replays["mirror"], str(gz))[0]
    with pytest.raises(SystemExit) as e:
        diff.main([replays["vs_starter"], replays["mirror"]])
    assert e.value.code == 1


def test_state_blocks(replays):
    rep = io.load(replays["vs_starter"])
    board = state.Board(rep.map)
    assert (board.w, board.h) == (11, 11) and board.unit_limit > 0 and len(board.dragons) == 2
    seen = 0
    for me, block, label in state.turns(rep):
        seen += 1
        assert block[0].startswith("ROUND ") and block[1].startswith("DIR ") and block[2].startswith("LENGTH ")
        n_msgs = int(block[4].split()[1])
        k = 5 + n_msgs
        if me.initial and me.turns == 1:                         # a starting dragon's first turn has no ECHOES line
            assert not block[k].startswith("ECHOES")
        else:
            assert block[k].startswith("ECHOES ")
            k += 1
        tiles = block[k:k + 49]
        assert all(len(t.split()) == 4 for t in tiles)
        assert block[k + 49].startswith("DRAGON_BODIES ")
        parts = int(block[k + 49].split()[1])
        rest = block[k + 50 + parts:]
        assert len(rest) == 15 and all(len(r.split()) in (7, 8) for r in rest)
        assert label == "NONE" or label.startswith(("MOVE ", "SPLIT "))
    assert seen > 45                                             # both teams, every dragon, every round
    only_b = list(state.turns(rep, only_team="B"))
    assert len(only_b) == 45 and all(me.team == "B" for me, _, _ in only_b)


def test_state_cli(replays, capsys):
    state.main([replays["vs_starter"], "--team", "B", "--limit", "2"])
    out = capsys.readouterr().out.splitlines()
    assert out[0].startswith("REC arena__abyss_vs_starter 1 B ") and out.count("END") == 2


def test_rules(replays):
    for path in (replays["vs_starter"], replays["mirror"]):
        m = rules.measure(path)
        assert m["check"] is True                                # the tracked standings equal the replay's result
        assert m["how"] == "wipe" and m["rule_ok"] is None
        assert set(m["queen_ids"]) == {"A", "B"} and sorted(m["queen_ids"].values()) == [0, 1]
    m = rules.measure(replays["vs_starter"])
    assert m["winner"] == "A" and m["rounds"] == 45 and m["qdeath"]["B"][0] == 44
    sp = m["spawn"]["r0-199"]
    assert sp["n"] == sp.get("closer A", 0) + sp.get("closer B", 0) + sp.get("closer tie", 0)


def test_terrain_helpers(replays):
    assert terrain.cheb((0, 0), (9, 9), 10, 10) == 1 and terrain.manhattan((0, 0), (9, 9), 10, 10) == 2
    assert [terrain.phase(r) for r in (0, 99, 100, 250, 499)] == [0, 0, 1, 2, 3]
    assert terrain.default_side("/runs/cand_vs_opp/replays") == "cand" and terrain.default_side("/x/y") is None
    rep = io.load(replays["vs_starter"])
    assert terrain.side_of(rep, "abyss") == "A" and terrain.side_of(rep, "starter") == "B"
    assert terrain.map_key(rep) == terrain.map_key(rep.map) and len(terrain.map_key(rep)) == 12


def test_dist_matrix(replays, tmp_path, monkeypatch):
    pytest.importorskip("numpy")
    pytest.importorskip("scipy")
    monkeypatch.setenv("ABYSS_CACHE", str(tmp_path))
    rep = io.load(replays["vs_starter"])
    bd = state.Board(rep.map)
    key = terrain.map_key(rep)
    D = terrain.dist_matrix(bd, key)
    assert D.shape == (bd.w * bd.h, bd.w * bd.h) and int(D[0, 0]) == 0
    assert os.path.exists(os.path.join(str(tmp_path), "dist", key + ".npy"))
    # a step that the board allows is distance 1
    for d in "NESW":
        t = bd.step((4, 4), d)
        if t is not None and t != (4, 4):
            assert int(D[4 * bd.w + 4, t[1] * bd.w + t[0]]) == 1
    assert (terrain.dist_matrix(bd, key) == D).all()            # the cached copy
