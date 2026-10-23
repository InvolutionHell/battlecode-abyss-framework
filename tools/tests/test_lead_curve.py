"""abyss_tools.kpi.lead_curve: chokepoints, bed clusters, the lead cells and index (no replays needed), and one run over the
fixtures."""
import os

import pytest

np = pytest.importorskip("numpy")

from abyss_tools.kpi import lead_curve as lc  # noqa: E402


def grid_graph(cells):
    """undirected 4-neighbour graph of a set of (x, y) cells: (adjacency lists, node index of a cell)"""
    idx = {c: i for i, c in enumerate(sorted(cells))}
    adj = [[] for _ in idx]
    for (x, y), i in idx.items():
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            j = idx.get((x + dx, y + dy))
            if j is not None:
                adj[i].append(j)
    return [sorted(a) for a in adj], idx


def test_chokepoints_dumbbell():
    # two 4x4 rooms joined by a 3-cell corridor, and a 2-cell stub on the left room
    left = {(x, y) for x in range(4) for y in range(4)}
    right = {(x, y) for x in range(7, 11) for y in range(4)}
    corridor = {(4, 1), (5, 1), (6, 1)}
    stub = {(-1, 0), (-2, 0)}
    adj, idx = grid_graph(left | right | corridor | stub)
    cut = lc.chokepoints(adj, list(range(len(adj))), m=8)
    got = {c for c, i in idx.items() if i in cut}
    # the corridor cells and the two room cells it attaches to separate 16+ cells from 16+ cells
    assert {(4, 1), (5, 1), (6, 1)} <= got, got
    assert (3, 1) in got and (7, 1) in got, got
    # the stub's attachment is an articulation point, but the stub (2 cells) is below m
    assert (0, 0) not in got and (-1, 0) not in got, got
    assert len(got) == 5, got


def test_chokepoints_none_on_a_cycle():
    ring = {(x, 0) for x in range(10)} | {(x, 2) for x in range(10)} | {(0, 1), (9, 1)}
    adj, idx = grid_graph(ring)
    assert lc.chokepoints(adj, list(range(len(adj))), m=3) == set()


def test_chokepoints_root_with_two_big_children():
    # a cross whose centre is the DFS root (lowest index after sorting is not the centre, so start the DFS at it explicitly)
    arms = {(0, y) for y in range(-9, 10)} | {(x, 0) for x in range(-9, 10)}
    adj, idx = grid_graph(arms)
    centre = idx[(0, 0)]
    order = [centre] + [i for i in range(len(adj)) if i != centre]
    cut = lc.chokepoints(adj, order, m=8)
    assert centre in cut
    assert idx[(0, 9)] not in cut and idx[(9, 0)] not in cut


def test_split_wide_line():
    n = 40
    D = np.abs(np.arange(n)[:, None] - np.arange(n)[None, :]).astype(np.int16)
    parts = lc.split_wide(D, list(range(n)), 8.0)
    assert len(parts) >= 3, parts
    assert sorted(x for p in parts for x in p) == list(range(n))
    for p in parts:                       # every bed within D* of its own centre: no part spans more than 2 D*
        assert max(p) - min(p) <= 16, p
    # a tight cluster is left alone
    assert lc.split_wide(D, list(range(6)), 8.0) == [list(range(6))]


def game(rounds=500, us=None, them=None, terr=None, spawn=1, eat=(1, 0), cov=(1, 0), final=None):
    """a synthetic walk() result: A = us; per-checkpoint standings [units, total, longest, queen]"""
    us = us or {}
    them = them or {}
    terr = terr or {}
    snaps, tr = {}, {}
    for t in sorted(lc.SNAP):
        if t < rounds:
            snaps[str(t)] = {"A": us.get(t, [10, 30, 5, 3]), "B": them.get(t, [9, 28, 5, 3])}
            tr[str(t)] = terr.get(t, (0.5, 0.4))
    return {"rounds": rounds, "winner": "A", "snaps": snaps, "terr": tr,
            "spawn": [spawn] * 500, "eat": {"A": [eat[0]] * 500, "B": [eat[1]] * 500},
            "cov": {"A": [cov[0]] * 500, "B": [cov[1]] * 500},
            "final": final or {"A": [10, 30, 5, 3], "B": [9, 28, 5, 3]}, "local": {}}


def test_lead_all_ahead_but_one():
    g = game(terr={40: (0.4, 0.5)})
    L = lc.lead(g, "A")
    assert L["cells_n"] == 30
    assert L["led"] == 29 and abs(L["index"] - 29 / 30) < 1e-12
    assert L["lag"] == (40, "terr") and L["lag_round"] == 40
    assert L["major_round"] == 500
    # the other side sees the mirror image
    M = lc.lead(g, "B")
    assert M["led"] == 1 and M["lag"] == (10, "total")


def test_lead_ties_are_not_leads_and_empty_windows_drop_out():
    g = game(spawn=0, us={t: [9, 28, 5, 3] for t in lc.SNAP}, terr={t: (0.4, 0.4) for t in lc.SNAP})
    L = lc.lead(g, "A")
    assert L["cells_n"] == 18                       # eat / cov have no spawns: 6 checkpoints x 3 dimensions left
    assert L["led"] == 0 and L["lag"] is None and L["lag_round"] == 500 and L["major_round"] == 500


def test_majority_behind():
    them = {80: [12, 40, 5, 3]}                     # behind on units and total at r80
    g = game(them=them, terr={80: (0.3, 0.6)})      # and on territory: 3 of 5
    L = lc.lead(g, "A")
    assert L["major_round"] == 80, L["major_round"]
    assert L["lag"] == (80, "total")
    g2 = game(them=them)                            # 2 of 5 behind: not a majority
    assert lc.lead(g2, "A")["major_round"] == 500


def test_wipe_carries_the_final_standing():
    # we are wiped at r150: r160 and r320 use the final standing (0 units, 0 territory) and empty windows
    g = game(rounds=150, final={"A": [0, 0, 0, 0], "B": [7, 40, 12, 6]})
    L = lc.lead(g, "A")
    assert L["post"][160] and L["post"][320] and not L["post"][80]
    assert L["cells"][160]["units"] == -7 and L["cells"][160]["terr"] == -1.0
    assert L["cells"][320]["eat"] is None and L["cells"][320]["cov"] is None
    assert L["cells"][160]["eat"] is not None       # the r80-r159 window is partly played
    assert L["end"][500]["sign"] == -1 and L["end"][400]["sign"] == -1


def test_end_tiebreak_order():
    # r500: the queen first (dead = 0), then the longest, then the total
    g = game(final={"A": [3, 20, 15, 0], "B": [5, 30, 8, 2]})
    assert lc.lead(g, "A")["end"][500]["sign"] == -1
    g = game(final={"A": [3, 20, 15, 4], "B": [5, 30, 18, 4]})
    assert lc.lead(g, "A")["end"][500]["sign"] == -1
    g = game(final={"A": [3, 31, 18, 4], "B": [5, 30, 18, 4]})
    assert lc.lead(g, "A")["end"][500]["sign"] == 1


def test_windows():
    assert lc.WIN[:6] == ((0, 10), (10, 20), (20, 40), (40, 80), (80, 160), (160, 320))
    # eat / cov as shares of the window's spawns: 2 spawns a round, we eat 1 a round and they eat 0
    g = game(spawn=2, eat=(1, 0), cov=(2, 1))
    L = lc.lead(g, "A")
    assert abs(L["cells"][40]["eat"] - 0.5) < 1e-12 and abs(L["cells"][40]["cov"] - 0.5) < 1e-12


def test_local_stats():
    reg = np.array([0, 0, 0, 1, 1, 1, -1])
    dA = np.array([0, 1, 2, 9, 9, 9, 30000])
    dB = np.array([3, 2, 1, 2, 1, 0, 30000])
    # region 0: both within R=3, A owns 2 tiles, B 1 -> contested, A leads; region 1: A is 9 away -> not contested
    assert lc.local_stats(reg, 2, 3, dA, dB) == (1, 1, 0)
    assert lc.local_stats(reg, 2, 9, dA, dB) == (2, 1, 1)


def test_pair_key():
    assert lc.pair_key("/x/stripes__cand_vs_opp.03_r1.replay.zst", "A") == ("stripes", "A", "1")
    assert lc.pair_key("/y/stripes__base_vs_opp.03_r1.replay", "A") == ("stripes", "A", "1")
    assert lc.pair_key("/y/stripes__opp.03_vs_base.replay", "B") == ("stripes", "B", "0")


def test_boot_ci():
    m, lo, hi = lc.boot_ci([1.0] * 10)
    assert m == lo == hi == 1.0
    m, lo, hi = lc.boot_ci(list(range(100)))
    assert lo < m < hi


def test_split_wide_needs_scipy_or_not():
    # split_wide works on a plain distance matrix
    D = np.zeros((3, 3), dtype=np.int16)
    assert lc.split_wide(D, [0, 1, 2], 8.0) == [[0, 1, 2]]


def test_run_on_fixtures(replays, tmp_path, monkeypatch, capsys):
    pytest.importorskip("scipy")
    monkeypatch.setenv("ABYSS_CACHE", str(tmp_path))
    lc.main([os.path.dirname(replays["mirror"]), "--side", "abyss", "-j", "1", "--json", str(tmp_path / "g.jsonl")])
    out = capsys.readouterr().out
    assert "### abyss: 2 games, won 1 (50%)" in out and "lead index r10-r320" in out
    assert len((tmp_path / "g.jsonl").read_text().splitlines()) == 2
